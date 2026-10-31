"""CALL-next: original x86 bytes versus emitted ARM64, without a device.

Run: python tests/test_call_next.py
Requires the configured Android NDK, Capstone, Unicorn and pyelftools. The full
UT3 stack-capture owner is exercised with StackWalk64 returning FALSE, matching
the host's unavailable unwinder. All executable fixtures stay under build/.
"""
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import unittest

import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UcError, UC_ARCH_X86, UC_ARCH_ARM64, UC_MODE_32, UC_MODE_ARM, UC_HOOK_CODE
from unicorn import x86_const as x86, arm64_const as arm

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import lift_game
from tools.disasm.disasm32 import Instruction

OWNER, CAPTURE, CONTINUATION = 0xB6AC10, 0xB6AA80, 0xB6AC72
SITE, GUEST_BASE = 0xB6AC6D, 0x2000000000
STACK, OUTPUT, CONTEXT, STOP = 0x308000, 0x320000, 0x321000, 0x200000
STUBS = {0x201000: (0, 0xFFFFFFFF), 0x201010: (0, 0xFFFFFFFE),
         0x201020: (9, 0), 0x201030: (0, 0)}
IAT = {0x1729190: 0x201000, 0x1729184: 0x201010,
       0x1729B80: 0x201020, 0x1729270: 0x201030}
REGS = ['eax', 'ebx', 'ecx', 'edx', 'esi', 'edi', 'ebp', 'esp']
XREGS = [getattr(x86, 'UC_X86_REG_' + name.upper()) for name in REGS]


def instruction(raw, address):
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True
    i = next(md.disasm(raw, address, count=1))
    return Instruction(i.address, i.size, i.mnemonic, i.op_str, i.bytes, i.operands)


def original():
    spec = json.loads((ROOT / 'config/modules.json').read_text())['modules'][0]
    inputs = json.loads((ROOT / 'config/inputs.json').read_text())
    path = Path(inputs['source_root']) / spec['path']
    raw = path.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == spec['sha256']
    pe = pefile.PE(data=raw)
    assert pe.get_data(SITE - 0x400000, 6) == bytes.fromhex('e80000000058')
    return raw, pe, spec


def initialize(machine, pe, base, count, explicit_context):
    machine.mem_map(base + 0x300000, 0x40000)
    machine.mem_write(base + 0x300000, b'\xA5' * 0x40000)
    machine.mem_map(base + 0x380000, 0x10000)
    machine.mem_write(base + 0x380000, struct.pack('<I', 0x11223344))
    for address, target in IAT.items():
        machine.mem_write(base + address, struct.pack('<I', target))
    machine.mem_write(base + STACK, struct.pack('<4I', STOP, OUTPUT, count,
                                               CONTEXT if explicit_context else 0))
    # The provided CONTEXT has the same EIP/EBP fields the original owner uses.
    machine.mem_write(base + CONTEXT, bytes(0x2CC))
    for offset, value in [(0xB4, 0x325000), (0xB8, 0x12345678), (0xC4, STACK)]:
        machine.mem_write(base + CONTEXT + offset, struct.pack('<I', value))


