"""Check the device's HLSL compiler (android/host/hlsl_vkd3d.c, vkd3d-shader) on a phone.

Builds tests/test_vkd3d_requests.c for Android, pushes it with a folder of recorded
D3DXCompileShader requests (<data>/shadercache/requests/*.req, "UT3SHADERREQ 2") and the
bytecode d3dx9_35.dll made from them on Windows (tools/compile_shader_requests.py), runs it in
parallel shards and sums up: how many compile, how many MojoShader parses, how many constant
tables match d3dx9's, and how long a compile takes. The requests are made from the game's shader
sources, so they stay out of the repository like the shaders themselves.

  python tools/test_vkd3d.py --serial SERIAL --requests DIR --reference DIR [--shards 8] [--build-only]
"""
import argparse
import os
from pathlib import Path
import re
import statistics
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SDK = Path(os.environ.get('ANDROID_SDK_ROOT', Path.home() / 'AppData/Local/Android/Sdk'))
CLANG = SDK / 'ndk/27.2.12479018/toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe'
OUT = ROOT / 'build/vkd3d/test/test_vkd3d_requests'
REMOTE = '/data/local/tmp/ut3-vkd3d'
MOJO = ['mojoshader.c', 'mojoshader_common.c', 'mojoshader_assembler.c', 'mojoshader_preprocessor.c',
        'mojoshader_lexer.c', 'profiles/mojoshader_profile_common.c', 'profiles/mojoshader_profile_glsl.c',
        'profiles/mojoshader_profile_d3d.c', 'profiles/mojoshader_profile_bytecode.c']
MOJO_FLAGS = ['-DMOJOSHADER_FLIP_RENDERTARGET', '-DMOJOSHADER_DEPTH_CLIPPING', '-DSUPPORT_PROFILE_HLSL=0',
              '-DSUPPORT_PROFILE_ARB1=0', '-DSUPPORT_PROFILE_ARB1_NV=0', '-DSUPPORT_PROFILE_METAL=0',
              '-DSUPPORT_PROFILE_SPIRV=0', '-DSUPPORT_PROFILE_GLSPIRV=0', '-DSUPPORT_PROFILE_GLSL120=0']
SUMMARY = re.compile(r'SUMMARY total (\d+) compiled (\d+) failed (\d+) mojoshader_ok (\d+) ctab_same (\d+) '
                     r'ctab_differs (\d+) skipped (\d+) ctab_risky (\d+) disasm_failed (\d+)')


def build():
    subprocess.run([sys.executable, str(ROOT / 'tools/build_vkd3d.py')], cwd=ROOT, check=True)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    command = [str(CLANG), '--target=aarch64-linux-android29', '-O2', '-w', '-std=gnu11', '-Iandroid/runtime',
               '-Ireference/mojoshader', '-Ireference/vkd3d/include', *MOJO_FLAGS, 'tests/test_vkd3d_requests.c',
               *[f'reference/mojoshader/{name}' for name in MOJO], 'build/vkd3d/libvkd3d_shader.a', '-lm',
               '-Wl,-z,max-page-size=16384', '-o', str(OUT)]
    subprocess.run(command, cwd=ROOT, check=True)


def adb(serial, *args, check=True, capture=True):
    env = dict(os.environ, MSYS_NO_PATHCONV='1')
    result = subprocess.run(['adb', '-s', serial, *args], capture_output=capture, text=True, env=env)
    if check and result.returncode:
        raise SystemExit(f'adb {" ".join(args)}: {result.stderr.strip() or result.stdout.strip()}')
    return result.stdout if capture else ''


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--serial')
    parser.add_argument('--requests', type=Path)
    parser.add_argument('--reference', type=Path)
    parser.add_argument('--shards', type=int, default=8)
    parser.add_argument('--verbose', type=int, default=4, help='failures and table differences printed per shard')
    parser.add_argument('--build-only', action='store_true')
    parser.add_argument('--no-push', action='store_true', help='reuse the requests already on the phone')
    parser.add_argument('--times', type=Path, help='write "key profile ms result bytes" lines here')
    args = parser.parse_args()
    build()
    if args.build_only:
        return 0
    if not args.serial or not args.requests:
        parser.error('--serial and --requests are needed to run')
    adb(args.serial, 'shell', f'mkdir -p {REMOTE}/req {REMOTE}/ref && rm -f {REMOTE}/times-*.txt')
    adb(args.serial, 'push', str(OUT), f'{REMOTE}/')
    if not args.no_push:
        adb(args.serial, 'shell', f'rm -rf {REMOTE}/req {REMOTE}/ref && mkdir -p {REMOTE}/req {REMOTE}/ref')
        adb(args.serial, 'push', str(args.requests) + '/.', f'{REMOTE}/req/')
        if args.reference:
            adb(args.serial, 'push', str(args.reference) + '/.', f'{REMOTE}/ref/')
    runs = ' & '.join(f'./test_vkd3d_requests req ref {args.verbose} {i} {args.shards} times-{i}.txt > out-{i}.txt 2>&1'
                      for i in range(args.shards))
    adb(args.serial, 'shell', f'cd {REMOTE} && chmod 755 test_vkd3d_requests && ({runs}; wait)', capture=False)
    totals = [0] * 9
    for i in range(args.shards):
        text = adb(args.serial, 'shell', f'cat {REMOTE}/out-{i}.txt')
        for line in text.splitlines():
            match = SUMMARY.search(line)
            if match:
                totals = [a + int(b) for a, b in zip(totals, match.groups())]
            elif line.strip():
                print(line[:900])
    rows = []
    for i in range(args.shards):
        for line in adb(args.serial, 'shell', f'cat {REMOTE}/times-{i}.txt').splitlines():
            parts = line.split()
            if len(parts) == 5:
                rows.append((parts[0], parts[1], float(parts[2]), parts[3], int(parts[4])))
    if args.times:
        args.times.write_text(''.join(f'{k} {p} {ms:.1f} {r} {b}\n' for k, p, ms, r, b in rows), encoding='utf-8')
    names = ['total', 'compiled', 'failed', 'mojoshader_ok', 'ctab_same', 'ctab_differs', 'skipped', 'ctab_risky', 'disasm_failed']
    print(' '.join(f'{n} {v}' for n, v in zip(names, totals)))
    if rows:
        times = sorted(ms for _, _, ms, _, _ in rows)
        print(f'compile ms: median {statistics.median(times):.0f}, p90 {times[int(len(times) * 0.9)]:.0f}, '
              f'max {times[-1]:.0f}, sum {sum(times) / 1000:.0f} s over {len(times)} shaders (parallel shards)')
        for key, profile, ms, result, size in sorted(rows, key=lambda r: -r[2])[:8]:
            print(f'  slow {key} {profile} {ms:.0f} ms {result} {size} bytes')
    return 1 if totals[2] else 0


if __name__ == '__main__':
    sys.exit(main())
