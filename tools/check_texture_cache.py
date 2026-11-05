"""Build and execute the actual GLES cache/mod-write test on an explicitly selected Android device."""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess

ROOT=Path(__file__).resolve().parents[1]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device',required=True)
    parser.add_argument('--game',required=True,help='Copied test fixture with a converted UTGame/port-textures.tvt')
    args=parser.parse_args()
    sdk=Path(os.environ.get('ANDROID_SDK_ROOT',Path.home()/'AppData/Local/Android/Sdk'))
    clang=sdk/'ndk/27.2.12479018/toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe'
    out=ROOT/'build/texconv';binary=out/'cache-gpu-check'
    sources=['tests/test_texture_cache_gpu.c','android/host/d3d9_resource.c','android/host/d3d9_format.c',
             'android/host/d3d9_texel.c','android/host/texture_cache.c','android/host/d3d9_thread.c']
    subprocess.run([str(clang),'--target=aarch64-linux-android29','-O2','-ffunction-sections','-fdata-sections',
        '-Iandroid/runtime','-Iandroid/host','-Igenerated','-Ireference/mojoshader',*sources,'-Wl,--gc-sections',
        '-lEGL','-lGLESv3','-lm','-o',str(binary)],cwd=ROOT,check=True)
    adb=[str(sdk/'platform-tools/adb.exe'),'-s',args.device]
    run=lambda *cmd: subprocess.run([*adb,*cmd],check=True,capture_output=True,text=True)
    run('push',str(binary),'/data/local/tmp/ut3-cache-gpu-check')
    run('shell','chmod','755','/data/local/tmp/ut3-cache-gpu-check')
    # VOID_RENDER_THREAD=0: the resource code's GL records run inline in the test's own context.
    good=run('shell','VOID_RENDER_THREAD=0','/data/local/tmp/ut3-cache-gpu-check',args.game).stdout
    print(good)
    local=out/'test-cache.tvt';run('pull',args.game+'/UTGame/port-textures.tvt',str(local))
    data=bytearray(local.read_bytes());index=struct.unpack_from('<Q',data,16)[0]
    struct.pack_into('<Q',data,index+8,0xFFFFFFFFFFFFFFFF)
    malformed=out/'malformed/UTGame';malformed.mkdir(parents=True,exist_ok=True)
    (malformed/'port-textures.tvt').write_bytes(data)
    run('push',str(malformed.parent),'/data/local/tmp/ut3-malformed-cache')
    bad=run('shell','VOID_RENDER_THREAD=0','/data/local/tmp/ut3-cache-gpu-check','/data/local/tmp/ut3-malformed-cache','invalid').stdout
    print(bad)
    (out/'gpu-test.json').write_text(json.dumps({'device':args.device,'game':args.game,'sources':sources,
        'result':'PASS','valid':good,'malformed':bad},indent=2)+'\n')


if __name__=='__main__':
    main()
