"""Original CMPXCHG8B bytes versus checked emission and production ARM64 atomicity."""
import ctypes
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX,
                              UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP, UC_X86_REG_ESP, UC_X86_REG_EFLAGS)

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import lift_game
from pe_rebase import rebase_image

DATA, CODE, STACK, STOP = 0x25000000, 0x10000, 0x308000, 0x400000
FORMS = ['f00fc70e', '0fc70e', 'f00fc708', 'f00fc74c9010', 'f00fc74ccbfc']
REGISTERS = [UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX,
             UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP, UC_X86_REG_ESP]
BITS = (1, 4, 0x10, 0x40, 0x80, 0x800, 0x400, 0x200000)
EDGES = (0, 1, 0xFFFFFFFF, 0x100000000, 0x8000000000000000, 0xFFFFFFFFFFFFFFFF,
         0x0123456789ABCDEF, 0xFEDCBA9876543210)


def native_oracle():
    if sys.platform != 'win32':
        return None
    k32 = ctypes.WinDLL('kernel32', use_last_error=True)
    k32.VirtualAlloc.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint32, ctypes.c_uint32]
    k32.VirtualAlloc.restype = ctypes.c_void_p
    k32.VirtualFree.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint32]
    k32.VirtualFree.restype = ctypes.c_int
    memory = k32.VirtualAlloc(DATA, 0x4000, 0x3000, 4)
    assert memory == DATA
    # Win64 ABI wrapper: retain host flags/nonvolatile registers, execute the
    # original four bytes, and capture guest GPRs/flags before restoring host DF.
    raw = bytes.fromhex('5355565741549c415c4989c8'
                        '418b00418b5804418b4808418b500c418b7010418b7814418b6818'
                        '458b482041519d' + FORMS[0] +
                        '9c415941894024418958284189482c4189503041897034418978384189683c45894840'
                        '41549d415c5f5e5d5bc3')
    executable = k32.VirtualAlloc(None, len(raw), 0x3000, 0x40)
    assert executable
    ctypes.memmove(executable, raw, len(raw))
    function = ctypes.WINFUNCTYPE(None, ctypes.POINTER(ctypes.c_uint32))(executable)

    def execute(registers, flags, address, old):
        state = (ctypes.c_uint32 * 17)(*registers, flags)
        ctypes.c_uint64.from_address(address).value = old
        function(state)
        result = list(state[9:16]) + [registers[7]]
        return result, state[16], ctypes.c_uint64.from_address(address).value

    def close():
        assert k32.VirtualFree(executable, 0, 0x8000) and k32.VirtualFree(memory, 0, 0x8000)

    return execute, close


