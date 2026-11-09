"""Compare native controller perception with its original lifted body."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/ai-perception-native-tests"
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
    bodies = []
    for address, owner in [("00FC8DE0", "0458"), ("00FCBCB0", "0459"), ("00CE85C0", "0334")]:
        source = (ROOT / f"generated/ut3/functions_{owner}.c").read_text()
        match = re.search(r"void sub_" + address + r"\(void\) \{.*?(?=\nvoid sub_|\Z)", source, re.S)
        assert match
        body, rewritten, kept = x87_slots.transform_file(match[0], "ut3")
        assert body == match[0]
        target = "original_world_controllers" if address == "00CE85C0" else "__real_sub_" + address
        bodies.append(body.replace("void sub_" + address, "void " + target, 1))
    oracle = OUT / "oracle.c"
    oracle.write_text('#define RECOMP_GENERATED_CODE\n#include "recomp_types.h"\n'
        'void sub_00486FD0(void),sub_00CE85C0(void),sub_00FC99E0(void),sub_00FC9980(void);\n'
        + "\n".join(bodies))
    binary = OUT / "test-ai-perception-native"
    command = [str(clang), "--target=aarch64-linux-android29", "-O2", "-fno-strict-aliasing", "-fwrapv",
        "-ffp-contract=off", "-fno-math-errno", "-DRECOMP_X87_WINDOW", "-Iandroid/runtime", "-Iandroid/host",
        str(context), str(oracle), str(ROOT / "tests/test_ai_perception_native.c"), "-o", str(binary), "-lm"]
    built = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    (OUT / "build.log").write_text(built.stdout + built.stderr)
    assert built.returncode == 0, built.stdout + built.stderr
    print(binary)
    if args.build_only:
        return
    adb = [str(sdk / "platform-tools/adb.exe"), "-s", args.serial]
    remote = "/data/local/tmp/ut3-test-ai-perception-native"
    try:
        subprocess.run([*adb, "push", str(binary), remote], check=True, capture_output=True)
        subprocess.run([*adb, "shell", "chmod", "755", remote], check=True, capture_output=True)
        ran = subprocess.run([*adb, "shell", remote], capture_output=True, text=True, timeout=90)
        (OUT / "result.txt").write_text(ran.stdout + ran.stderr)
        print(ran.stdout + ran.stderr, end="")
        assert ran.returncode == 0 and "PASS native AI perception:" in ran.stdout
    finally:
        subprocess.run([*adb, "shell", "rm", "-f", remote], capture_output=True)


if __name__ == "__main__":
    main()
