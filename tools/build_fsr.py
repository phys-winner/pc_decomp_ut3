"""Build the optional API-31 FSR3 Vulkan plugin from pinned ARM64 SDK libraries."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess

ROOT=Path(__file__).resolve().parents[1]
SDK=Path(os.environ.get('ANDROID_SDK_ROOT',Path.home()/'AppData/Local/Android/Sdk'))

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--shader-compiler',type=Path,help='Regenerate motion_spirv.inc using this glslangValidator')
    args=parser.parse_args()
    manifest=ROOT/'dependencies-fsr.json';spec=json.loads(manifest.read_text(encoding='utf-8'))
    for name,digest in spec['files'].items():
        if sha(ROOT/name)!=digest:raise ValueError('FSR input hash mismatch: '+name)
    out=ROOT/'build/fsr';out.mkdir(parents=True,exist_ok=True)
    shader=ROOT/'android/postfx/motion.comp';generated=ROOT/'android/postfx/motion_spirv.inc'
    marker='/* motion.comp SHA256 '+sha(shader)+' */\n'
    if args.shader_compiler:
        binary=out/'motion.spv'
        subprocess.run([str(args.shader_compiler),'-V','--target-env','vulkan1.2',str(shader),'-o',str(binary)],check=True)
        data=binary.read_bytes();words=struct.unpack('<'+'I'*(len(data)//4),data)
        generated.write_text(marker+'static const uint32_t motion_spirv[]={\n'+',\n'.join('  '+','.join(f'0x{w:08x}u' for w in words[i:i+8]) for i in range(0,len(words),8))+'\n};\n',encoding='utf-8')
    if not generated.read_text(encoding='utf-8').startswith(marker):raise ValueError('motion.comp changed: regenerate with --shader-compiler glslangValidator.exe')
    llvm=SDK/'ndk/27.2.12479018/toolchains/llvm/prebuilt/windows-x86_64/bin'
    compiler=llvm/'clang++.exe'
    inputs={**spec['files']}
    for path in [manifest,Path(__file__),ROOT/'android/postfx/fsr3.cpp',ROOT/'android/dxvk/vulkan_interop.inc',shader,generated]:
        inputs[path.relative_to(ROOT).as_posix()]=sha(path)
    for path in (ROOT/'reference/dxvk/include').rglob('*.h'):inputs[path.relative_to(ROOT).as_posix()]=sha(path)
    compiler_sha=sha(compiler)
    fingerprint=hashlib.sha256(json.dumps({'inputs':inputs,'compiler':compiler_sha},sort_keys=True).encode()).hexdigest()
    library=out/'libut3_fsr3.so';receipt=out/'receipt.json'
    if library.is_file() and receipt.is_file():
        old=json.loads(receipt.read_text(encoding='utf-8'))
        if old.get('fingerprint')==fingerprint and old.get('sha256')==sha(library):return
    flags=['--target=aarch64-linux-android31','-std=c++17','-O2','-g0','-fPIC','-shared','-fvisibility=hidden','-Wno-extern-c-compat','-Wl,-z,max-page-size=16384']
    for directory in ['reference/dxvk/include/native/windows','reference/dxvk/include/native/directx','reference/dxvk/include/vulkan/include','reference/fsr3-runtime/include']:
        flags+=['-I'+str(ROOT/directory)]
    libraries=[ROOT/'reference/fsr3-runtime/lib'/('libffx_'+name+'_android_arm64.a') for name in ['frameinterpolation','opticalflow','backend_vk']]
    subprocess.run([str(compiler),*flags,str(ROOT/'android/postfx/fsr3.cpp'),*map(str,libraries),'-lvulkan','-llog','-ldl','-o',str(library)],cwd=ROOT,check=True)
    result={'fingerprint':fingerprint,'sha256':sha(library),'path':library.relative_to(ROOT).as_posix(),
        'inputs':inputs,'compiler_sha256':compiler_sha,'licenses':spec['licenses'],'min_sdk':31,'whole_game':'NOT YET VERIFIED'}
    receipt.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print('FSR3 plugin',library.stat().st_size,'bytes',result['sha256'])

if __name__=='__main__':main()
