"""Compare the call-contract native kernels with freshly extracted lifted originals on ARM64.

Kernels: core_native.c (container finds, variable opcodes, shader parameter bridges), collision_native.c (0116C250), crt_native.c
(msvcr80 memcpy/memmove/memset against MSVC 8's memmove semantics), particle_native.c (00BF8DA0). See tests/test_contract_native.c.
"""
import argparse
import glob
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
ORIGINALS = ['004B49A0', '00B65BD0', '004B9D60', '00F78830', '00E2DC90', '00BC1310', '00BC12C0', '0116C250', '00BF8DA0',
             '00EC2580', '00EC2610', '00EC26A0', '00EC2730']
SHADERS = ORIGINALS[-4:]


def original(address, prefix='__real_'):
    for path in sorted(glob.glob(str(ROOT / 'generated/ut3/functions_*.c'))):
        text = Path(path).read_text(encoding='utf-8')
        match = re.search(r'void sub_' + address + r'\(void\) \{.*?(?=\nvoid sub_|\Z)', text, re.S)
        if match:
            return match.group().replace('void sub_' + address, 'void ' + prefix + 'sub_' + address, 1)
    raise AssertionError(f'Missing original {address}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', default=os.environ.get('UT3_TEST_ANDROID'))
    parser.add_argument('--build-only', action='store_true')
    args = parser.parse_args()
    if not args.build_only and not args.serial:
        parser.error('--serial is required to run the device fixture')
    sdk = Path(os.environ.get('ANDROID_SDK_ROOT', Path.home() / 'AppData/Local/Android/Sdk'))
    ndk = Path(os.environ.get('ANDROID_NDK_HOME', sdk / 'ndk/27.2.12479018'))
    clang = ndk / 'toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe'
    out = ROOT / 'build/contract-native-tests'
    out.mkdir(parents=True, exist_ok=True)
    core = (ROOT / 'android/host/core.c').read_text(encoding='utf-8')
    start = core.index('VOID_HIDDEN uint32_t g_eax')
    end = core.index('/* --------------------------------------------------------------- logging */')
    (out / 'context.c').write_text('#include "host.h"\n' + core[start:end], encoding='utf-8')
    (out / 'originals.c').write_text('#define RECOMP_GENERATED_CODE\n#include "host.h"\nvoid sub_00B55380(void);\n' +
                                     '\n'.join(original(a) for a in ORIGINALS), encoding='utf-8')
    sys.path.insert(0, str(ROOT / 'tools'))
    from build_android import DEFAULT_CALL_PROTOCOL, NATIVE_CALL_CONTRACT, WRAPS, host_overrides, validate_native_call_contract
    from protocol_transform import Protocol, apply_protocol
    full = (set(WRAPS) | host_overrides(sorted((ROOT / 'android/host').glob('*.c')))) - NATIVE_CALL_CONTRACT
    protocol = Protocol(DEFAULT_CALL_PROTOCOL, full=full, masked_native=NATIVE_CALL_CONTRACT)
    validate_native_call_contract(protocol)
    masked = '\n'.join(original(a, '') for a in SHADERS)
    masked = apply_protocol(masked.encode(), protocol, 'ut3').decode()
    for address in SHADERS:
        masked = masked.replace('void sub_' + address, 'void __masked_sub_' + address, 1)
    (out / 'masked.c').write_text('''#define RECOMP_GENERATED_CODE
#define RECOMP_LOCAL_REGS
#include "host.h"
void sub_00B55380(void);
void shader_masked_call(recomp_func_t target, const uint32_t *input, uint32_t *output)
{
    RECOMP_REGS_LOCALS
    uint32_t _flag_k = input[8], _flag_a = input[9], _flag_b = input[10], _cf = input[11];
    eax = input[0]; ecx = input[1]; edx = input[2]; ebx = input[3];
    esp = input[4]; ebp = input[5]; esi = input[6]; edi = input[7];
    RECOMP_CALL_P(target, 0x17u, 0x17u, 0, 0x00EC2580u);
    output[0] = eax; output[1] = ecx; output[2] = edx; output[3] = ebx;
    output[4] = esp; output[5] = ebp; output[6] = esi; output[7] = edi;
    output[8] = _flag_k; output[9] = _flag_a; output[10] = _flag_b; output[11] = _cf;
}
''' + masked, encoding='utf-8')
    binary = out / 'test-contract-native'
    command = [str(clang), '--target=aarch64-linux-android29', '-O2', '-fno-strict-aliasing', '-fwrapv',
               '-ffp-contract=off', '-fno-math-errno', '-DRECOMP_X87_WINDOW', '-Iandroid/runtime', '-Iandroid/host',
               '-Wl,-z,max-page-size=16384', str(ROOT / 'tests/test_contract_native.c'), str(out / 'originals.c'),
               str(out / 'masked.c'), str(out / 'context.c'), '-o', str(binary), '-lm']
    subprocess.run(command, cwd=ROOT, check=True)
    print(binary)
    if args.build_only:
        return
    adb = [str(sdk / 'platform-tools/adb.exe'), '-s', args.serial]
    remote = '/data/local/tmp/ut3-test-contract-native'
    try:
        subprocess.run([*adb, 'push', str(binary), remote], check=True, capture_output=True)
        subprocess.run([*adb, 'shell', 'chmod', '755', remote], check=True)
        result = subprocess.run([*adb, 'shell', remote], capture_output=True, text=True, timeout=600)
        (out / 'result.txt').write_text(result.stdout + result.stderr, encoding='utf-8')
        print(result.stdout + result.stderr, end='')
        assert result.returncode == 0 and 'PASS ' in result.stdout
    finally:
        subprocess.run([*adb, 'shell', 'rm', '-f', remote], check=False, capture_output=True)


if __name__ == '__main__':
    main()
