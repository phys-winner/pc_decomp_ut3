"""Independent lifted-oracle fixture for the connected visibility traversal."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/visibility-native-2026-11-07/fixture"
CALLS = "00402780 004050E0 00405190 004055A0 00486030 005DAF50 00B4F1F0 00B55380 00BF8120 00BF8130 00BF82A0 00BF82F0 00BF8340 00E5B1C0 00EFA7B0 00EFF830 01105580 0117AEC0 0117C3B0 0119F420 011AA010 011CA6A0 011CA9D0 011CFA90 01288FD4 012890E4".split()


def prepare():
    OUT.mkdir(parents=True, exist_ok=True)
    core = (ROOT / "android/host/core.c").read_text()
    context = OUT / "context.c"
    context.write_text('#include "host.h"\n' + core[core.index("VOID_HIDDEN uint32_t g_eax"):
        core.index("/* --------------------------------------------------------------- logging */")])
    bodies = []
    sys.path.insert(0, str(ROOT / "tools"))
    from x87_slots import transform_file
    for address, unit, replacement in [("00F01CF0", "0427", "__real_sub_00F01CF0"),
                                     ("005DAF50", "0061", "original_iterator"),
                                     ("00BF8120", "0286", "sub_00BF8120"),
                                     ("00BF8130", "0286", "sub_00BF8130")]:
        text = (ROOT / f"generated/ut3/functions_{unit}.c").read_text()
        match = re.search(r"void sub_" + address + r"\(void\) \{.*?(?=\nvoid sub_|\Z)", text, re.S)
        assert match, address
        body = match[0]
        if address == "00F01CF0":
            transformed, rewritten, kept = transform_file(body, "ut3")
            assert (rewritten, kept) == (0, 1) and transformed == body, "Visibility production x87 strategy changed"
        bodies.append(body.replace("void sub_" + address, "void " + replacement, 1))
    oracle = OUT / "oracle.c"
    oracle.write_text('#define RECOMP_GENERATED_CODE\n#include "recomp_types.h"\n' +
        "\n".join(f"void sub_{address}(void);" for address in CALLS) + "\n" + "\n".join(bodies))
    return [context, oracle, ROOT / "tests/test_render_visibility_native.c"]


def host_build(sources):
    directory = OUT / "host"
    (directory / "sys").mkdir(parents=True, exist_ok=True)
    runtime = (ROOT / "android/runtime/recomp_types.h").read_text().replace(
        '#error "This header is the ARM64 runtime model; x86 builds use native/recomp_types.h"', "")
    runtime = re.sub(r"static inline uint64_t __rdtsc\(void\) \{.*?\n\}",
        "static inline uint64_t fixture_rdtsc(void) { return 0; }", runtime, flags=re.S)
    (directory / "recomp_types.h").write_text(runtime)
    host = (ROOT / "android/host/host.h").read_text()
    registers = host[host.index("typedef struct void_regs {"):host.index("} void_regs;") + len("} void_regs;")]
    (directory / "host.h").write_text('#ifndef VOID_HOST_H\n#define VOID_HOST_H\n#include "recomp_types.h"\n'
        '#include <stdarg.h>\n' + registers + '\nextern recomp_ehframe_t *g_eh_frames; extern uint32_t g_eh_obj,g_eh_info;\n'
        'void void_regs_save(void_regs*);void void_regs_load(const void_regs*);\n'
        'const char *void_data_dir(void);void void_log(const char *,...);void void_dispatch_add(uint32_t,recomp_func_t);\n'
        '#define ARG(i) MEM32(g_esp+4u+4u*(uint32_t)(i))\n#endif\n')
    (directory / "sys/mman.h").write_text('#include <windows.h>\n#define PROT_READ 1\n#define PROT_WRITE 2\n'
        '#define MAP_PRIVATE 0\n#define MAP_ANONYMOUS 0\n#define MAP_FIXED_NOREPLACE 0\n'
        'static void *mmap(void *p,size_t n,int a,int b,int c,int d){return VirtualAlloc(p,n,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);}\n'
        'static int munmap(void *p,size_t n){return !VirtualFree(p,0,MEM_RELEASE);}\n'
        '#undef GPTR\n#define GPTR(va) ((void *)ADDR(va))\n')
    spec = importlib.util.spec_from_file_location("hostenv", ROOT / "tests/host_environment.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    env, linker = module.environment("x64")
    objects = [directory / f"{i}.obj" for i in range(len(sources))]
    commands = [[str(module.CLANG), "/nologo", "/O2", "/std:c11", "/D_CRT_SECURE_NO_WARNINGS",
        "/DRECOMP_X87_WINDOW", "/clang:-fwrapv", "/clang:-fno-strict-aliasing", "/clang:-ffp-contract=off",
        "/I" + str(directory), "/c", str(source), "/Fo" + str(obj)] for source, obj in zip(sources, objects)]
    binary = directory / "test.exe"
    commands.append([str(linker), "/nologo", *map(str, objects), "/OUT:" + str(binary)])
    return commands, binary, env


def main():
    global OUT
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", default=os.environ.get("UT3_TEST_ANDROID"))
    parser.add_argument("--build-only", action="store_true")
    parser.add_argument("--host", action="store_true", help="Run Windows x64 oracle; does not replace ARM64 validation")
    parser.add_argument("--case", type=int, help="Run one existing case, without changing its inputs")
    parser.add_argument("--output", type=Path, default=OUT, help="Isolated output directory under the project root")
    args = parser.parse_args()
    OUT = args.output.resolve()
    if not OUT.is_relative_to(ROOT):
        parser.error("--output must remain under the project root")
    if not (args.build_only or args.serial or args.host):
        parser.error("--serial, --build-only or --host is required")
    if args.case is not None and not 0 <= args.case < 1024:
        parser.error("--case must be between 0 and 1023")
    sources = prepare()
    sdk = Path(os.environ.get("ANDROID_SDK_ROOT", Path.home() / "AppData/Local/Android/Sdk"))
    if args.host:
        commands, binary, env = host_build(sources)
    else:
        ndk = Path(os.environ.get("ANDROID_NDK_HOME", sdk / "ndk/27.2.12479018"))
        clang = ndk / "toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe"
        binary, env = OUT / "test-render-visibility-native", None
        common = ["--target=aarch64-linux-android29", "-O2", "-fno-strict-aliasing", "-fwrapv",
            "-fvisibility=hidden", "-fPIC", "-ffp-contract=off", "-fno-math-errno", "-g0",
            "-DRECOMP_X87_WINDOW", "-DUT3_ANDROID_APP", "-Iandroid/runtime", "-Iandroid/host"]
        separated = ["-DRECOMP_GENERATED_CODE", "-DRECOMP_GUEST_STATE_SEPARATE",
            "-mllvm", "-basic-aa-separate-storage", "-include", str(ROOT / "android/runtime/guest_state_prefix.h")]
        commands, objects = [], []
        for source in sources:
            obj = OUT / (source.stem + ".o")
            # The fixture includes the native implementation. Context owns the
            # ordinary globals; oracle and native references use production AA.
            flags = common + (["-w"] if source.name == "oracle.c" else ["-std=gnu11", "-Wall", "-Wno-unused-function", "-Wno-unused-variable"])
            if source != sources[0]:
                flags += separated
            commands.append([str(clang), *flags, "-c", str(source), "-o", str(obj)])
            objects.append(obj)
        commands.append([str(clang), "--target=aarch64-linux-android29", *map(str, objects), "-o", str(binary), "-lm"])
    log = []
    for command in commands:
        built = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True)
        log.append(built.stdout + built.stderr)
        (binary.parent / "build.log").write_text("\n".join(log))
        assert built.returncode == 0, log[-1]
    files = [*sources, ROOT / "android/host/render_visibility_native.c", ROOT / "android/runtime/recomp_types.h",
        ROOT / "android/runtime/guest_state_prefix.h", ROOT / "android/host/core.c", ROOT / "android/host/host.h",
        ROOT / "tools/x87_slots.py", Path(__file__), binary]
    receipt = {"status": "BUILT", "target": "host-x64" if args.host else "android-arm64", "commands": commands,
        "oracle": "Unmodified lifted F01CF0, sparse iterator and bit-reference helpers; other engine boundaries mocked.",
        "native_arm_profile": "Production separated guest-state references on the native-containing fixture TU and oracle; ordinary context definitions.",
        "limitations": "Synthetic fixtures, not gameplay or performance evidence. Host execution does not establish ARM64 floating-point parity.",
        "sha256": {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in files}}
    receipt_path = binary.parent / "receipt.json"
    receipt_path.write_text(json.dumps(receipt, indent=2) + "\n")
    if args.build_only:
        print(binary)
        return
    case_args = [] if args.case is None else [str(args.case)]
    if args.host:
        ran = subprocess.run([str(binary), *case_args], capture_output=True, text=True, timeout=120)
    else:
        adb = [str(sdk / "platform-tools/adb.exe"), "-s", args.serial]
        remote = "/data/local/tmp/ut3-test-render-visibility-native"
        try:
            subprocess.run([*adb, "push", str(binary), remote], check=True, capture_output=True)
            subprocess.run([*adb, "shell", "chmod", "755", remote], check=True, capture_output=True)
            ran = subprocess.run([*adb, "shell", remote, *case_args], capture_output=True, text=True, timeout=120)
        finally:
            subprocess.run([*adb, "shell", "rm", "-f", remote], capture_output=True)
    result = ran.stdout + ran.stderr
    (binary.parent / "result.txt").write_text(result)
    print(result, end="")
    assert ran.returncode == 0 and "PASS native visibility:" in result, f"exit {ran.returncode}: {result}"
    receipt.update(status="PASS", result=result, case=args.case)
    receipt_path.write_text(json.dumps(receipt, indent=2) + "\n")


if __name__ == "__main__":
    main()
