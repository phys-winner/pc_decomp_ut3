"""Compare the native bit-array cluster with all four original lifted bodies."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/"build/visibility-native-2026-11-07/bitarray-fixture"
ADDRESSES=("00BF81A0","00BF82A0","00BF82F0","00BF8340")


def prepare():
    OUT.mkdir(parents=True,exist_ok=True)
    core=(ROOT/"android/host/core.c").read_text()
    context=OUT/"context.c"
    context.write_text('#include "host.h"\n'+core[core.index("VOID_HIDDEN uint32_t g_eax"):core.index("/* --------------------------------------------------------------- logging */")])
    source=(ROOT/"generated/ut3/functions_0286.c").read_text()
    bodies=[]
    for address in ADDRESSES:
        body=re.search(r"void sub_"+address+r"\(void\) \{.*?(?=\nvoid sub_|\Z)",source,re.S)[0]
        assert "RECOMP_BACKEDGE" not in body and "fp_push" not in body and "fp_pop" not in body
        for renamed in ADDRESSES:
            body=re.sub(r"\bsub_"+renamed+r"\b","__real_sub_"+renamed,body)
        bodies.append(body)
    oracle=OUT/"oracle.c"
    oracle.write_text('#define RECOMP_GENERATED_CODE\n#include "recomp_types.h"\n'
        'void sub_004055A0(void);void sub_012890DE(void);void sub_01288FD4(void);\n'+
        "\n".join("void __real_sub_"+a+"(void);" for a in ADDRESSES)+"\n"+"\n".join(bodies))
    return [context,oracle,ROOT/"tests/test_render_bitarray_native.c"]


def host_build(sources):
    directory=OUT/"host";(directory/"sys").mkdir(parents=True,exist_ok=True)
    runtime=(ROOT/"android/runtime/recomp_types.h").read_text().replace(
        '#error "This header is the ARM64 runtime model; x86 builds use native/recomp_types.h"',"")
    runtime=re.sub(r"static inline uint64_t __rdtsc\(void\) \{.*?\n\}",
        "static inline uint64_t fixture_rdtsc(void) { return 0; }",runtime,flags=re.S)
    (directory/"recomp_types.h").write_text(runtime)
    header=(ROOT/"android/host/host.h").read_text()
    registers=header[header.index("typedef struct void_regs {"):header.index("} void_regs;")+len("} void_regs;")]
    (directory/"host.h").write_text('#ifndef VOID_HOST_H\n#define VOID_HOST_H\n#include "recomp_types.h"\n#include <stdarg.h>\n'+registers+
        '\nextern recomp_ehframe_t *g_eh_frames;extern uint32_t g_eh_obj,g_eh_info;\n'
        'void void_regs_save(void_regs*);void void_regs_load(const void_regs*);\n'
        'const char *void_data_dir(void);void void_log(const char *,...);void void_dispatch_add(uint32_t,recomp_func_t);\n'
        '#define ARG(i) MEM32(g_esp+4u+4u*(uint32_t)(i))\n#endif\n')
    (directory/"sys/mman.h").write_text('#include <windows.h>\n#define PROT_READ 1\n#define PROT_WRITE 2\n'
        '#define MAP_PRIVATE 0\n#define MAP_ANONYMOUS 0\n#define MAP_FIXED_NOREPLACE 0\n'
        'static void *mmap(void *p,size_t n,int a,int b,int c,int d){return VirtualAlloc(p,n,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);}\n'
        'static int munmap(void *p,size_t n){return !VirtualFree(p,0,MEM_RELEASE);}\n#undef GPTR\n#define GPTR(va) ((void *)ADDR(va))\n')
    spec=importlib.util.spec_from_file_location("hostenv",ROOT/"tests/host_environment.py")
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    env,linker=module.environment("x64");objects=[directory/f"{i}.obj" for i in range(len(sources))]
    commands=[[str(module.CLANG),"/nologo","/O2","/std:c11","/D_CRT_SECURE_NO_WARNINGS","/DRECOMP_X87_WINDOW",
        "/clang:-fwrapv","/clang:-fno-strict-aliasing","/clang:-ffp-contract=off","/I"+str(directory),"/c",str(source),"/Fo"+str(obj)]
        for source,obj in zip(sources,objects)]
    binary=directory/"test.exe";commands.append([str(linker),"/nologo",*map(str,objects),"/OUT:"+str(binary)])
    return commands,binary,env


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial",default=os.environ.get("UT3_TEST_ANDROID"))
    parser.add_argument("--build-only",action="store_true")
    parser.add_argument("--host",action="store_true")
    args=parser.parse_args()
    if not(args.serial or args.build_only or args.host):parser.error("--serial, --build-only or --host is required")
    sources=prepare();sdk=Path(os.environ.get("ANDROID_SDK_ROOT",Path.home()/"AppData/Local/Android/Sdk"))
    if args.host:commands,binary,env=host_build(sources)
    else:
        ndk=Path(os.environ.get("ANDROID_NDK_HOME",sdk/"ndk/27.2.12479018"))
        clang=ndk/"toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe";binary=OUT/"test-render-bitarray-native";env=None
        commands=[[str(clang),"--target=aarch64-linux-android29","-O2","-fno-strict-aliasing","-fwrapv","-ffp-contract=off",
            "-DRECOMP_X87_WINDOW","-Iandroid/runtime","-Iandroid/host",*map(str,sources),"-o",str(binary),"-lm"]]
    logs=[]
    for command in commands:
        result=subprocess.run(command,cwd=ROOT,env=env,capture_output=True,text=True);logs.append(result.stdout+result.stderr)
        (binary.parent/"build.log").write_text("\n".join(logs));assert result.returncode==0,logs[-1]
    files=[*sources,ROOT/"android/host/render_bitarray_native.c",ROOT/"android/runtime/recomp_types.h",ROOT/"android/host/core.c",
        ROOT/"android/host/host.h",ROOT/"generated/ut3/functions_0286.c",Path(__file__),binary]
    receipt={"status":"BUILT","target":"host-x64" if args.host else "android-arm64","commands":commands,
        "oracle":"Four canonical lifted bodies, only symbol names changed; all cluster-internal oracle calls remain original.",
        "limitations":"Allocation and memcpy/memset boundaries are deterministic mocked engine callbacks; no gameplay/performance claim.",
        "sha256":{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in files}}
    receipt_path=binary.parent/"receipt.json";receipt_path.write_text(json.dumps(receipt,indent=2)+"\n")
    if args.build_only:print(binary);return
    if args.host:ran=subprocess.run([str(binary)],capture_output=True,text=True,timeout=120)
    else:
        adb=[str(sdk/"platform-tools/adb.exe"),"-s",args.serial];remote="/data/local/tmp/ut3-test-render-bitarray-native"
        try:
            subprocess.run([*adb,"push",str(binary),remote],check=True,capture_output=True)
            subprocess.run([*adb,"shell","chmod","755",remote],check=True,capture_output=True)
            ran=subprocess.run([*adb,"shell",remote],capture_output=True,text=True,timeout=120)
        finally:subprocess.run([*adb,"shell","rm","-f",remote],capture_output=True)
    result=ran.stdout+ran.stderr;(binary.parent/"result.txt").write_text(result);print(result,end="")
    assert ran.returncode==0 and "PASS native bitarray:" in result,f"exit {ran.returncode}: {result}"
    receipt.update(status="PASS",result=result);receipt_path.write_text(json.dumps(receipt,indent=2)+"\n")


if __name__=="__main__":main()
