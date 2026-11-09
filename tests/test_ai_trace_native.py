"""Build/run native AI trace replacements against the unmodified lifted functions."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/ai-trace-native-tests"
sys.path.insert(0, str(ROOT / "tools"))
import x87_slots


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", default=os.environ.get("UT3_TEST_ANDROID"))
    parser.add_argument("--build-only", action="store_true")
    args = parser.parse_args()
    if not args.build_only and not args.serial:
        parser.error("--serial or --build-only is required")
    sdk = Path(os.environ.get("ANDROID_SDK_ROOT", Path.home() / "AppData/Local/Android/Sdk"))
    ndk = Path(os.environ.get("ANDROID_NDK_HOME", sdk / "ndk/27.2.12479018"))
    clang = ndk / "toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe"
    OUT.mkdir(parents=True, exist_ok=True)
    core = (ROOT / "android/host/core.c").read_text()
    context = OUT / "context.c"
    context.write_text('#include "host.h"\n' + core[core.index("VOID_HIDDEN uint32_t g_eax"):
        core.index("/* --------------------------------------------------------------- logging */")])
    oracle = OUT / "oracle.c"
    bodies = []
    for address, owner in [("01156630", "0523")]:
        text = (ROOT / f"generated/ut3/functions_{owner}.c").read_text()
        match = re.search(r"void sub_" + address + r"\(void\) \{.*?(?=\nvoid sub_|\Z)", text, re.S)
        assert match, address
        body = match[0]
        if address == "01156630":
            body, rewritten, kept = x87_slots.transform_file(body, "ut3")
            assert rewritten == 1 and kept == 0, (rewritten, kept)
        bodies.append(body.replace("void sub_" + address, "void __real_sub_" + address, 1))
    oracle.write_text('#define RECOMP_GENERATED_CODE\n#include "recomp_types.h"\n' + "\n".join(bodies))
    binary = OUT / "test-ai-trace-native"
    sources = [context, oracle, ROOT / "tests/test_ai_trace_native.c"]
    command = [str(clang), "--target=aarch64-linux-android29", "-O2", "-fno-strict-aliasing", "-fwrapv",
        "-ffp-contract=off", "-fno-math-errno", "-DRECOMP_X87_WINDOW", "-Iandroid/runtime", "-Iandroid/host",
        *map(str, sources), "-o", str(binary), "-lm"]
    built = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    (OUT / "build.log").write_text(built.stdout + built.stderr)
    assert built.returncode == 0, built.stdout + built.stderr
    result = None
    if not args.build_only:
        adb = [str(sdk / "platform-tools/adb.exe"), "-s", args.serial]
        remote = "/data/local/tmp/ut3-test-ai-trace-native"
        subprocess.run([*adb, "push", str(binary), remote], check=True, capture_output=True)
        try:
            subprocess.run([*adb, "shell", "chmod", "755", remote], check=True, capture_output=True)
            ran = subprocess.run([*adb, "shell", remote], capture_output=True, text=True, timeout=60)
            result = {"returncode": ran.returncode, "stdout": ran.stdout, "stderr": ran.stderr}
            print(ran.stdout + ran.stderr, end="")
        finally:
            subprocess.run([*adb, "shell", "rm", "-f", remote], capture_output=True)
    files = [*sources, ROOT / "android/host/ai_trace_native.c", ROOT / "android/runtime/recomp_types.h",
        ROOT / "tools/x87_slots.py", Path(__file__), binary]
    receipt = {"status": "BUILT_NOT_RUN" if result is None else "PASS" if result["returncode"] == 0 and
        "PASS native AI trace:" in result["stdout"] else "FAIL", "device": args.serial, "command": command,
        "result": result, "sha256": {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in files}}
    (OUT / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    assert receipt["status"] != "FAIL", receipt
    print(receipt["status"], binary)


if __name__ == "__main__":
    main()
