"""Compile current production networking/thread sources and run loopback event tests on ARM64."""
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    output = ROOT / 'build/wsa-readiness-test'; output.mkdir(parents=True, exist_ok=True)
    sdk = Path(os.environ.get('ANDROID_SDK_ROOT', Path.home() / 'AppData/Local/Android/Sdk'))
    ndk = Path(os.environ.get('ANDROID_NDK_HOME', sdk / 'ndk/27.2.12479018'))
    clang = ndk / 'toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe'
    flags = ['--target=aarch64-linux-android29', '-O1', '-DRECOMP_X87_WINDOW', '-DRECOMP_TRACE',
             '-Iandroid/runtime', '-Iandroid/host', '-Ibuild/host-smoke']
    sources = [ROOT / 'tests/test_wsa_readiness.c', ROOT / 'android/host/core.c', ROOT / 'android/host/threads.c']
    own = []
    for index, source in enumerate(sources):
        obj = output / f'test-{index}.o'
        subprocess.run([str(clang), *flags, '-c', str(source), '-o', str(obj)], cwd=ROOT, check=True)
        own.append('"' + obj.as_posix() + '"')
    objects = (ROOT / 'build/host-smoke/test-pe-imports.rsp').read_text().splitlines()[1:]
    objects = [path for path in objects if not path.endswith(('/core.o"', '/threads.o"', '/network.o"', '/pe.o"'))]
    response = output / 'test.rsp'; response.write_text('\n'.join(own + objects) + '\n')
    binary = output / 'test-wsa-readiness'
    subprocess.run([str(clang), '--target=aarch64-linux-android29', '-Wl,-z,nostart-stop-gc',
                    '@' + str(response), '-o', str(binary), '-lm', '-ldl', '-llog', '-landroid',
                    '-lEGL', '-lGLESv3', '-laaudio', '-lz'], cwd=ROOT, check=True)
    adb = [str(sdk / 'platform-tools/adb.exe'), '-s', os.environ['UT3_TEST_ANDROID']]
    remote = '/data/local/tmp/ut3-test-wsa-readiness'
    subprocess.run([*adb, 'push', str(binary), remote], check=True)
    subprocess.run([*adb, 'shell', 'chmod', '755', remote], check=True)
    result = subprocess.run([*adb, 'shell', remote], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    print(result.stdout, end='')
    (ROOT / 'research/device-wsa-readiness.log').write_text(result.stdout)
    if result.returncode:
        raise RuntimeError(f'WSA readiness test failed: {result.returncode}')
    provenance = [*sources, ROOT / 'android/host/network.c']
    (ROOT / 'research/wsa-readiness-test.json').write_text(json.dumps({
        'status': 'PASS', 'device': adb[-1],
        'sources': {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest() for path in provenance},
        'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
        'log': 'research/device-wsa-readiness.log',
        'scope_limit': 'Readiness pump runs in WSA waits and WSAEnumNetworkEvents; generic Kernel32 waits are not readiness sources.',
        'supported_events': ['FD_READ', 'FD_WRITE', 'FD_ACCEPT', 'FD_CONNECT', 'FD_CLOSE'],
        'unsupported_mask_error': 10045,
        'references': ['https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-wsaeventselect',
                       'https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-wsaenumnetworkevents']}, indent=2) + '\n')


if __name__ == '__main__':
    main()
