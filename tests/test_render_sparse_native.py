"""Build and compare the native sparse iterator with the independent lifted body."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/render-sparse-native-tests"


def host_run(sources):
    from host_environment import CLANG, environment
    host = OUT / "host"
    host.mkdir(parents=True, exist_ok=True)
    runtime = (ROOT / "android/runtime/recomp_types.h").read_text()
    runtime = runtime.replace('#error "This header is the ARM64 runtime model; x86 builds use native/recomp_types.h"', "")
    runtime = re.sub(r"static inline uint64_t __rdtsc\(void\) \{.*?\n\}",
        "static inline uint64_t fixture_rdtsc(void) { return 0; }", runtime, flags=re.S)
    (host / "recomp_types.h").write_text(runtime)
    text = (ROOT / "android/host/host.h").read_text()
    registers = text[text.index("typedef struct void_regs {"):text.index("} void_regs;") + len("} void_regs;")]
    (host / "host.h").write_text('#pragma once\n#include "recomp_types.h"\n#include <stdarg.h>\n' + registers +
        '\nextern recomp_ehframe_t *g_eh_frames;extern uint32_t g_eh_obj,g_eh_info;\n'
        'void void_regs_save(void_regs*);void void_regs_load(const void_regs*);\n'
        'const char *void_data_dir(void);void void_log(const char *,...);void void_dispatch_add(uint32_t,recomp_func_t);\n'
        '#define ARG(i) MEM32(g_esp+4u+4u*(uint32_t)(i))\n')
    (host / "sys").mkdir(exist_ok=True)
    (host / "sys/mman.h").write_text('#include <windows.h>\n#define PROT_READ 1\n#define PROT_WRITE 2\n'
        '#define MAP_PRIVATE 0\n#define MAP_ANONYMOUS 0\n#define MAP_FIXED_NOREPLACE 0\n'
        'static void *mmap(void *p,size_t n,int a,int b,int c,int d){return VirtualAlloc(p,n,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);}\n'
        'static int munmap(void *p,size_t n){return !VirtualFree(p,0,MEM_RELEASE);}\n'
        '#undef GPTR\n#define GPTR(va) ((void *)ADDR(va))\n')
    env, linker = environment("x64")
    log, objects = [], []
    for index, source in enumerate(sources):
        obj = host / f"{index}.obj"
        objects.append(obj)
        command = [str(CLANG), "/nologo", "/O2", "/std:c11", "/D_CRT_SECURE_NO_WARNINGS", "/DRECOMP_X87_WINDOW",
            "/clang:-fwrapv", "/clang:-fno-strict-aliasing", "/clang:-ffp-contract=off", "/I" + str(host),
            "/I" + str(OUT), "/c", str(source), "/Fo" + str(obj)]
        ran = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True)
        log.append(ran.stdout + ran.stderr)
        assert ran.returncode == 0, log[-1]
    binary = host / "fixture.exe"
    ran = subprocess.run([str(linker), "/nologo", *map(str, objects), "/OUT:" + str(binary)], cwd=ROOT, env=env, capture_output=True, text=True)
    log.append(ran.stdout + ran.stderr)
    assert ran.returncode == 0, log[-1]
    (host / "build.log").write_text("\n".join(log))
    ran = subprocess.run([str(binary)], cwd=ROOT, capture_output=True, text=True, timeout=90)
    (host / "result.txt").write_text(ran.stdout + ran.stderr)
    assert ran.returncode == 0 and "PASS native sparse iterator:" in ran.stdout, ran.stdout + ran.stderr
    print(ran.stdout, end="")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path)
    parser.add_argument("--build-only", action="store_true")
    parser.add_argument("--host", action="store_true")
    parser.add_argument("--serial", default=os.environ.get("UT3_TEST_ANDROID"))
    args = parser.parse_args()
    if not args.build_only and not args.serial and not args.host:
        parser.error("--build-only, --host or --serial is required")
    production = ROOT / "android/host/render_sparse_native.c"
    source = (args.source or production).resolve()
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "render_sparse_native_under_test.c").write_bytes(source.read_bytes())
    text = (ROOT / "android/host/core.c").read_text()
    context = OUT / "context.c"
    context.write_text('#include "host.h"\n' + text[text.index("VOID_HIDDEN uint32_t g_eax"):text.index("/* --------------------------------------------------------------- logging */")])
    text = (ROOT / "generated/ut3/functions_0061.c").read_text()
    body = re.search(r"void sub_005DAF50\(void\) \{.*?(?=\nvoid sub_|\Z)", text, re.S)[0]
    sys.path.insert(0, str(ROOT / "tools"))
    import x87_slots
    assert x87_slots.transform_file(body, "ut3")[0] == body
    oracle = OUT / "oracle.c"
    oracle.write_text('#define RECOMP_GENERATED_CODE\n#include "recomp_types.h"\n' + body.replace("void sub_005DAF50", "void __real_sub_005DAF50", 1))
    sources = [context, oracle, ROOT / "tests/test_render_sparse_native.c"]
    if args.host:
        host_run(sources)
    sdk = Path(os.environ.get("ANDROID_SDK_ROOT", Path.home() / "AppData/Local/Android/Sdk"))
    ndk = Path(os.environ.get("ANDROID_NDK_HOME", sdk / "ndk/27.2.12479018"))
    clang = ndk / "toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe"
    binary = OUT / "test-render-sparse-native"
    command = [str(clang), "--target=aarch64-linux-android29", "-O2", "-fno-strict-aliasing", "-fwrapv", "-ffp-contract=off",
        "-fno-math-errno", "-DRECOMP_X87_WINDOW", "-Iandroid/runtime", "-Iandroid/host", "-I" + str(OUT),
        *map(str, sources), "-o", str(binary), "-lm"]
    built = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    (OUT / "build.log").write_text(built.stdout + built.stderr)
    assert built.returncode == 0, built.stdout + built.stderr
    result = None
    if args.serial and not args.build_only:
        adb = [str(sdk / "platform-tools/adb.exe"), "-s", args.serial]
        remote = "/data/local/tmp/ut3-test-render-sparse-native"
        subprocess.run([*adb, "push", str(binary), remote], check=True, capture_output=True)
        try:
            subprocess.run([*adb, "shell", "chmod", "755", remote], check=True, capture_output=True)
            ran = subprocess.run([*adb, "shell", remote], capture_output=True, text=True, timeout=90)
            result = {"returncode": ran.returncode, "stdout": ran.stdout, "stderr": ran.stderr}
            print(ran.stdout + ran.stderr, end="")
        finally:
            subprocess.run([*adb, "shell", "rm", "-f", remote], capture_output=True)
    receipt = {"status": "BUILT_NOT_RUN_ON_DEVICE" if result is None else "PASS" if result["returncode"] == 0 and
        "PASS native sparse iterator:" in result["stdout"] else "FAIL", "device": args.serial, "command": command, "result": result,
        "sha256": {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in [*sources, source, binary]}}
    (OUT / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    assert receipt["status"] != "FAIL", receipt
    print(receipt["status"], binary)


if __name__ == "__main__":
    main()
