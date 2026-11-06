"""Extract the UT3 PhysX MSI without running its installer; prepare a game-root overlay (Windows, 7-Zip)."""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile


def files(msi):
    api = ctypes.WinDLL("msi")
    database, view = ctypes.c_uint(), ctypes.c_uint()
    def checked(result):
        if result:
            raise OSError(result, "Cannot read MSI file table")
    checked(api.MsiOpenDatabaseW(str(msi), None, ctypes.byref(database)))
    try:
        checked(api.MsiDatabaseOpenViewW(database, "SELECT File, FileName, FileSize FROM File", ctypes.byref(view)))
        checked(api.MsiViewExecute(view, 0))
        result = []
        while True:
            record = ctypes.c_uint()
            status = api.MsiViewFetch(view, ctypes.byref(record))
            if status == 259:
                return result
            checked(status)
            try:
                row = []
                for field in (1, 2, 3):
                    text, size = ctypes.create_unicode_buffer(4096), ctypes.c_uint(4096)
                    checked(api.MsiRecordGetStringW(record, field, text, ctypes.byref(size)))
                    row.append(text.value)
                result.append((row[0], row[1].split("|")[-1], int(row[2])))
            finally:
                api.MsiCloseHandle(record)
    finally:
        if view.value:
            api.MsiCloseHandle(view)
        api.MsiCloseHandle(database)


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("msi", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--seven-zip", default=shutil.which("7z") or r"C:\Program Files\7-Zip\7z.exe")
    args = parser.parse_args()
    msi, output = args.msi.resolve(), args.output.resolve()
    if output.exists():
        parser.error("Output already exists; choose a new directory")
    rows = files(msi)
    if not rows or not any(name == "PhysX.u" for _, name, _ in rows):
        parser.error("Not the UT3 PhysX Mod Pack")
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="ut3-physx-", dir=output.parent) as temporary:
        stage = Path(temporary)
        raw, prepared = stage / "raw", stage / "prepared"
        subprocess.run([args.seven_zip, "x", "-y", "-o" + str(raw), str(msi)], check=True)
        manifest, targets = [], set()
        for identifier, name, size in rows:
            if any(c in identifier + name for c in "/\\:") or name in (".", ".."):
                raise ValueError("Unsafe MSI filename")
            source = raw / identifier
            if not source.is_file() or source.stat().st_size != size:
                raise ValueError("Missing or truncated cabinet file: " + identifier)
            suffix = Path(name).suffix.lower()
            if suffix in (".u", ".upk", ".ut3"):
                relative = Path("game/UTGame/CookedPC/PhysXModPack") / name
            elif suffix == ".ini":
                relative = Path("game/UTGame/Config") / name
            elif suffix == ".int":
                relative = Path("game/UTGame/Localization/INT") / name
            else:
                relative = Path("documents") / identifier / name
            if relative.as_posix().lower() in targets:
                raise ValueError("Duplicate output: " + str(relative))
            targets.add(relative.as_posix().lower())
            target = prepared / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
            digest = sha(source)
            assert target.stat().st_size == size and sha(target) == digest
            manifest.append({"msi_file": identifier, "path": relative.as_posix(), "bytes": size, "sha256": digest})
        assert len(manifest) == len(rows)
        receipt = {"source": str(msi), "source_sha256": sha(msi), "files": manifest}
        (prepared / "manifest.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
        (prepared / "INSTALL.txt").write_text(
            "Copy the UTGame folder from game/ into the installed UT3 game folder (beside Engine and Binaries).\n"
            "For the Android port this is normally /sdcard/UT3/game/. Restart the game after copying.\n"
            "Maps: CTF-LightHouse_PhysX, CTF-Tornado_PhysX, DM-HeatRay_PhysX.\n"
            "The original Windows installer has NOT been executed. Original filenames come from its MSI File table.\n"
            "Native AGEIA/CUDA acceleration is unavailable on Android; PhysX effects require the port's CPU fallback.\n"
            "Original guides and licenses are in documents/. manifest.json verifies every extracted file.\n", encoding="utf-8")
        prepared.rename(output)
    print(json.dumps({"output": str(output), "files": len(rows), "bytes": sum(row[2] for row in rows)}))


if __name__ == "__main__":
    main()
