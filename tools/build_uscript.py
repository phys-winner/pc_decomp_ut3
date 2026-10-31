"""Compile an UnrealScript package with the pinned local UT3 compiler in isolation."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def prepare(work, original, package):
    for directory in ("Binaries", "Engine/Config", "UTGame/Config", "UTGame/Src",
                      "UTGame/Logs", "UTGame/Script", "UTGame/Published/CookedPC/Script",
                      "UTGame/Unpublished/CookedPC/Script"):
        (work / directory).mkdir(parents=True, exist_ok=True)
    for source in (original / "Binaries").iterdir():
        if source.is_file() and source.suffix.lower() in (".exe", ".dll", ".com", ".txt"):
            target = work / "Binaries" / source.name
            if not target.exists() or sha(source) != sha(target):
                shutil.copy2(source, target)
    for directory in ("Engine/Config", "UTGame/Config"):
        for source in (original / directory).iterdir():
            if source.is_file():
                shutil.copy2(source, work / directory / source.name)
    # These contain immutable compiler inputs; source, config and outputs stay private.
    for directory in ("UTGame/CookedPC", "UTGame/Localization", "Engine/Localization", "Engine/Shaders"):
        target = work / directory
        if target.exists():
            if target.resolve() != (original / directory).resolve():
                raise ValueError(f"Unexpected compiler input directory: {target}")
        else:
            quote = lambda value: "'" + str(value).replace("'", "''") + "'"
            subprocess.run(["powershell", "-NoProfile", "-Command",
                "New-Item -ItemType Junction -Path " + quote(target) + " -Target " +
                quote(original / directory) + " | Out-Null"], check=True)
    editor = work / "UTGame/Config/DefaultEditor.ini"
    editor.write_text(editor.read_text(encoding="utf-8-sig") +
        "\n[ModPackages]\nModPackages=" + package +
        "\nModPackagesInPath=..\\UTGame\\Src\nModOutputDir=..\\UTGame\\Unpublished\\CookedPC\\Script\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", default="AndroidPort")
    parser.add_argument("--source", type=Path, default=ROOT / "android/uscript/AndroidPort")
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]{0,63}", args.package):
        parser.error("Package must be an UnrealScript identifier")
    sources = sorted((args.source.resolve() / "Classes").glob("*.uc"))
    if not sources:
        parser.error("Source must contain Classes/*.uc")
    manifest = json.loads((ROOT / "config/inputs.json").read_text())
    original = Path(manifest["source_root"])
    expected = next(row["sha256"] for row in manifest["modules"] if row["path"] == "Binaries/UT3.exe")
    if sha(original / "Binaries/UT3.exe") != expected:
        parser.error("Original UT3 compiler hash differs from config/inputs.json")
    work = ROOT / "build/uscript-compiler"
    prepare(work, original, args.package)
    classes = work / "UTGame/Src" / args.package / "Classes"
    classes.mkdir(parents=True, exist_ok=True)
    for stale in classes.glob("*.uc"):
        if stale.name not in {path.name for path in sources}:
            stale.unlink()
    for source in sources:
        shutil.copyfile(source, classes / source.name)
    output = work / "UTGame/Unpublished/CookedPC/Script" / (args.package + ".u")
    output.unlink(missing_ok=True)
    # Remove only generated config in this private compiler checkout.
    for generated in (work / "UTGame/Config").glob("UT*.ini"):
        generated.unlink()
    log = work / "UTGame/Logs/AndroidPortCompiler.log"
    log.unlink(missing_ok=True)
    command = [str(work / "Binaries/UT3.exe"), "make", "-unattended", "-silent", "-silentbuild",
               "-nop4", "-nullrhi", "-nosound", "-nosplash", "-nomoviestartup", "-nohomedir",
               "-useunpublished", "-log=" + log.name]
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    begin = time.monotonic()
    result = subprocess.run(command, cwd=work / "Binaries", stdin=subprocess.DEVNULL,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=60, startupinfo=startup,
        creationflags=subprocess.CREATE_NO_WINDOW | subprocess.CREATE_NEW_PROCESS_GROUP)
    text = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
    success = re.search(r"Success - 0 error\(s\), 0 warning\(s\)", text)
    if result.returncode or not success or not output.is_file():
        print("\n".join(line for line in text.splitlines() if any(word in line for word in
            ("Error,", "Warning,", "Error:", "Failed", "Success -")))[-12000:])
        raise SystemExit(f"UT3 make failed (exit={result.returncode}); see {log}")
    data = bytearray(output.read_bytes())
    if len(data) < 69 or struct.unpack_from("<II", data) != (0x9E2A83C1, 512):
        raise ValueError("Compiler did not produce the expected version-512 UE3 package")
    # The port's classes are local UI only. A listen server sends every loaded package
    # without ServerSideOnly to its clients, and a PC joining an Android game has no
    # AndroidPort: ServerSideOnly (0x4), not AllowDownload (0x1), as the original
    # SetPackageFlags commandlet would set them in the summary.
    folder = struct.unpack_from("<i", data, 12)[0]
    at = 16 + (folder if folder >= 0 else -2 * folder)
    flags = struct.unpack_from("<I", data, at)[0]
    struct.pack_into("<I", data, at, (flags | 0x4) & ~0x1)
    destination = ROOT / "build/uscript" / output.name
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(data)
    receipt = {"package": args.package, "output": str(destination), "sha256": sha(destination),
        "size": len(data), "compiler_sha256": expected, "command": command,
        "exit_code": result.returncode, "seconds": time.monotonic() - begin,
        "log": str(log), "log_sha256": sha(log), "tool_sha256": sha(Path(__file__)),
        "sources": {str(path.relative_to(ROOT)): sha(path) for path in sources},
        "original_inputs": {str(path.relative_to(original)): sha(path) for path in
            sorted((original / "UTGame/CookedPC").glob("*.u"))},
        "runtime": "NOT YET VERIFIED"}
    destination.with_suffix(".json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: receipt[key] for key in ("output", "sha256", "size", "seconds", "exit_code")}))


if __name__ == "__main__":
    main()