def main():
    specs = json.loads((ROOT / 'config/modules.json').read_text())['modules']
    spec = next(row for row in specs if row['name'] == 'tier0_s')
    game = Path(json.loads((ROOT / 'config/inputs.json').read_text())['source_root'])
    raw = (game / spec['path']).read_bytes()
    assert hashlib.sha256(raw).hexdigest() == spec['sha256']
    raw, _ = rebase_image(raw, int(spec['base'], 0))
    pe = pefile.PE(data=raw, fast_load=True)
    assert pe.get_data(0xBC59, 4).hex() == FORMS[0]
    output = ROOT / 'build/cmpxchg8b-test'
    generated = output / 'generated'; generated.mkdir(parents=True, exist_ok=True)
    image = output / spec['file']; image.write_bytes(raw)
    info = lift_game.analyze_pe(str(image)); imports = lift_game.build_iat_map(info)
    decoder = lift_game.BoundedDisassembler(raw, info.image_base, info.sections)
    owner = decoder.disassemble_function(0x2100BC40, imports)
    assert not lift_game.function_dispatch_targets(owner)
    report = lift_game.emit_functions(generated, imports, decoder, {owner.address: owner},
                                     label='CMPXCHG8B owner test', header_name='functions.h')
    assert not report['functions_containing_traps']
    lift_game.emit_dispatch(generated / 'dispatch.c', [owner.address], ['functions.h'])
    md = Cs(CS_ARCH_X86, CS_MODE_32); md.detail = True
    probes = []
    for index, code in enumerate(FORMS):
        instruction = next(md.disasm(bytes.fromhex(code), 0x2100BC59))
        lifter = lift_game.CheckedLifter()
        body = '\n'.join(lifter.lift_instruction(instruction))
        assert 'void_lift_trap' not in body and lifter._flag_state[0] == 'eflags'
        assert 'FK_EFLAGS' in lifter._make_condition('sete')
        probes.append(f'case {index}: {{\n{body}\n}} break;')
    invalid = next(md.disasm(bytes.fromhex('67f00fc708'), 0x1000))
    assert 'void_lift_trap' in '\n'.join(lift_game.CheckedLifter().lift_instruction(invalid))
    (generated / 'probe.inc').write_text('\n'.join(probes) + '\n')
    machine = Uc(UC_ARCH_X86, UC_MODE_32)
    for address, size in ((DATA, 0x4000), (CODE, 4096), (0x300000, 0x10000), (STOP, 4096)):
        machine.mem_map(address, size)
    machine.mem_map(info.image_base, (pe.OPTIONAL_HEADER.SizeOfImage + 4095) & -4096)
    machine.mem_write(info.image_base, pe.get_memory_mapped_image())
    native = native_oracle()
    count, native_count = 0, 0
    vectors = output / 'vectors.bin'
    with vectors.open('wb') as file:
        file.write(b'\0' * 4)
        for form in range(len(FORMS) + 1):
            full_owner = form == len(FORMS)
            code = bytes.fromhex(FORMS[form]) if not full_owner else b''
            if code:
                machine.mem_write(CODE, code)
                machine.ctl_remove_cache(CODE, CODE + 16)
            for mask in range(256 if not full_owner else 64):
                flags = 0x202 | sum(bit for index, bit in enumerate(BITS) if mask & (1 << index))
                for offset in (*range(8), 63, 4093):
                    address = DATA + offset
                    for matched in (False, True):
                        expected = EDGES[(mask + offset) % len(EDGES)]
                        registers = [expected & 0xFFFFFFFF, 0x89ABCDEF, 0xFEDCBA98, expected >> 32,
                                     address, 0x87654321, 0x76543210, STACK]
                        if form == 2:
                            registers[0] = address
                        elif form == 3:
                            registers[3] = 0xF0000100
                            registers[0] = (address - registers[3] * 4 - 0x10) & 0xFFFFFFFF
                        elif form == 4:
                            registers[2] = 0xE0000002
                            registers[1] = (address - registers[2] * 8 + 4) & 0xFFFFFFFF
                        comparison = (registers[3] << 32) | registers[0]
                        old = comparison if matched else comparison ^ EDGES[(mask + offset + 3) % len(EDGES)] ^ 0x100000001
                        if old == comparison:
                            old ^= 1
                        if matched:
                            old = comparison
                        machine.mem_write(DATA, bytes([0xA5]) * 0x4000)
                        machine.mem_write(address, struct.pack('<Q', old))
                        for register, value in zip(REGISTERS, registers):
                            machine.reg_write(register, value)
                        machine.reg_write(UC_X86_REG_EFLAGS, flags)
                        if full_owner:
                            machine.mem_write(STACK, struct.pack('<6I', STOP, address, registers[1], registers[2], registers[0], registers[3]))
                            machine.emu_start(owner.address, STOP, count=100)
                        else:
                            machine.emu_start(CODE, CODE + len(code), count=1)
                        result_registers = [machine.reg_read(register) for register in REGISTERS]
                        result_flags = machine.reg_read(UC_X86_REG_EFLAGS)
                        result_memory = struct.unpack('<Q', machine.mem_read(address, 8))[0]
                        assert result_flags == (flags & ~0x40) | (0x40 if matched else 0)
                        replacement = (registers[2] << 32) | registers[1]
                        assert result_memory == (replacement if matched else old)
                        if not full_owner:
                            expected_registers = registers.copy()
                            if not matched:
                                expected_registers[0], expected_registers[3] = old & 0xFFFFFFFF, old >> 32
                            assert result_registers == expected_registers
                        if native and form == 0 and offset < 8:
                            assert native[0](registers, flags, address, old) == (result_registers, result_flags, result_memory)
                            native_count += 1
                        file.write(struct.pack('<IIQ8II8IIQ', 100 if full_owner else form, address, old,
                                               *registers, flags, *result_registers, result_flags, result_memory))
                        count += 1
        file.seek(0); file.write(struct.pack('<I', count))
    if native:
        native[1]()
    print('CMPXCHG8B original Unicorn PASS:', count, '; native Windows x86-64:', native_count, flush=True)
    sdk = Path(os.environ.get('ANDROID_SDK_ROOT', Path.home() / 'AppData/Local/Android/Sdk'))
    ndk = Path(os.environ.get('ANDROID_NDK_HOME', sdk / 'ndk/27.2.12479018'))
    clang = ndk / 'toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe'
    flags = ['--target=aarch64-linux-android29', '-O1', '-DRECOMP_X87_WINDOW', '-DRECOMP_TRACE', '-DRECOMP_LOCAL_REGS',
             '-Iandroid/runtime', '-Iandroid/host', '-Ibuild/host-smoke', '-I' + str(generated)]
    own = []
    for index, source in enumerate([ROOT / 'tests/test_cmpxchg8b.c', ROOT / 'android/host/core.c', *sorted(generated.glob('*.c'))]):
        obj = output / f'test-{index}.o'
        subprocess.run([str(clang), *flags, '-c', str(source), '-o', str(obj)], cwd=ROOT, check=True)
        own.append('"' + obj.as_posix() + '"')
    objects = (ROOT / 'build/host-smoke/test-pe-imports.rsp').read_text().splitlines()[1:]
    objects = [path for path in objects if not path.endswith(('/dispatch.o"', '/core.o"'))]
    response = output / 'test.rsp'; response.write_text('\n'.join(own + objects) + '\n')
    binary = output / 'test-cmpxchg8b'
    subprocess.run([str(clang), '--target=aarch64-linux-android29', '-Wl,-z,nostart-stop-gc',
                    '@' + str(response), '-o', str(binary), '-lm', '-ldl', '-llog', '-landroid',
                    '-lEGL', '-lGLESv3', '-laaudio', '-lz'], cwd=ROOT, check=True)
    adb = [str(sdk / 'platform-tools/adb.exe'), '-s', os.environ['UT3_TEST_ANDROID']]
    remote = '/data/local/tmp/ut3-test-cmpxchg8b'
    for local, target in ((binary, remote), (image, remote + '.dll'), (vectors, remote + '.bin')):
        subprocess.run([*adb, 'push', str(local), target], check=True)
    subprocess.run([*adb, 'shell', 'chmod', '755', remote], check=True)
    subprocess.run([*adb, 'shell', remote, remote + '.dll', remote + '.bin'], check=True)
    (ROOT / 'research/cmpxchg8b-test.json').write_text(json.dumps({
        'status': 'PASS', 'device': adb[-1], 'original_instruction': FORMS[0], 'owner': hex(owner.address),
        'vectors_per_backend': count, 'native_windows_x86_64_vectors': native_count,
        'forms': FORMS, 'arithmetic_flags': 'All 64 combinations, DF and ID preserved; ZF alone changed',
        'alignment_policy': 'Byte RMW under existing FIFO machine ownership; no yield; seq_cst fences; entire operand prevalidated RW',
        'scope_limit': 'Guest CR0.AM/alignment-check exceptions are not modeled by this runtime',
        'concurrency': '4 pthreads, 8000 successful updates per address, aligned/unaligned/split-page; failures retried',
        'faults': 'Matched and failed split-page read-only operand, 4GiB wrap, unmapped operand: checked trap and no partial write',
        'instruction_reference': 'https://cdrdv2-public.intel.com/835757/325383-sdm-vol-2abcd.pdf'}, indent=2) + '\n')


if __name__ == '__main__':
    main()
