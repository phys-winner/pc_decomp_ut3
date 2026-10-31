"""Generate native Shell32 vectors and run the production guest export on Android."""
import ctypes
import itertools
import os
from pathlib import Path
import random
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def native(command):
    shell = ctypes.WinDLL("shell32", use_last_error=True)
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    shell.CommandLineToArgvW.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_int)]
    shell.CommandLineToArgvW.restype = ctypes.POINTER(ctypes.c_wchar_p)
    kernel.LocalFree.argtypes = [ctypes.c_void_p]
    kernel.LocalFree.restype = ctypes.c_void_p
    count = ctypes.c_int()
    result = shell.CommandLineToArgvW(command, ctypes.byref(count))
    if not result:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        return [result[i] for i in range(count.value)]
    finally:
        kernel.LocalFree(result)


def main():
    output = ROOT / "build/argv-test"
    output.mkdir(parents=True, exist_ok=True)
    commands = [' ', '\t', ' a b', '"C:\\Games\\UT3\\Binaries\\UT3.exe" -dx9',
                'p "a""b" c', '"a"b c', 'a"b c" d', 'p a\\\\\\"b c d',
                'p a\\\\\\\\"b c" d e', 'p \U0001F600 "\U0001F642 x"',
                'p \ud800 \udc00 "\ud800 x"', 'p a\nb', 'p\na b', 'p \na b',
                '"p"\na b', 'p \u00a0 \u2003', 'p a\0ignored']
    for length in range(1, 7):
        commands.extend(map(''.join, itertools.product('a \t\\"', repeat=length)))
    randomizer = random.Random(2468)
    for _ in range(5000):
        commands.append(''.join(randomizer.choice('ab \t\\"\r\n')
                                for _ in range(randomizer.randrange(1, 60))))
    vectors = [(command, native(command)) for command in commands]
    # The empty-input contract is the port's current executable, independent of the test runner.
    vectors.append(('', [r'C:\Games\UT3\Binaries\UT3.exe']))
    data = bytearray(struct.pack('<I', len(vectors)))
    for command, arguments in vectors:
        raw = command.encode('utf-16le', errors='surrogatepass')
        data += struct.pack('<I', len(raw) // 2) + raw + struct.pack('<I', len(arguments))
        for argument in arguments:
            raw = argument.encode('utf-16le', errors='surrogatepass')
            data += struct.pack('<I', len(raw) // 2) + raw
    path = output / "windows-vectors.bin"
    path.write_bytes(data)
    sdk = Path(os.environ.get("ANDROID_SDK_ROOT", Path.home() / "AppData/Local/Android/Sdk"))
    ndk = Path(os.environ.get("ANDROID_NDK_HOME", sdk / "ndk/27.2.12479018"))
    clang = ndk / "toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe"
    obj = output / "test.o"
    subprocess.run([str(clang), '--target=aarch64-linux-android29', '-O1', '-DRECOMP_X87_WINDOW',
                    '-DRECOMP_TRACE', '-Iandroid/runtime', '-Iandroid/host', '-Ibuild/host-smoke',
                    '-c', 'tests/test_argv.c', '-o', str(obj)], cwd=ROOT, check=True)
    objects = (ROOT / 'build/host-smoke/test-pe-imports.rsp').read_text().splitlines()
    objects[0] = '"' + obj.as_posix() + '"'
    response = output / 'test.rsp'
    response.write_text('\n'.join(objects) + '\n')
    binary = output / 'test-argv'
    subprocess.run([str(clang), '--target=aarch64-linux-android29', '-Wl,-z,nostart-stop-gc',
                    '@' + str(response), '-o', str(binary), '-lm', '-ldl', '-llog', '-landroid',
                    '-lEGL', '-lGLESv3', '-laaudio', '-lz'], cwd=ROOT, check=True)
    adb = [str(sdk / 'platform-tools/adb.exe'), '-s', os.environ['UT3_TEST_ANDROID']]
    remote = '/data/local/tmp/ut3-test-argv'
    subprocess.run([*adb, 'push', str(binary), remote], check=True)
    subprocess.run([*adb, 'push', str(path), remote + '.bin'], check=True)
    subprocess.run([*adb, 'shell', 'chmod', '755', remote], check=True)
    subprocess.run([*adb, 'shell', remote, remote + '.bin'], check=True)


if __name__ == '__main__':
    main()
