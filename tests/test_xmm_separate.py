"""Cross-compile pinned global/volatile XMM separation fixtures; never uses adb."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/xmm-separate-candidate/correctness'
OUT.mkdir(parents=True, exist_ok=True)
PIN = ROOT / 'build/xmm-separate-candidate/candidate.json'
pins = json.loads(PIN.read_text(encoding='utf-8'))
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
canonical = ROOT / 'android/runtime/recomp_types.h'
candidate = ROOT / 'build/xmm-separate-candidate/include/recomp_types.h'
prefix = ROOT / 'android/runtime/guest_state_prefix.h'
assert sha(canonical) == pins['canonical_header_sha256']
assert sha(candidate) == pins['candidate_header_sha256']
assert sha(prefix) == pins['unchanged_prefix_sha256']
assert sha(candidate.parent / prefix.name) == sha(prefix)
sdk = Path(os.environ.get('ANDROID_SDK_ROOT', Path.home() / 'AppData/Local/Android/Sdk'))
llvm = sdk / 'ndk/27.2.12479018/toolchains/llvm/prebuilt/windows-x86_64/bin'
clang = llvm / 'clang.exe'
commands = []
def run(values):
    values = list(map(str, values)); commands.append(values)
    result = subprocess.run(values, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if result.returncode: raise RuntimeError(result.stdout.decode(errors='replace'))
    return result.stdout

flags = ['--target=aarch64-linux-android29', '-O2', '-std=gnu11', '-Wall', '-fno-strict-aliasing',
         '-fwrapv', '-ffp-contract=off', '-fno-math-errno', '-fPIC', '-fvisibility=hidden', '-g0',
         '-DRECOMP_X87_WINDOW']
includes = ['-Iandroid/runtime', '-Iandroid/host', '-Ibuild/host-smoke', '-Ireference/mojoshader']
host_sources = [ROOT / 'tests/test_xmm_separate.c'] + [ROOT / 'android/host' / f for f in ('core.c', 'memory.c', 'strings.c')]
objects = []
for path in host_sources:
    obj = OUT / (path.stem + '.o'); run([clang, *flags, *includes, '-c', path, '-o', obj]); objects.append(obj)
support = [p for p in sorted((ROOT / 'build/host-smoke/obj').rglob('*.o'))
           if p.name not in {'main.o', 'android_main.o', 'core.o', 'threads.o', 'memory.o', 'strings.o', 'loading.o', 'crypt32.o'}]
assert support
probe = ROOT / 'tests/test_xmm_separate.probes.c'
owner_source = ROOT / 'generated/ut3/functions_0528.c'
body = re.search(r'void sub_0117C3B0\(void\) \{.*?(?=\nvoid sub_|\Z)', owner_source.read_text(encoding='utf-8'), re.S)
assert body
owner = OUT / 'owner.c'
owner.write_text('#define RECOMP_GENERATED_CODE\n#include "recomp_types.h"\n' + body[0], encoding='utf-8')
variants = []
for name, header in [('baseline', canonical), ('candidate', candidate)]:
    generated = flags + ['-DRECOMP_GENERATED_CODE', '-DRECOMP_GUEST_STATE_SEPARATE',
                         '-mllvm', '-basic-aa-separate-storage', '-include', header.parent / prefix.name,
                         '-I' + str(header.parent), *includes]
    obj = OUT / (name + '-probes.o'); run([clang, *generated, '-c', probe, '-o', obj])
    owner_obj = OUT / (name + '-owner.o'); run([clang, *generated, '-c', owner, '-o', owner_obj])
    ir = OUT / (name + '-probes.ll'); run([clang, *generated, '-S', '-emit-llvm', probe, '-o', ir])
    text = ir.read_text(encoding='utf-8')
    body = re.search(r'define[^\n]*@fixture_xmm_volatile_read\(.*?^}', text, re.S | re.M)
    assert body and len(re.findall(r'load volatile i32', body[0])) == 2, 'Volatile guest reads changed'
    binary = OUT / ('test-xmm-' + name)
    rsp = OUT / (name + '-link.rsp')
    rsp.write_text('\n'.join('"' + p.as_posix() + '"' for p in [*objects, obj, owner_obj, *support]) + '\n')
    run([clang, '--target=aarch64-linux-android29', '-Wl,-z,nostart-stop-gc', '@' + str(rsp), '-o', binary,
         '-lm', '-ldl', '-llog', '-landroid', '-lEGL', '-lGLESv3', '-laaudio', '-lz'])
    variants.append({'name': name, 'binary': str(binary.relative_to(ROOT)), 'binary_sha256': sha(binary),
                     'header_sha256': sha(header), 'ir_sha256': sha(ir), 'volatile_reads': 2})
assert sha(canonical) == pins['canonical_header_sha256'] and sha(candidate) == pins['candidate_header_sha256']
report = {'status': 'CROSS_COMPILED_NOT_RUN', 'scope': 'No adb/app/build lifecycle; real core/memory/threads with synthetic byte-level XMM overlap oracle and actual callback/VM-handover paths.',
          'commands': commands, 'variants': variants,
          'sources_sha256': {str(p.relative_to(ROOT)): sha(p) for p in [*host_sources, probe, owner, owner_source, Path(__file__), ROOT / 'android/host/threads.c', canonical, candidate, prefix]},
          'support_sha256': {str(p.relative_to(ROOT)): sha(p) for p in support}, 'candidate_pin_sha256': sha(PIN),
          'runtime_required': 'Run both isolated ELFs; require PASS and identical checksum. No runtime result is inferred from successful compilation.'}
(OUT / 'receipt.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
print('CROSS_COMPILED_NOT_RUN; volatile IR checks PASS; canonical header unchanged')
for v in variants: print(v['name'] + ': ' + v['binary'])
