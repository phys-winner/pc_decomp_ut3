"""Per-site indirect-call caches (core.c) on the device, in all three site-cache modes.

Builds tests/test_site_cache.c against a build's objects (tools/build_host_test.py), runs it with the
site-cache switch absent (extensions), 0 (single entry) and 2 (statistics) and removes everything it
pushed. usage: python tests/test_site_cache.py --serial DEVICE --objects build/<task>/obj
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
REMOTE = '/data/local/tmp/ut3-site-cache-test'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', default=os.environ.get('UT3_TEST_ANDROID'))
    parser.add_argument('--objects', type=Path, required=True)
    args = parser.parse_args()
    if not args.serial:
        parser.error('--serial is required')
    sdk = Path(os.environ.get('ANDROID_SDK_ROOT', Path.home() / 'AppData/Local/Android/Sdk'))
    ndk = Path(os.environ.get('ANDROID_NDK_HOME', sdk / 'ndk/27.2.12479018'))
    out = ROOT / 'build/site-cache-test'
    binary = out / 'test_site_cache'
    subprocess.run([sys.executable, str(ROOT / 'tools/build_host_test.py'), 'tests/test_site_cache.c',
                    '--objects', str(args.objects), '--output', str(binary), '--exclude', 'android_main'],
                   cwd=ROOT, check=True)
    stripped = binary.with_suffix('.stripped')
    subprocess.run([str(ndk / 'toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-strip.exe'), '-o', str(stripped),
                    str(binary)], check=True)
    env = dict(os.environ, MSYS_NO_PATHCONV='1')
    adb = ['adb', '-s', args.serial]

    def shell(command):
        return subprocess.run(adb + ['shell', command], capture_output=True, text=True, env=env)

    shell(f'mkdir -p {REMOTE}/m0 {REMOTE}/m2 && printf 0 > {REMOTE}/m0/site-cache && printf 2 > {REMOTE}/m2/site-cache')
    try:
        subprocess.run(adb + ['push', str(stripped), REMOTE + '/test'], check=True, capture_output=True, env=env)
        for library in (ROOT / 'build/native-audio/runtime').glob('*.so'):
            subprocess.run(adb + ['push', str(library), REMOTE + '/'], check=True, capture_output=True, env=env)
        failures = 0
        for mode, folder in (('absent', '/nonexistent'), ('0', REMOTE + '/m0'), ('2', REMOTE + '/m2')):
            result = shell(f'cd {REMOTE} && chmod 755 test && LD_LIBRARY_PATH={REMOTE} SITE_TEST_DIR={folder} ./test')
            text = result.stdout + result.stderr
            ok = result.returncode == 0 and 'PASS site caches' in text
            failures += not ok
            print(f'site-cache {mode}: {"PASS" if ok else "FAIL"}')
            print('\n'.join('  ' + line for line in text.strip().splitlines()[-4:]))
    finally:
        shell(f'rm -rf {REMOTE}')
    sys.exit(1 if failures else 0)


if __name__ == '__main__':
    main()
