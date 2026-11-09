"""Run native draw-list traversal against original lifted traversal and iterator."""
import argparse
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/render-draw-native-tests"


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
    for address, unit, name in [("00F058D0", "0428", "__real_sub_00F058D0"), ("00F05700", "0428", "__real_sub_00F05700"),
                                ("00F05AA0", "0428", "__real_sub_00F05AA0"), ("00EFE060", "0427", "__real_sub_00EFE060"),
                                ("00EFE230", "0427", "__real_sub_00EFE230"),
                                ("00F05530", "0428", "__real_sub_00F05530"),
                                ("005DAF50", "0061", "original_iterator"),
                                ("00BF8120", "0286", "sub_00BF8120")]:
        text = (ROOT / f"generated/ut3/functions_{unit}.c").read_text()
        match = re.search(r"void sub_" + address + r"\(void\) \{.*?(?=\nvoid sub_|\Z)", text, re.S)
        assert match, address
        bodies.append(match[0].replace("void sub_" + address, "void " + name, 1))
    oracle = OUT / "oracle.c"
    oracle.write_text('#define RECOMP_GENERATED_CODE\n#include "recomp_types.h"\n'
        'void sub_005DAF50(void); void sub_00BF8120(void); void sub_00B55380(void); void sub_00F043E0(void);\n'
        'void sub_00F042C0(void); void sub_00F04350(void); void sub_00F04470(void); void sub_00EFB030(void); void sub_00EFB0C0(void);\n' + "\n".join(bodies))
    binary = OUT / "test-render-draw-native"
    command = [str(clang), "--target=aarch64-linux-android29", "-O2", "-fno-strict-aliasing", "-fwrapv",
        "-ffp-contract=off", "-fno-math-errno", "-DRECOMP_X87_WINDOW", "-Iandroid/runtime", "-Iandroid/host",
        str(context), str(oracle), str(ROOT / "tests/test_render_draw_native.c"), "-o", str(binary), "-lm"]
    built = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    (OUT / "build.log").write_text(built.stdout + built.stderr)
    assert built.returncode == 0, built.stdout + built.stderr
    print(binary)
    if args.build_only:
        return
    adb = [str(sdk / "platform-tools/adb.exe"), "-s", args.serial]
    remote = "/data/local/tmp/ut3-test-render-draw-native"
    try:
        subprocess.run([*adb, "push", str(binary), remote], check=True, capture_output=True)
        subprocess.run([*adb, "shell", "chmod", "755", remote], check=True, capture_output=True)
        ran = subprocess.run([*adb, "shell", remote], capture_output=True, text=True, timeout=60)
        (OUT / "result.txt").write_text(ran.stdout + ran.stderr)
        print(ran.stdout + ran.stderr, end="")
        assert ran.returncode == 0 and "PASS native draw-list:" in ran.stdout
    finally:
        subprocess.run([*adb, "shell", "rm", "-f", remote], capture_output=True)


if __name__ == "__main__":
    main()
