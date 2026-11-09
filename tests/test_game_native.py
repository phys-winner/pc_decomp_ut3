"""Compare native game helpers with freshly extracted lifted originals on ARM64."""
import argparse
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', default=os.environ.get('UT3_TEST_ANDROID'))
    parser.add_argument('--build-only', action='store_true')
    args = parser.parse_args()
    if not args.build_only and not args.serial:
        parser.error('--serial is required to run the device fixture')
    sdk = Path(os.environ.get('ANDROID_SDK_ROOT', Path.home() / 'AppData/Local/Android/Sdk'))
    ndk = Path(os.environ.get('ANDROID_NDK_HOME', sdk / 'ndk/27.2.12479018'))
    clang = ndk / 'toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe'
    out = ROOT / 'build/game-native-tests'
    out.mkdir(parents=True, exist_ok=True)
    core = (ROOT / 'android/host/core.c').read_text(encoding='utf-8')
    start = core.index('VOID_HIDDEN uint32_t g_eax')
    end = core.index('/* --------------------------------------------------------------- logging */')
    (out / 'context.c').write_text('#include "host.h"\n' + core[start:end], encoding='utf-8')
    owners = []
    for address, unit in [('006044F0', '0072'), ('00BF8120', '0286'), ('0088B010', '0179')]:
        text = (ROOT / f'generated/ut3/functions_{unit}.c').read_text(encoding='utf-8')
        match = re.search(r'void sub_' + address + r'\(void\) \{.*?(?=\nvoid sub_|\Z)', text, re.S)
        assert match, f'Missing original {address}'
        owners.append(match.group().replace('void sub_' + address, 'void __real_sub_' + address, 1))
    (out / 'originals.c').write_text('#define RECOMP_GENERATED_CODE\n#include "host.h"\n' + '\n'.join(owners), encoding='utf-8')
    binary = out / 'test-game-native'
    command = [str(clang), '--target=aarch64-linux-android29', '-O2', '-fno-strict-aliasing', '-fwrapv',
               '-ffp-contract=off', '-fno-math-errno', '-DRECOMP_X87_WINDOW', '-Iandroid/runtime', '-Iandroid/host',
               str(ROOT / 'tests/test_game_native.c'), str(out / 'originals.c'), str(out / 'context.c'),
               '-o', str(binary), '-lm']
    subprocess.run(command, cwd=ROOT, check=True)
    print(binary)
    if args.build_only:
        return
    adb = [str(sdk / 'platform-tools/adb.exe'), '-s', args.serial]
    remote = '/data/local/tmp/ut3-test-game-native'
    try:
        subprocess.run([*adb, 'push', str(binary), remote], check=True, capture_output=True)
        subprocess.run([*adb, 'shell', 'chmod', '755', remote], check=True)
        result = subprocess.run([*adb, 'shell', remote], capture_output=True, text=True, timeout=60)
        (out / 'result.txt').write_text(result.stdout + result.stderr, encoding='utf-8')
        print(result.stdout + result.stderr, end='')
        assert result.returncode == 0 and 'PASS ' in result.stdout
    finally:
        subprocess.run([*adb, 'shell', 'rm', '-f', remote], check=False, capture_output=True)


if __name__ == '__main__':
    main()
