"""Build vkd3d-shader (Wine's HLSL compiler, LGPL-2.1+) for the port: on-device D3DXCompileShader.

The vendored subset of the vkd3d 2.1 release lives in reference/vkd3d (sources, parsers generated
with bison 3.8.2 / flex 2.6.4, a hand-written config.h and a stub for the SPIR-V backend the port
does not use). dependencies-vkd3d.json pins every file; --pin rewrites those pins after an update.

  python tools/build_vkd3d.py [--pin]      -> build/vkd3d/libvkd3d_shader.a + receipt.json
"""
import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SDK = Path(os.environ.get('ANDROID_SDK_ROOT', Path.home() / 'AppData/Local/Android/Sdk'))
LLVM = SDK / 'ndk/27.2.12479018/toolchains/llvm/prebuilt/windows-x86_64/bin'
VKD3D = ROOT / 'reference/vkd3d'
MANIFEST = ROOT / 'dependencies-vkd3d.json'
OUT = ROOT / 'build/vkd3d'
SOURCES = [
    'libs/vkd3d-common/debug.c', 'libs/vkd3d-common/error.c',
    'libs/vkd3d-common/memory.c', 'libs/vkd3d-common/utf8.c',
    'libs/vkd3d-shader/checksum.c', 'libs/vkd3d-shader/d3d_asm.c', 'libs/vkd3d-shader/d3dbc.c',
    'libs/vkd3d-shader/dxbc.c', 'libs/vkd3d-shader/dxil.c', 'libs/vkd3d-shader/fx.c',
    'libs/vkd3d-shader/glsl.c', 'libs/vkd3d-shader/hlsl.c', 'libs/vkd3d-shader/hlsl_codegen.c',
    'libs/vkd3d-shader/hlsl_constant_ops.c', 'libs/vkd3d-shader/ir.c', 'libs/vkd3d-shader/msl.c',
    'libs/vkd3d-shader/tpf.c', 'libs/vkd3d-shader/vkd3d_shader_main.c',
    'generated/hlsl.tab.c', 'generated/hlsl.yy.c', 'generated/preproc.tab.c', 'generated/preproc.yy.c',
    'port/spirv_stub.c',
]
FLAGS = ['--target=aarch64-linux-android29', '-O2', '-g0', '-fPIC', '-std=gnu11', '-fvisibility=hidden',
         '-ffunction-sections', '-fdata-sections', '-w',
         '-DLIBVKD3D_SHADER_SOURCE', '-DVKD3D_DEBUG_ENV=VKD3D_SHADER_DEBUG', '-DHAVE_CONFIG_H',
         '-include', 'config.h']
INCLUDES = ['port', 'include', 'include/private', 'libs/vkd3d-shader', 'generated']


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def pinned_files():
    return sorted(p for p in VKD3D.rglob('*') if p.is_file())


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--pin', action='store_true', help='rewrite the file pins in dependencies-vkd3d.json')
    parser.add_argument('--jobs', type=int, default=8)
    args = parser.parse_args()
    manifest = json.loads(MANIFEST.read_text(encoding='utf-8'))
    files = {p.relative_to(ROOT).as_posix(): sha(p) for p in pinned_files()}
    if args.pin:
        manifest['files'] = files
        MANIFEST.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8', newline='\n')
    elif files != manifest['files']:
        changed = sorted(set(files.items()) ^ set(manifest['files'].items()))
        raise SystemExit(f'reference/vkd3d differs from dependencies-vkd3d.json: {changed[:6]}')
    clang = LLVM / 'clang.exe'
    obj = OUT / 'obj'
    obj.mkdir(parents=True, exist_ok=True)
    compiler = sha(clang)
    base = hashlib.sha256((compiler + json.dumps(FLAGS) + json.dumps(files, sort_keys=True)).encode()).hexdigest()

    def build(name):
        target = obj / (name.replace('/', '_') + '.o')
        stamp = target.with_suffix('.stamp')
        if target.exists() and stamp.exists() and stamp.read_text() == base:
            return target, True
        command = [str(clang), *FLAGS, *[f'-I{VKD3D / i}' for i in INCLUDES], '-c', str(VKD3D / name), '-o', str(target)]
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode:
            raise SystemExit(f'{name}:\n{result.stderr[-4000:]}')
        stamp.write_text(base)
        return target, False

    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        objects = list(pool.map(build, SOURCES))
    library = OUT / 'libvkd3d_shader.a'
    if library.exists():
        library.unlink()
    subprocess.run([str(LLVM / 'llvm-ar.exe'), 'rcs', str(library), *[str(o) for o, _ in objects]], check=True)
    receipt = {'library': library.relative_to(ROOT).as_posix(), 'sha256': sha(library), 'version': manifest['version'],
               'tool_sha256': sha(__file__), 'manifest_sha256': sha(MANIFEST),
               'headers': {p.relative_to(ROOT).as_posix(): sha(p) for p in sorted((VKD3D / 'include').glob('*.h'))},
               'licenses': ['reference/vkd3d/COPYING', 'reference/vkd3d/AUTHORS'],
               'compiler_sha256': compiler, 'flags': FLAGS, 'sources': SOURCES, 'license': manifest['license'],
               'cached_objects': sum(cached for _, cached in objects)}
    (OUT / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8', newline='\n')
    print('vkd3d-shader', library.stat().st_size, 'bytes', receipt['sha256'])


if __name__ == '__main__':
    sys.exit(main())
