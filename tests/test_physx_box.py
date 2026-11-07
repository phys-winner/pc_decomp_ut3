"""fluid_box against the general convex test (tests/test_physx_box.c) on an ARM64 device.

Built with the NDK clang and the floating-point flags of tools/build_android.py, pushed to
/data/local/tmp and run there.

    UT3_TEST_ANDROID=SERIAL python tests/test_physx_box.py [CASES]
"""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SDK = Path(os.environ.get('ANDROID_SDK_ROOT', Path.home() / 'AppData/Local/Android/Sdk'))
NDK = Path(os.environ.get('ANDROID_NDK_HOME', SDK / 'ndk/27.2.12479018'))
# The production flags that decide floating-point results (tools/build_android.py, `common`).
FLAGS = ['--target=aarch64-linux-android29', '-O2', '-fno-strict-aliasing', '-fwrapv', '-ffp-contract=off',
         '-fno-math-errno']


def main():
    cases = sys.argv[1] if len(sys.argv) > 1 else '20000000'
    clang = NDK / 'toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe'
    adb = [str(SDK / 'platform-tools/adb.exe'), '-s', os.environ['UT3_TEST_ANDROID']]
    with tempfile.TemporaryDirectory() as folder:
        binary = Path(folder) / 'test-physx-box'
        subprocess.run([str(clang), *FLAGS, str(ROOT / 'tests/test_physx_box.c'), '-o', str(binary), '-lm'],
                       check=True)
        remote = '/data/local/tmp/ut3-test-physx-box'
        subprocess.run([*adb, 'push', str(binary), remote], check=True, capture_output=True)
        subprocess.run([*adb, 'shell', 'chmod', '755', remote], check=True)
        result = subprocess.run([*adb, 'shell', remote, cases], capture_output=True, text=True)
        subprocess.run([*adb, 'shell', 'rm', '-f', remote], check=False)
    print(result.stdout, end='')
    if result.returncode or 'PASS' not in result.stdout:
        sys.exit(1)


if __name__ == '__main__':
    main()
