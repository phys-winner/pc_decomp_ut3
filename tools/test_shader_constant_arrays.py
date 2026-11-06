"""Check literal SM3 arrays and the original terrain shader on an explicitly selected GLES device."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

from build_postfx_shaders import compile_shader

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--objects', type=Path, required=True)
    args = parser.parse_args()
    out = ROOT/'build/tests/shader-constant-arrays'
    out.mkdir(parents=True, exist_ok=True)
    source = ROOT/'tests/test_shader_constant_arrays.hlsl'
    files = [ROOT/'build/native-audio/runtime/libc++_shared.so']
    for entry, profile in [('vertex', 'vs_3_0'), ('pixel', 'ps_3_0'), ('flat_pixel', 'ps_3_0')]:
        target = out/(entry+'.bin')
        target.write_bytes(compile_shader(source.read_text(encoding='utf-8'), entry, profile))
        files.append(target)
    game = Path(json.loads((ROOT/'config/inputs.json').read_text())['source_root'])
    cache = game/'UTGame/CookedPC/RefShaderCache-PC-D3D-SM3.upk'
    with cache.open('rb') as stream:
        stream.seek(71859)
        original = stream.read(2032)
    expected = '723b7e02177d0aafcfe88faf0de1a717c98611dd94d9f0f464cc900ae33ea3ff'
    assert hashlib.sha256(original).hexdigest() == expected, 'Original retail terrain shader differs'
    retail = out/'terrain-original.bin'
    retail.write_bytes(original)
    files.append(retail)
    binary = out/'test'
    subprocess.run([sys.executable, '-B', str(ROOT/'tools/build_host_test.py'),
                    str(ROOT/'tests/test_shader_constant_arrays.c'), '--objects', str(args.objects.resolve()),
                    '--exclude', 'android_main', '--output', str(binary)], cwd=ROOT, check=True)
    adb = ['adb', '-s', args.serial]
    remote = '/data/local/tmp/ut3-terrain-test'
    subprocess.run([*adb, 'shell', 'mkdir', '-p', remote], check=True)
    for path in [binary, *files]:
        subprocess.run([*adb, 'push', str(path), remote+'/'+path.name], check=True)
    subprocess.run([*adb, 'shell', 'chmod', '755', remote+'/test'], check=True)
    result = subprocess.run([*adb, 'shell', 'LD_LIBRARY_PATH='+remote, remote+'/test', remote+'/vertex.bin', remote+'/pixel.bin',
                             remote+'/terrain-original.bin', remote+'/flat_pixel.bin'],
                            capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=120)
    log = result.stdout+result.stderr
    (out/'device.log').write_text(log, encoding='utf-8')
    passed = result.returncode == 0 and 'PASS 180 GPU' in log and 'PASS original retail terrain' in log
    inputs = [source, ROOT/'tests/test_shader_constant_arrays.c', ROOT/'android/host/d3d9_shader.c', binary, *files]
    receipt = {'serial': args.serial, 'returncode': result.returncode, 'result': 'PASS' if passed else 'FAIL',
               'inputs': {p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}}
    (out/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n', encoding='utf-8')
    print(log)
    if not passed:
        raise SystemExit('GPU literal array test failed: '+str(out/'device.log'))


if __name__ == '__main__':
    main()
