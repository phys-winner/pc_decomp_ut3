"""Check real UT3 resource bytes through native Windows and production Android APIs."""
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import subprocess

import pefile

ROOT = Path(__file__).resolve().parents[1]


def main():
    inputs = json.loads((ROOT / "config/inputs.json").read_text())
    spec = next(row for row in inputs["modules"] if row["path"].lower() == "binaries/ut3.exe")
    source = Path(inputs["source_root"]) / spec["path"]
    raw = source.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == spec["sha256"]
    pe = pefile.PE(data=raw, fast_load=True)
    pe.parse_data_directories(directories=[2])
    kind = next(row for row in pe.DIRECTORY_ENTRY_RESOURCE.entries if row.id == 10)
    name = next(row for row in kind.directory.entries if row.id == 1010)
    language = next(row for row in name.directory.entries if row.id == 1033)
    entry = language.data.struct
    expected = pe.get_data(entry.OffsetToData, entry.Size)
    assert len(expected) == 4728
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.LoadLibraryExW.argtypes = [ctypes.c_wchar_p, ctypes.c_void_p, wintypes.DWORD]
    kernel.LoadLibraryExW.restype = ctypes.c_void_p
    kernel.FindResourceW.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
    kernel.FindResourceW.restype = ctypes.c_void_p
    kernel.LoadResource.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    kernel.LoadResource.restype = ctypes.c_void_p
    kernel.SizeofResource.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    kernel.SizeofResource.restype = wintypes.DWORD
    kernel.LockResource.argtypes = [ctypes.c_void_p]
    kernel.LockResource.restype = ctypes.c_void_p
    kernel.FreeLibrary.argtypes = [ctypes.c_void_p]
    module = kernel.LoadLibraryExW(str(source), None, 0x22)  # Data/image resource only: never execute the game.
    assert module
    try:
        handle = kernel.FindResourceW(module, 1010, 10)
        assert handle and kernel.SizeofResource(module, handle) == 4728
        pointer = kernel.LockResource(kernel.LoadResource(module, handle))
        assert pointer and ctypes.string_at(pointer, 4728) == expected
        ctypes.set_last_error(0)
        assert not kernel.FindResourceW(module, 1010, 65535) and ctypes.get_last_error() == 1813
        ctypes.set_last_error(0)
        assert not kernel.FindResourceW(module, 65535, 10) and ctypes.get_last_error() == 1814
    finally:
        kernel.FreeLibrary(module)
    output = ROOT / "build/pe-resources-test"
    output.mkdir(parents=True, exist_ok=True)
    payload = output / "resource.bin"
    payload.write_bytes(expected)
    sdk = Path(os.environ.get("ANDROID_SDK_ROOT", Path.home() / "AppData/Local/Android/Sdk"))
    ndk = Path(os.environ.get("ANDROID_NDK_HOME", sdk / "ndk/27.2.12479018"))
    clang = ndk / "toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe"
    flags = ['--target=aarch64-linux-android29', '-O1', '-DRECOMP_X87_WINDOW', '-DRECOMP_TRACE',
             '-Iandroid/runtime', '-Iandroid/host', '-Ibuild/host-smoke']
    for path, obj in [('tests/test_pe_resources.c', 'test.o'), ('android/host/user32.c', 'user32.o')]:
        subprocess.run([str(clang), *flags, '-c', path, '-o', str(output / obj)], cwd=ROOT, check=True)
    objects = (ROOT / 'build/host-smoke/test-pe-imports.rsp').read_text().splitlines()
    objects[0] = '"' + (output / 'test.o').as_posix() + '"'
    objects = [('"' + (output / 'user32.o').as_posix() + '"') if path.endswith('/user32.o"') else path
               for path in objects]
    response = output / 'test.rsp'
    response.write_text('\n'.join(objects) + '\n')
    binary = output / 'test-pe-resources'
    subprocess.run([str(clang), '--target=aarch64-linux-android29', '-Wl,-z,nostart-stop-gc',
                    '@' + str(response), '-o', str(binary), '-lm', '-ldl', '-llog', '-landroid',
                    '-lEGL', '-lGLESv3', '-laaudio', '-lz'], cwd=ROOT, check=True)
    adb = [str(sdk / 'platform-tools/adb.exe'), '-s', os.environ['UT3_TEST_ANDROID']]
    remote = '/data/local/tmp/ut3-test-pe-resources'
    for local, target in [(binary, remote), (source, remote + '.exe'), (payload, remote + '.bin')]:
        subprocess.run([*adb, 'push', str(local), target], check=True)
    subprocess.run([*adb, 'shell', 'chmod', '755', remote], check=True)
    subprocess.run([*adb, 'shell', remote, remote + '.exe', remote + '.bin'], check=True)
    print('Resource SHA-256:', hashlib.sha256(expected).hexdigest())


if __name__ == '__main__':
    main()
