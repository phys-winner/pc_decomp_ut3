"""Compile native/rendered hash differential checks from current generated sources; run on a named ARM64 device."""
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
    if not args.serial and not args.build_only:
        parser.error('Use --serial DEVICE or --build-only')
    sdk = Path(os.environ.get('ANDROID_SDK_ROOT', Path.home() / 'AppData/Local/Android/Sdk'))
    ndk = Path(os.environ.get('ANDROID_NDK_HOME', sdk / 'ndk/27.2.12479018'))
    clang = ndk / 'toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe'
    out = ROOT / 'build/render-hash-test'
    out.mkdir(parents=True, exist_ok=True)
    core = (ROOT / 'android/host/core.c').read_text(encoding='utf-8')
    context = out / 'context.c'
    context.write_text('#include "host.h"\n' + core[core.index('VOID_HIDDEN uint32_t g_eax'):core.index('void void_regs_save')], encoding='utf-8')
    bodies = []
    for address, filename in [('00E57210', 'functions_0404.c'), ('01178E50', 'functions_0527.c'), ('01178EA0', 'functions_0527.c')]:
        source = (ROOT / 'generated/ut3' / filename).read_text(encoding='utf-8')
        found = re.search(r'void sub_' + address + r'\(void\) \{.*?(?=\nvoid sub_|\Z)', source, re.S)
        assert found, address
        body = found.group()
        if address != '00E57210':
            body = body.replace('void sub_' + address, 'void __real_sub_' + address, 1)
        bodies.append(body)
    original = out / 'original.c'
    original.write_text('#define RECOMP_GENERATED_CODE\n#include "host.h"\n' + '\n'.join(bodies), encoding='utf-8')
    binary = out / 'test-render-hash'
    command = [str(clang), '--target=aarch64-linux-android29', '-O2', '-fno-strict-aliasing', '-fwrapv',
               '-ffp-contract=off', '-fno-math-errno', '-DRECOMP_X87_WINDOW', '-Iandroid/runtime', '-Iandroid/host',
               str(context), str(original), str(ROOT / 'tests/test_render_hash.c'), '-o', str(binary), '-lm']
    subprocess.run(command, cwd=ROOT, check=True)
    if args.build_only:
        print(binary)
        return
    adb = [str(sdk / 'platform-tools/adb.exe'), '-s', args.serial]
    remote = '/data/local/tmp/ut3-test-render-hash'
    subprocess.run([*adb, 'push', str(binary), remote], check=True, capture_output=True)
    try:
        subprocess.run([*adb, 'shell', 'chmod', '755', remote], check=True)
        result = subprocess.run([*adb, 'shell', remote], capture_output=True, text=True, timeout=60)
        print(result.stdout, end='')
        assert result.returncode == 0 and 'PASS render hash:' in result.stdout, result.stdout + result.stderr
    finally:
        subprocess.run([*adb, 'shell', 'rm', '-f', remote], check=True)


if __name__ == '__main__':
    main()