def oracle(raw, pe, count, explicit_context):
    vm = Uc(UC_ARCH_X86, UC_MODE_32)
    vm.mem_map(0x400000, (pe.OPTIONAL_HEADER.SizeOfImage + 0xFFF) & ~0xFFF)
    vm.mem_write(0x400000, pe.get_memory_mapped_image())
    vm.mem_map(STOP, 0x2000)
    initialize(vm, pe, 0, count, explicit_context)
    # Unicorn's default 32-bit FS descriptor has base zero. This page is the
    # oracle's TLS exception slot; the ARM64 model uses g_fs_base instead.
    vm.mem_map(0, 0x1000)
    vm.mem_write(0, struct.pack('<I', 0x11223344))
    initial = [0xA1234567, 0xB1234567, 0xC1234567, 0xD1234567,
               0x51234567, 0x61234567, 0x325000, STACK]
    for reg, value in zip(XREGS, initial):
        vm.reg_write(reg, value)
    vm.reg_write(x86.UC_X86_REG_EFLAGS, 0x246)
    walk_calls = []

    def hook(machine, address, size, userdata):
        if address not in STUBS and address not in (0x1288FD4, 0x1288EF2):
            return
        sp = machine.reg_read(x86.UC_X86_REG_ESP)
        words = struct.unpack('<10I', machine.mem_read(sp, 40))
        if address == 0x1288FD4:
            machine.mem_write(words[1], bytes([words[2] & 255]) * words[3])
            result, argc = words[1], 0
        elif address == 0x1288EF2:
            result, argc = machine.reg_read(x86.UC_X86_REG_EAX), 0
        else:
            argc, result = STUBS[address]
        if address == 0x201020:
            walk_calls.append(words[1:])
        machine.reg_write(x86.UC_X86_REG_EAX, result)
        machine.reg_write(x86.UC_X86_REG_ESP, sp + 4 + argc * 4)
        machine.reg_write(x86.UC_X86_REG_EIP, words[0])

    vm.hook_add(UC_HOOK_CODE, hook)
    vm.emu_start(OWNER, STOP, count=200000)
    return {
        'registers': [vm.reg_read(reg) for reg in XREGS],
        'output': bytes(vm.mem_read(OUTPUT, 808)),
        'fs0': bytes(vm.mem_read(0, 4)),
        'walk_calls': len(walk_calls),
    }


HARNESS = r'''
#include "recomp_types.h"
uint32_t g_eax,g_ebx,g_ecx,g_edx,g_esi,g_edi,g_ebp,g_esp;
uint32_t g_flag_k,g_flag_a,g_flag_b,g_flag_cf,g_fs_base=0x380000;
uint32_t g_backedges,walk_calls,fixture_failure;
void (*recomp_yield_hook)(void);
void sub_01288FD4(void) {
    uint32_t p=MEM32(g_esp+4),v=MEM32(g_esp+8),n=MEM32(g_esp+12);
    for(uint32_t i=0;i<n;++i) MEM8(p+i)=(uint8_t)v;
    g_eax=p;g_esp+=4;
}
void sub_01288EF2(void) {g_esp+=4;}
static void process(void) {g_eax=0xFFFFFFFFu;g_esp+=4;}
static void thread(void) {g_eax=0xFFFFFFFEu;g_esp+=4;}
static void walk(void) {++walk_calls;g_eax=0;g_esp+=40;}
static void error(void) {g_eax=0;g_esp+=4;}
static void bad(void) {fixture_failure=1;for(;;){};}
recomp_func_t void_resolve(uint32_t va) {
    switch(va) {case 0x201000:return process;case 0x201010:return thread;
    case 0x201020:return walk;case 0x201030:return error;default:return bad;}
}
void void_lift_trap(uint32_t va,const char *text) {fixture_failure=va;for(;;){};}
'''


