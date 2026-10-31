"""Compare O2 global/local integer registers using the current production header.

Runs two isolated native fixtures; never starts, stops or reinstalls UT3.
The resolver callback uses production register snapshots and void_call_guest.
"""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', default=os.environ.get('UT3_TEST_ANDROID'), required=not os.environ.get('UT3_TEST_ANDROID'))
    args = parser.parse_args()
    sdk = Path(os.environ.get('ANDROID_SDK_ROOT', Path.home() / 'AppData/Local/Android/Sdk'))
    ndk = Path(os.environ.get('ANDROID_NDK_HOME', sdk / 'ndk/27.2.12479018'))
    clang = ndk / 'toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe'
    adb = [str(sdk / 'platform-tools/adb.exe'), '-s', args.serial]
    out = ROOT / 'build/local-regs-tests'
    out.mkdir(parents=True, exist_ok=True)

    core = (ROOT / 'android/host/core.c').read_text(encoding='utf-8')
    start = core.index('VOID_HIDDEN uint32_t g_eax')
    end = core.index('/* --------------------------------------------------------------- logging */')
    threads = (ROOT / 'android/host/threads.c').read_text(encoding='utf-8')
    callback = re.search(r'uint32_t void_call_guest\(.*?(?=\nuint32_t void_call_guest_this)', threads, re.S)
    assert callback, 'Production guest-call helper was not found'
    context = out / 'production-context.c'
    context.write_text('#include "host.h"\n' + core[start:end] + '\n' + callback.group(), encoding='utf-8')

    generated = ROOT / 'generated/ut3/functions_0528.c'
    body = re.search(r'void sub_0117C3B0\(void\) \{.*?(?=\nvoid sub_|\Z)',
                     generated.read_text(encoding='utf-8'), re.S)
    assert body, 'Production plane/bounds owner was not found'
    owner = out / 'owner.c'
    owner.write_text('#define RECOMP_GENERATED_CODE\n#include "functions.h"\n' + body.group(), encoding='utf-8')
    sources = [context, ROOT / 'tests/local_regs_probes.c', ROOT / 'tests/test_local_regs.c', owner]
    flags = ['--target=aarch64-linux-android29', '-O2', '-fno-strict-aliasing', '-fwrapv',
             '-fvisibility=hidden', '-fPIC', '-ffp-contract=off', '-fno-math-errno', '-g0',
             '-DRECOMP_X87_WINDOW', '-DEXPECT_LIVE_FLAGS', '-Iandroid/runtime', '-Iandroid/host',
             '-Igenerated/ut3']
    commands = []
    for variant, extra in [('global', []), ('local', ['-DRECOMP_LOCAL_REGS'])]:
        command = [str(clang), *flags, *extra, *map(str, sources), '-o', str(out / variant)]
        commands.append(command)
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
        (out / f'{variant}-compile.log').write_text(result.stdout + result.stderr, encoding='utf-8')
        if result.returncode:
            raise RuntimeError(f'{variant} compilation failed; see {out / (variant + "-compile.log")}')

    remote = '/data/local/tmp/ut3-localregs-tests'
    subprocess.run([*adb, 'shell', 'mkdir', '-p', remote], check=True, capture_output=True)
    results = []
    for variant in ['global', 'local']:
        subprocess.run([*adb, 'push', str(out / variant), remote + '/' + variant], check=True, capture_output=True)
        subprocess.run([*adb, 'shell', 'chmod', '755', remote + '/' + variant], check=True, capture_output=True)
        result = subprocess.run([*adb, 'shell', remote + '/' + variant], capture_output=True, text=True, timeout=20)
        results.append(dict(variant=variant, returncode=result.returncode, stdout=result.stdout, stderr=result.stderr))
        print(variant, result.stdout.strip(), flush=True)
    passed = all(r['returncode'] == 0 and 'PASS ' in r['stdout'] for r in results)
    checksums = [re.search(r'checksum=([a-f0-9]+)', r['stdout']) for r in results]
    passed = passed and all(checksums) and checksums[0][1] == checksums[1][1]
    report = dict(status='PASS' if passed else 'FAIL', utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                  device=args.serial, results=results, compile_commands=commands,
                  sha256={str(p.relative_to(ROOT)): sha(p) for p in [*sources, ROOT / 'android/runtime/recomp_types.h',
                      ROOT / 'android/host/core.c', ROOT / 'android/host/threads.c', ROOT / 'tests/test_local_regs.py',
                      out / 'global', out / 'local']},
                  scope='Current production header only; eight integer locals only. SSE/x87 global, no masked protocol, provenance or plain-memory flags.',
                  limits='Focused resolver/call/tail/backedge/EH and owner fixture, not full-engine validation or an FPS benchmark.')
    (ROOT / 'research/local-regs-production-test.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    assert passed, 'Global/local outputs differ; inspect research/local-regs-production-test.json'


if __name__ == '__main__':
    main()
