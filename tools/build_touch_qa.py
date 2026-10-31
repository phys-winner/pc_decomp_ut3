"""Build the standalone shell UiAutomation touch QA dex; never starts the game."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def run(*arguments):
    subprocess.run([str(argument) for argument in arguments], cwd=ROOT, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check-device", help="Push read-only dex, then run --check and no-input --self-test on this serial")
    args = parser.parse_args()
    sdk = Path(os.environ.get("ANDROID_SDK_ROOT", Path.home() / "AppData/Local/Android/Sdk"))
    java = Path(os.environ.get("JAVA_HOME", "C:/Portable/jdk-24.0.1")) / "bin"
    android = sdk / "platforms/android-36/android.jar"
    out = ROOT / "build/touchqa"
    classes, dex = out / "classes", out / "dex"
    for directory in (classes, dex):
        directory.mkdir(parents=True, exist_ok=True)
    source = ROOT / "tests/touchqa/src/org/ut3/touchqa/Main.java"
    run(java / "javac.exe", "--release", "8", "-encoding", "UTF-8", "-cp", android, "-d", classes, source)
    run(java / "java.exe", "-cp", sdk / "build-tools/36.0.0/lib/d8.jar", "com.android.tools.r8.D8",
        "--lib", android, "--min-api", "30", "--output", dex, *sorted(classes.rglob("*.class")))
    binary = dex / "classes.dex"
    digest = hashlib.sha256(binary.read_bytes()).hexdigest()
    remote = "/data/local/tmp/ut3-touchqa-" + digest[:12] + ".dex"
    receipt = {"source": str(source.relative_to(ROOT)), "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
               "dex": str(binary), "dex_sha256": digest, "remote": remote,
               "main": "org.ut3.touchqa.Main", "production_changes": False, "minimum_api": 30}
    (out / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(receipt, indent=2), flush=True)
    if args.check_device:
        adb = sdk / "platform-tools/adb.exe"
        existing = subprocess.run([str(adb), "-s", args.check_device, "shell", "sha256sum", remote], capture_output=True, text=True)
        if existing.returncode == 0:
            if existing.stdout.split()[0] != digest:
                raise ValueError("Existing QA dex does not match its content-addressed filename")
        else:
            run(adb, "-s", args.check_device, "push", binary, remote)
            run(adb, "-s", args.check_device, "shell", "chmod", "444", remote)
        for command in ("--check", "--self-test"):
            run(adb, "-s", args.check_device, "shell", "env", "CLASSPATH=" + remote,
                "app_process", "/system/bin", "org.ut3.touchqa.Main", command)


if __name__ == "__main__":
    main()