def compile_owner(raw, pe, out):
    image = out / 'UT3.exe'
    image.write_bytes(raw)
    info = lift_game.analyze_pe(str(image))
    imports = lift_game.build_iat_map(info)
    decoder = lift_game.BoundedDisassembler(raw, info.image_base, info.sections)
    functions = {a: decoder.disassemble_function(a, imports) for a in (OWNER, CAPTURE, CONTINUATION)}
    emitted = out / 'generated'
    lift_game.emit_functions(emitted, imports, decoder, functions,
                             additional_entries=[0x1288FD4, 0x1288EF2])
    (out / 'harness.c').write_text(HARNESS)
    sdk = Path(os.environ.get('ANDROID_SDK_ROOT', Path.home() / 'AppData/Local/Android/Sdk'))
    ndk = Path(os.environ.get('ANDROID_NDK_HOME', sdk / 'ndk/27.2.12479018'))
    clang = ndk / 'toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe'
    common = [str(clang), '--target=aarch64-linux-android29', '-O2', '-fno-strict-aliasing',
              '-fwrapv', '-ffreestanding', '-fno-builtin', '-nostdlib', '-static',
              '-Wl,-e,sub_00B6AC10', '-Wl,-Ttext=0x40000000',
              '-I' + str(ROOT / 'android/runtime'), '-I' + str(emitted)]
    command = common + [str(out / 'harness.c'), str(emitted / 'recomp_0000.c'), '-o', str(out / 'owner')]
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    (out / 'compile.log').write_text(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError('Compilation failed: ' + result.stderr)
    # Keep the historical broken emission as a negative control.
    baseline = ROOT / 'research/pixel10-crash/call-next-baseline/generated/ut3/functions_0265.c'
    if baseline.exists():
        text = baseline.read_text()
        bodies = []
        for address in (OWNER, CAPTURE, CONTINUATION):
            bodies.append(re.search(r'void sub_%08X\(void\) \{.*?(?=\nvoid sub_|\Z)' % address, text, re.S)[0])
        (out / 'baseline.c').write_text('#define RECOMP_GENERATED_CODE\n#include "recomp_funcs.h"\n' + '\n'.join(bodies))
        subprocess.run(common + [str(out / 'harness.c'), str(out / 'baseline.c'), '-o', str(out / 'baseline')],
                       cwd=ROOT, check=True, capture_output=True)
    # Validate the actual generated-only production profile as well. Native
    # globals are ELF storage outside the fixed guest window, as on Android.
    flags = [str(clang), '--target=aarch64-linux-android29', '-O2', '-fno-strict-aliasing',
             '-fwrapv', '-ffreestanding', '-fno-builtin',
             '-I' + str(ROOT / 'android/runtime'), '-I' + str(emitted)]
    commands = [command]
    for source, name, extra in [
            (out / 'harness.c', 'harness.o', []),
            (emitted / 'recomp_0000.c', 'owner.o', ['-DRECOMP_GUEST_STATE_SEPARATE',
             '-mllvm', '-basic-aa-separate-storage', '-include',
             str(ROOT / 'android/runtime/guest_state_prefix.h')])]:
        cmd = flags + extra + ['-c', str(source), '-o', str(out / name)]
        subprocess.run(cmd, cwd=ROOT, check=True, capture_output=True)
        commands.append(cmd)
    cmd = [str(clang), '--target=aarch64-linux-android29', '-nostdlib', '-static',
           '-Wl,-e,sub_00B6AC10', '-Wl,-Ttext=0x40000000', str(out / 'harness.o'),
           str(out / 'owner.o'), '-o', str(out / 'owner-separate')]
    subprocess.run(cmd, cwd=ROOT, check=True, capture_output=True)
    commands.append(cmd)
    return commands


def audit_generated():
    """All generated direct-call sites, checked against pinned PE instructions."""
    rows, modules, source_count, nearby_calls = [], [], 0, 0
    pattern = re.compile(r'/\* 0x([0-9a-fA-F]+): call 0x([0-9a-fA-F]+) \*/')
    for spec in json.loads((ROOT / 'config/modules.json').read_text())['modules']:
        directory = ROOT / 'generated' / spec['name']
        report = json.loads((directory / 'report.json').read_text())
        image = ROOT / 'generated/input' / spec['file']
        raw = image.read_bytes()
        image_sha = hashlib.sha256(raw).hexdigest()
        assert image_sha == report['image_sha256'], image
        pe = pefile.PE(data=raw, fast_load=True)
        for name in report['sources']:
            source_count += 1
            path = directory / name
            text = path.read_text()
            for match in pattern.finditer(text):
                site, target = int(match[1], 16), int(match[2], 16)
                if not 0 < target - site <= 15:
                    continue
                nearby_calls += 1
                insn = instruction(pe.get_data(site - pe.OPTIONAL_HEADER.ImageBase, 15), site)
                assert insn.mnemonic == 'call' and insn.get_branch_target() == target
                if target != insn.end_address or insn.operands[0].size != 4:
                    continue
                header = text.rfind('\nvoid sub_', 0, match.start())
                owner = int(re.match(r'\nvoid sub_([0-9A-F]+)', text[header:])[1], 16)
                line_start = text.rfind('\n', 0, match.start()) + 1
                line = text[line_start:text.find('\n', match.start())].strip()
                rows.append({'module': spec['name'], 'owner': hex(owner), 'site': hex(site),
                             'next_address': hex(target), 'bytes': bytes(insn.bytes).hex(),
                             'source': str(path.relative_to(ROOT)).replace('\\', '/'),
                             'line': text.count('\n', 0, line_start) + 1, 'emitted': line,
                             'correct_push_fallthrough': line.startswith(f'PUSH32(esp, 0x{target:08X}u);')})
        modules.append({'module': spec['name'], 'image_sha256': image_sha,
                        'source_sha256': spec['sha256'], 'sources': len(report['sources'])})
    return {'status': 'PASS' if all(row['correct_push_fallthrough'] for row in rows) else 'FAIL',
            'verified_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
            'module_count': len(modules), 'generated_source_count': source_count,
            'nearby_direct_calls_checked_against_pinned_images': nearby_calls,
            'call_next_occurrences': rows, 'modules': modules}


def run_arm64(binary, pe, count, explicit_context):
    vm = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    with binary.open('rb') as file:
        elf = ELFFile(file)
        mapped = set()
        for seg in elf.iter_segments():
            if seg['p_type'] != 'PT_LOAD':
                continue
            lo, hi = seg['p_vaddr'] & ~0xFFF, (seg['p_vaddr'] + seg['p_memsz'] + 0xFFF) & ~0xFFF
            for address in range(lo, hi, 0x1000):
                if address not in mapped:
                    vm.mem_map(address, 0x1000)
                    mapped.add(address)
            vm.mem_write(seg['p_vaddr'], seg.data())
        symbols = {s.name: s['st_value'] for s in elf.get_section_by_name('.symtab').iter_symbols()}
    vm.mem_map(GUEST_BASE + 0x400000, (pe.OPTIONAL_HEADER.SizeOfImage + 0xFFF) & ~0xFFF)
    vm.mem_write(GUEST_BASE + 0x400000, pe.get_memory_mapped_image())
    initialize(vm, pe, GUEST_BASE, count, explicit_context)
    vm.mem_map(0x7100000000, 0x10000)
    vm.mem_map(0x7200000000, 0x1000)
    vm.reg_write(arm.UC_ARM64_REG_SP, 0x710000F000)
    vm.reg_write(arm.UC_ARM64_REG_LR, 0x7200000000)
    initial = [0xA1234567, 0xB1234567, 0xC1234567, 0xD1234567,
               0x51234567, 0x61234567, 0x325000, STACK]
    for name, value in zip(REGS, initial):
        vm.mem_write(symbols['g_' + name], struct.pack('<I', value))
    vm.emu_start(symbols['sub_00B6AC10'], 0x7200000000, count=400000)
    assert vm.reg_read(arm.UC_ARM64_REG_PC) == 0x7200000000
    read32 = lambda name: struct.unpack('<I', vm.mem_read(symbols[name], 4))[0]
    assert read32('fixture_failure') == 0
    return {'registers': [read32('g_' + name) for name in REGS],
            'output': bytes(vm.mem_read(GUEST_BASE + OUTPUT, 808)),
            'fs0': bytes(vm.mem_read(GUEST_BASE + 0x380000, 4)),
            'walk_calls': read32('walk_calls')}


class CallNextTest(unittest.TestCase):
    def test_pushes_actual_next_address_without_call_or_flag_transfer(self):
        for code, address in [('e800000000', SITE), ('2ee800000000', 0x401000),
                              ('f2e800000000', 0x402000)]:
            insn = instruction(bytes.fromhex(code), address)
            lifter = lift_game.CheckedLifter()
            lifter._flag_state = ('cmp', '_flag_a, _flag_b')
            lifter._flag_seq = 12
            lines = lifter.lift_instruction(insn)
            self.assertEqual(lines, [f'PUSH32(esp, 0x{address + insn.size:08X}u); '
                                     f'/* 0x{address:08X}: {insn.mnemonic} {insn.op_str} */'])
            self.assertEqual(lifter._flag_state, ('cmp', '_flag_a, _flag_b'))
            self.assertEqual(lifter._flag_seq, 12)

    def test_other_calls_keep_existing_protocol(self):
        for code in ['e801000000', 'ffd0', 'ff10']:
            lines = '\n'.join(lift_game.CheckedLifter().lift_instruction(instruction(bytes.fromhex(code), SITE)))
            self.assertIn('RECOMP_', lines)
            self.assertIn('RECOMP_FLAGS_IN();', lines)

    def test_original_owner_against_production_arm64(self):
        raw, pe, spec = original()
        out = ROOT / 'build/call-next-test'
        out.mkdir(parents=True, exist_ok=True)
        commands = compile_owner(raw, pe, out)
        results = []
        for count in (0, 1, 2, 100):
            for explicit in (False, True):
                expected = oracle(raw, pe, count, explicit)
                for variant in ['owner', 'owner-separate']:
                    actual = run_arm64(out / variant, pe, count, explicit)
                    self.assertEqual(actual, expected, (variant, count, explicit))
                    self.assertEqual(actual['registers'][-1], STACK + 4)
                    self.assertEqual(actual['registers'][6], 0x325000)
                    if count:
                        self.assertEqual(struct.unpack('<I', actual['output'][:4])[0],
                                         0x12345678 if explicit else CONTINUATION)
                    results.append({'variant': variant, 'count': count, 'explicit_context': explicit,
                                    'walk_calls': actual['walk_calls'],
                                    'output_sha256': hashlib.sha256(actual['output']).hexdigest()})
        baseline_detected = False
        if (out / 'baseline').exists():
            try:
                baseline_detected = run_arm64(out / 'baseline', pe, 2, False) != oracle(raw, pe, 2, False)
            except (UcError, AssertionError):
                baseline_detected = True
            self.assertTrue(baseline_detected, 'Historical broken CALL-next must fail the same owner fixture')
        report = {'status': 'PASS', 'verified_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
                  'original_sha256': spec['sha256'], 'owner': hex(OWNER), 'site': hex(SITE),
                  'original_site_bytes': 'e80000000058', 'cases': results,
                  'reference': 'Original full x86 owner and stack-capture helper in Unicorn',
                  'candidate': 'O2 ARM64 output with the current production header, executed in Unicorn ARM64',
                  'historical_broken_emission_detected': baseline_detected,
                  'compile_commands': commands,
                  'source_sha256': {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
                                    for p in [Path(__file__), ROOT / 'tools/lift_game.py',
                                              ROOT / 'android/runtime/recomp_types.h',
                                              ROOT / 'android/runtime/guest_state_prefix.h']},
                  'device_used': False}
        (ROOT / 'research/pixel10-crash/call-next-regression.json').write_text(json.dumps(report, indent=2) + '\n')

    def test_generated_occurrence_audit(self):
        report = audit_generated()
        self.assertEqual(report['module_count'], 20)
        self.assertEqual(report['generated_source_count'], 2005)
        self.assertEqual([(row['module'], row['owner'], row['site'])
                          for row in report['call_next_occurrences']], [('ut3', hex(OWNER), hex(SITE))])
        self.assertEqual(report['status'], 'PASS')
        (ROOT / 'research/pixel10-crash/call-next-audit.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    unittest.main()
