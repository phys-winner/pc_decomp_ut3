"""Verify and stage pinned ARM64 OpenAL/Oboe DSOs, licenses and build provenance.

Staging is offline and uses only the prebuilt snapshot in reference/audio-runtime (imported once from
the Frostpunk port, whose files dependencies-audio.json pins as import_pins).
--rebuild replays the exact retained recipe in a private build directory from the pristine
OpenAL Soft and Oboe sources in reference/openal-soft and reference/oboe (dependencies-audio.json
names their upstream tags); it never runs its device fixture, and accepts only byte-identical outputs.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SDK = Path(os.environ.get("ANDROID_SDK_ROOT", Path.home() / "AppData/Local/Android/Sdk"))
REFERENCE = ROOT / "reference/audio-runtime"
MANIFEST = ROOT / "dependencies-audio.json"
BUILD = ROOT / "build/native-audio"


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def verify(path, digest):
    if not path.is_file() or sha(path) != digest:
        raise ValueError("Native audio hash mismatch or missing file: " + str(path))


def copy_verified(source, destination, digest):
    verify(source, digest)
    if destination.exists():
        verify(destination, digest)
    else:
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
        verify(destination, digest)


def load(path):
    return json.loads(path.read_text(encoding="utf-8"))


def relpath(text):
    return Path(text.replace("\\", "/"))


def verify_snapshot(manifest, sources):
    """The prebuilt snapshot; with sources, also the pristine upstream trees a rebuild compiles."""
    snapshot = REFERENCE.relative_to(ROOT).as_posix() + "/"
    for relative, digest in manifest["files"].items():
        if sources or relative.startswith(snapshot):
            verify(ROOT / relative, digest)
    count = 0
    for name, source in manifest["sources"].items() if sources else ():
        info = load(ROOT / source["provenance"])
        if info["revision"] != source["revision"]:
            raise ValueError("Native audio revision changed: " + name)
        tree = ROOT / "reference" / name / "source"
        expected = {relpath(relative).as_posix() for relative in info["source_tree_sha256"]}
        actual = {p.relative_to(tree).as_posix() for p in tree.rglob("*") if p.is_file()}
        if actual != expected:
            raise ValueError("Unexpected or missing pristine source files: " + name)
        for relative, digest in info["source_tree_sha256"].items():
            verify(tree / relpath(relative), digest)
            count += 1
    for name, relative in manifest["runtime"].items():
        data = (ROOT / relative).read_bytes()[:64]
        if data[:6] != b"\x7fELF\x02\x01" or struct.unpack_from("<H", data, 18)[0] != 183:
            raise ValueError("Expected little-endian AArch64 ELF64: " + name)
    return count


def rebuild(manifest):
    config = dict(manifest["toolchain"], android_sdk=str(SDK))
    sdk = SDK
    compiler = sdk / "ndk" / config["android_ndk_version"] / "toolchains/llvm/prebuilt/windows-x86_64/bin/clang++.exe"
    verify(compiler, config["compiler_sha256"])
    private = BUILD / "rebuild"
    for name in manifest["sources"]:
        shutil.copytree(ROOT / "reference" / name, private / "reference" / name, dirs_exist_ok=True)
    for name in ("metrics.cpp", "metrics.h", "native_probe.cpp"):
        target = private / "android/audio" / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(REFERENCE / "rebuild/android/audio" / name, target)
    recipe = private / "tools/build_native_audio.py"
    recipe.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ROOT / manifest["upstream_recipe"], recipe)
    (private / "research").mkdir(exist_ok=True)
    (private / "port.json").write_text(json.dumps(config, indent=2) + "\n")
    # Do not pass --run: the retained recipe's device fixture is never executed.
    subprocess.run([sys.executable, str(recipe)], cwd=private, check=True)
    for name, relative in manifest["runtime"].items():
        verify(private / "build/native-audio/runtime" / name, manifest["files"][relative])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rebuild", action="store_true")
    args = parser.parse_args()
    if not MANIFEST.is_file():
        parser.error("Missing dependencies-audio.json")
    manifest = load(MANIFEST)
    count = verify_snapshot(manifest, args.rebuild)
    if args.rebuild:
        rebuild(manifest)
    for group in ("runtime", "licenses"):
        for name, relative in manifest[group].items():
            copy_verified(ROOT / relative, BUILD / group / name, manifest["files"][relative])
    copy_verified(ROOT / manifest["upstream_receipt"], BUILD / "upstream-build-receipt.json",
                  manifest["files"][manifest["upstream_receipt"]])
    report = {"timestamp_utc": datetime.now(timezone.utc).isoformat(), "mode": "rebuild" if args.rebuild else "pinned-stage",
              "manifest_sha256": sha(MANIFEST), "tool_sha256": sha(Path(__file__)),
              "verified_source_files": count, "runtime": {name: sha(BUILD / "runtime" / name) for name in manifest["runtime"]},
              "licenses": {name: sha(BUILD / "licenses" / name) for name in manifest["licenses"]},
              "configuration": manifest["configuration"], "device_commands_executed": False,
              "current_UT3_device_audio_verified": False}
    (BUILD / "build-receipt.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
