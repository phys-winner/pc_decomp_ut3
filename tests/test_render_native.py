"""Build/run native render replacements against the unmodified lifted functions."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/render-native-tests"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", default=os.environ.get("UT3_TEST_ANDROID"))
    parser.add_argument("--build-only", action="store_true")
    parser.add_argument("--cases", type=int, default=200000)
    args = parser.parse_args()
    if not args.build_only and not args.serial:
        parser.error("--serial or --build-only is required")
    sdk = Path(os.environ.get("ANDROID_SDK_ROOT", Path.home() / "AppData/Local/Android/Sdk"))
    ndk = Path(os.environ.get("ANDROID_NDK_HOME", sdk / "ndk/27.2.12479018"))
    clang = ndk / "toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe"
    OUT.mkdir(parents=True, exist_ok=True)
    core = (ROOT / "android/host/core.c").read_text()
    context = OUT / "context.c"
    context.write_text('#include "host.h"\n' + core[core.index("VOID_HIDDEN uint32_t g_eax"):
        core.index("/* --------------------------------------------------------------- logging */")])
    oracle = OUT / "oracle.c"
    bodies = []
    bounds = None
    for address, owner in [("00486030", "0022"), ("0117C3B0", "0528"), ("0117D890", "0528")]:
        text = (ROOT / f"generated/ut3/functions_{owner}.c").read_text()
        match = re.search(r"void sub_" + address + r"\(void\) \{.*?(?=\nvoid sub_|\Z)", text, re.S)
        assert match, address
        if address == "0117D890":
            bounds = match[0]
        name = "render_bounds_oracle" if address == "0117D890" else "__real_sub_" + address
        bodies.append(match[0].replace("void sub_" + address, "void " + name, 1))
    declarations = 'void sub_0041FF80(void); void sub_0041FF60(void); void sub_0117C2A0(void);\n'
    oracle.write_text('#define RECOMP_GENERATED_CODE\n#include "recomp_types.h"\n' + declarations + "\n".join(bodies))
    sys.path.insert(0, str(ROOT / "tools"))
    from build_android import DEFAULT_CALL_PROTOCOL, NATIVE_CALL_CONTRACT, WRAPS, host_overrides, validate_native_call_contract
    from protocol_transform import Protocol, apply_protocol
    full = (set(WRAPS) | host_overrides(sorted((ROOT / "android/host").glob("*.c")))) - NATIVE_CALL_CONTRACT
    protocol = Protocol(DEFAULT_CALL_PROTOCOL, full=full, masked_native=NATIVE_CALL_CONTRACT)
    validate_native_call_contract(protocol)
    assert protocol.call_masks("sub_0117D890") == (0x17, 0x17, 0)
    masked = OUT / "masked.c"
    body = apply_protocol(bounds.encode(), protocol, "ut3").decode()
    body = body.replace("void sub_0117D890", "void render_bounds_masked_oracle", 1)
    masked.write_text('''#define RECOMP_GENERATED_CODE
#define RECOMP_LOCAL_REGS
#include "host.h"
''' + declarations + '''
void render_bounds_masked_call(recomp_func_t target, const uint32_t *input, uint32_t *output)
{
    RECOMP_REGS_LOCALS
    uint32_t _flag_k = input[8], _flag_a = input[9], _flag_b = input[10], _cf = input[11];
    eax = input[0]; ecx = input[1]; edx = input[2]; ebx = input[3];
    esp = input[4]; ebp = input[5]; esi = input[6]; edi = input[7];
    RECOMP_CALL_P(target, 0x17u, 0x17u, 0, 0x0117D890u);
    output[0] = eax; output[1] = ecx; output[2] = edx; output[3] = ebx;
    output[4] = esp; output[5] = ebp; output[6] = esi; output[7] = edi;
    output[8] = _flag_k; output[9] = _flag_a; output[10] = _flag_b; output[11] = _cf;
}
''' + body)
    binary = OUT / "test-render-native"
    sources = [context, oracle, masked, ROOT / "tests/test_render_native.c"]
    command = [str(clang), "--target=aarch64-linux-android29", "-O2", "-fno-strict-aliasing", "-fwrapv",
        "-ffp-contract=off", "-fno-math-errno", "-DRECOMP_X87_WINDOW", "-Iandroid/runtime", "-Iandroid/host",
        *map(str, sources), "-o", str(binary), "-lm"]
    built = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    (OUT / "build.log").write_text(built.stdout + built.stderr)
    assert built.returncode == 0, built.stdout + built.stderr
    result = None
    if not args.build_only:
        adb = [str(sdk / "platform-tools/adb.exe"), "-s", args.serial]
        remote = "/data/local/tmp/ut3-test-render-native"
        subprocess.run([*adb, "push", str(binary), remote], check=True, capture_output=True)
        try:
            subprocess.run([*adb, "shell", "chmod", "755", remote], check=True, capture_output=True)
            ran = subprocess.run([*adb, "shell", remote, str(args.cases)], capture_output=True, text=True, timeout=60)
            result = {"returncode": ran.returncode, "stdout": ran.stdout, "stderr": ran.stderr}
            print(ran.stdout + ran.stderr, end="")
        finally:
            subprocess.run([*adb, "shell", "rm", "-f", remote], capture_output=True)
    files = [*sources, ROOT / "android/host/render_native.c", ROOT / "android/runtime/recomp_types.h", binary]
    receipt = {"status": "BUILT_NOT_RUN" if result is None else "PASS" if result["returncode"] == 0 and
        "PASS native render:" in result["stdout"] else "FAIL", "device": args.serial, "command": command,
        "result": result, "sha256": {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in files}}
    (OUT / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    assert receipt["status"] != "FAIL", receipt
    print(receipt["status"], binary)


if __name__ == "__main__":
    main()
