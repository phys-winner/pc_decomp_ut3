"""Build the optional UT3 UPK texture converter and its Android self-check."""
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/texconv'
SDK = Path(os.environ.get('ANDROID_SDK_ROOT', Path.home() / 'AppData/Local/Android/Sdk'))
CXX = Path(os.environ.get('ANDROID_NDK_HOME', SDK / 'ndk/27.2.12479018')) / 'toolchains/llvm/prebuilt/windows-x86_64/bin/clang++.exe'
ASTC = ['-DASTCENC_NEON=1', '-DASTCENC_SVE=0', '-DASTCENC_SSE=0', '-DASTCENC_AVX=0', '-DASTCENC_POPCNT=0', '-DASTCENC_F16C=0']
FLAGS = ['--target=aarch64-linux-android29', '-O2', '-fPIC', '-fvisibility=hidden', '-ffunction-sections', '-fdata-sections',
         '-std=c++17', '-ffile-prefix-map=' + str(ROOT) + '=.']


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    manifest = ROOT / 'dependencies-textures.json'
    pins = json.loads(manifest.read_text())['sources']
    for name, digest in pins.items():
        if hashlib.sha256((ROOT / name).read_bytes().replace(b'\r\n', b'\n')).hexdigest() != digest:
            raise ValueError('Texture codec source differs from its pin: ' + name)
    (OUT / 'obj').mkdir(parents=True, exist_ok=True)
    sources = [(p, ASTC) for p in sorted((ROOT / 'reference/astcenc/Source').glob('astcenc_*.cpp'))]
    sources += [(ROOT / 'reference/etcpak' / n, ['-DBCDEC_STATIC', '-DBCDEC_IMPLEMENTATION'])
                for n in ('ProcessRGB.cpp', 'Tables.cpp', 'Dither.cpp', 'Decode.cpp')]
    sources += [(ROOT / 'android/texconv/codec.cpp', ASTC), (ROOT / 'android/texconv/ut3tex.cpp', [])]
    sources += [(ROOT / 'android/launcher/native/zipmount.c', [])]
    headers = sorted((ROOT / 'android/texconv').glob('*.h'))
    headers += [ROOT / 'android/launcher/native/zipmount.h']
    digest = hashlib.sha256((sha(manifest) + str(CXX) + ''.join(sha(p) for p in headers)).encode()).hexdigest()

    def compile(unit):
        source, extra = unit
        compiler = CXX.with_name('clang.exe') if source.suffix == '.c' else CXX
        flags = [f for f in FLAGS if f != '-std=c++17'] + ['-std=c11'] if source.suffix == '.c' else FLAGS
        obj = OUT / 'obj' / (source.parent.name + '_' + source.stem + '.o')
        stamp = obj.with_suffix('.sha256')
        key = hashlib.sha256((digest + str(compiler) + sha(source) + json.dumps(flags + extra)).encode()).hexdigest()
        if not obj.is_file() or not stamp.is_file() or stamp.read_text() != key:
            subprocess.run([str(compiler), *flags, *extra, '-c', str(source), '-o', str(obj)], check=True)
            stamp.write_text(key)
        return obj

    with ThreadPoolExecutor(max_workers=4) as pool:
        objects = list(pool.map(compile, sources))
    lib = OUT / 'libtexconv.so'
    link = [str(CXX), FLAGS[0], '-fuse-ld=lld', '-static-libstdc++', '-Wl,--gc-sections',
            '-Wl,--no-undefined', '-Wl,-z,max-page-size=16384']
    subprocess.run([*link, '-shared', *map(str, objects), '-lz', '-llog', '-o', str(lib)], check=True)
    # The check includes the production package reader and converter, with real codec objects.
    test_objects = [p for p in objects if p.name != 'texconv_ut3tex.o']
    subprocess.run([*link, *FLAGS[1:], str(ROOT / 'tests/test_texture_packages.cpp'), *map(str, test_objects),
                    '-lz', '-llog', '-o', str(OUT / 'texture-check')], check=True)
    sources_pins = {p.relative_to(ROOT).as_posix(): sha(p) for p in headers + [s for s, _ in sources]}
    (OUT / 'receipt.json').write_text(json.dumps({'sha256': sha(lib), 'manifest_sha256': sha(manifest),
        'tool_sha256': sha(Path(__file__)), 'sources': sources_pins}, indent=2) + '\n')
    print(f'Texture converter: {lib} ({lib.stat().st_size} bytes)')


if __name__ == '__main__':
    main()
