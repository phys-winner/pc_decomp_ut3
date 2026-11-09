"""Independent lifted-oracle fixture for visible-light array reset."""
import argparse,hashlib,importlib.util,json,os,re,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'build/render-light-native-tests'
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--host',action='store_true');p.add_argument('--build-only',action='store_true')
    p.add_argument('--source',type=Path);p.add_argument('--serial',default=os.environ.get('UT3_TEST_ANDROID'));args=p.parse_args()
    if not(args.host or args.build_only or args.serial):p.error('--host, --build-only or --serial required')
    source=(args.source or ROOT/'android/host/render_light_native.c').resolve();OUT.mkdir(parents=True,exist_ok=True)
    (OUT/'render_light_native_under_test.c').write_bytes(source.read_bytes())
    core=(ROOT/'android/host/core.c').read_text();context=OUT/'context.c'
    context.write_text('#include "host.h"\n'+core[core.index('VOID_HIDDEN uint32_t g_eax'):core.index('/* --------------------------------------------------------------- logging */')])
    generated=(ROOT/'generated/ut3/functions_0427.c').read_text()
    body=re.search(r'void sub_00EFF830\(void\) \{.*?(?=\nvoid sub_|\Z)',generated,re.S)[0]
    sys.path.insert(0,str(ROOT/'tools'));from x87_slots import transform_file
    assert transform_file(body,'ut3')[0]==body
    oracle=OUT/'oracle.c';oracle.write_text('#define RECOMP_GENERATED_CODE\n#include "recomp_types.h"\nvoid sub_01288F71(void);void sub_004055A0(void);\n'+body.replace('void sub_00EFF830','void __real_sub_00EFF830',1))
    sources=[context,oracle,ROOT/'tests/test_render_light_native.c']
    if args.host:
        spec=importlib.util.spec_from_file_location('visibility_fixture',ROOT/'tests/test_render_visibility_native.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);m.OUT=OUT
        commands,binary,env=m.host_build(sources);(OUT/'host/render_light_native_under_test.c').write_bytes(source.read_bytes())
    else:
        sdk=Path(os.environ.get('ANDROID_SDK_ROOT',Path.home()/'AppData/Local/Android/Sdk'));ndk=Path(os.environ.get('ANDROID_NDK_HOME',sdk/'ndk/27.2.12479018'))
        clang=ndk/'toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe';binary=OUT/'test-render-light-native';env=None
        commands=[[str(clang),'--target=aarch64-linux-android29','-O2','-fno-strict-aliasing','-fwrapv','-ffp-contract=off','-fno-math-errno','-DRECOMP_X87_WINDOW','-Iandroid/runtime','-Iandroid/host','-I'+str(OUT),*map(str,sources),'-o',str(binary),'-lm']]
    log=[]
    for command in commands:
        r=subprocess.run(command,cwd=ROOT,env=env,capture_output=True,text=True);log.append(r.stdout+r.stderr);(binary.parent/'build.log').write_text('\n'.join(log));assert r.returncode==0,log[-1]
    files=[*sources,source,ROOT/'android/runtime/recomp_types.h',ROOT/'android/host/core.c',ROOT/'android/host/host.h',ROOT/'tools/x87_slots.py',Path(__file__),binary]
    receipt={'status':'BUILT','commands':commands,'oracle':'Unmodified original EFF830; destructor/allocator callbacks are deterministic mocks with state and memory mutations.','sha256':{str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest() for f in files}}
    if not args.build_only:
        if args.host:r=subprocess.run([str(binary)],capture_output=True,text=True,timeout=120)
        else:
            adb=[str(sdk/'platform-tools/adb.exe'),'-s',args.serial];remote='/data/local/tmp/ut3-test-render-light-native'
            subprocess.run([*adb,'push',str(binary),remote],check=True,capture_output=True)
            try:
                subprocess.run([*adb,'shell','chmod','755',remote],check=True,capture_output=True)
                r=subprocess.run([*adb,'shell',remote],capture_output=True,text=True,timeout=120)
            finally:subprocess.run([*adb,'shell','rm','-f',remote],capture_output=True)
        result=r.stdout+r.stderr;(binary.parent/'result.txt').write_text(result);print(result,end='')
        receipt.update(status='PASS' if r.returncode==0 else 'FAIL',result=result)
    (binary.parent/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');assert receipt['status']!='FAIL',receipt.get('result')
    print(binary)
if __name__=='__main__':main()
