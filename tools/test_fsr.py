"""Run real FSR3 interpolation/readback on an explicitly selected Turnip Android device."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', required=True)
    args = parser.parse_args()
    sdk = Path(os.environ.get('ANDROID_SDK_ROOT', Path.home()/'AppData/Local/Android/Sdk'))
    compiler = sdk/'ndk/27.2.12479018/toolchains/llvm/prebuilt/windows-x86_64/bin/clang++.exe'
    subprocess.run([sys.executable, '-B', str(ROOT/'tools/build_fsr.py')], check=True)
    out = ROOT/'build/tests/fsr3'
    out.mkdir(parents=True, exist_ok=True)
    binary = out/'test-fsr3'
    source = ROOT/'tests/test_fsr3_gpu.cpp'
    flags = ['--target=aarch64-linux-android31', '-std=c++17', '-O1', '-g',
             '-Wno-extern-c-compat', '-Wl,-z,max-page-size=16384']
    for directory in ['reference/dxvk/include/native/windows', 'reference/dxvk/include/native/directx',
                      'reference/dxvk/include/vulkan/include']:
        flags += ['-I'+str(ROOT/directory)]
    subprocess.run([str(compiler), *flags, str(source), '-lvulkan', '-landroid', '-llog', '-ldl',
                    '-o', str(binary)], check=True)
    files = [binary, ROOT/'build/fsr/libut3_fsr3.so', ROOT/'reference/dxvk-runtime/libdxvk_d3d9.so',
             ROOT/'build/dxvk/libc++_shared.so', *(ROOT/'build/turnip-runtime/runtime').glob('*.so')]
    if len(files) < 5 or any(not path.is_file() for path in files):
        raise FileNotFoundError('Build the Android native dependencies before running the GPU test')
    pins = {p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in [source, *files]}
    adb = [str(sdk/'platform-tools/adb.exe'), '-s', args.serial]
    remote = '/data/local/tmp/ut3-fsr3-test'
    subprocess.run([*adb, 'shell', 'mkdir', '-p', remote], check=True)
    for path in files:
        subprocess.run([*adb, 'push', str(path), remote+'/'+path.name], check=True)
    subprocess.run([*adb, 'shell', 'chmod', '755', remote+'/test-fsr3'], check=True)
    result = subprocess.run([*adb, 'shell', 'cd '+remote+' && LD_LIBRARY_PATH=. UT3_TURNIP_DIR='+remote+' ./test-fsr3'],
                            capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=120)
    log = result.stdout+result.stderr
    (out/'device.log').write_text(log, encoding='utf-8')
    report = {'serial': args.serial, 'returncode': result.returncode, 'inputs': pins,
              'remote_directory': remote, 'result': 'PASS' if result.returncode == 0 and 'PASS 9 actual' in log else 'FAIL'}
    (out/'receipt.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(log)
    if report['result'] != 'PASS':
        raise SystemExit('FSR3 GPU fixture failed: '+str(out/'device.log'))


if __name__ == '__main__':
    main()
