"""Exercise every contracted CRT copy table index in original x86 and production ARM64."""
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

import pefile
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_REG_EAX, X86_REG_ECX, X86_REG_EDX
from unicorn import Uc, UC_ARCH_X86, UC_HOOK_CODE, UC_MODE_32
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EBX, UC_X86_REG_ECX,
                              UC_X86_REG_EDI, UC_X86_REG_EDX, UC_X86_REG_EFLAGS, UC_X86_REG_ESI,
                              UC_X86_REG_ESP)

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import lift_game
from pe_rebase import rebase_image

ARENA, ARENA_SIZE, STOP, STACK = 0x30000000, 16384, 0x400000, 0x308000
REGISTERS = {X86_REG_EAX: UC_X86_REG_EAX, X86_REG_ECX: UC_X86_REG_ECX, X86_REG_EDX: UC_X86_REG_EDX}
INITIAL = bytes((i * 73 + (i >> 4) * 17 + 0x35) & 255 for i in range(ARENA_SIZE))


def instructions(function):
    return sorted({i.address: i for b in function.blocks.values() for i in b.instructions}.values(),
                  key=lambda i: i.address)


def cases(has_sse):
    # Small paths cover every dword alignment, signed index, and unrolled copy count.
    for size in range(41):
        for alignment in range(4):
            for delta in (-128, -3, -2, -1, 0, 1, 2, 3, 128):
                yield size, alignment, delta, 0
    # Include exact threshold edges, all SSE alignments, overlap in both directions,
    # disjoint regions, and copies crossing a 4-KiB page boundary.
    for size in (255, 256, 257, 511, 512, 513, 1023, 1024, 2047, 2048, 2049):
        for alignment in (0, 1, 2, 3, 7, 15):
            distances = {0, 1, 16, 65, size - 1, size, size + 1}
            for delta in sorted(distances | {-value for value in distances}):
                for sse in ((0, 1) if has_sse else (0,)):
                    yield size, alignment, delta, sse


def oracle(pe, owner, function, tables):
    base = pe.OPTIONAL_HEADER.ImageBase
    machine = Uc(UC_ARCH_X86, UC_MODE_32)
    machine.mem_map(base, (pe.OPTIONAL_HEADER.SizeOfImage + 4095) & -4096)
    machine.mem_write(base, pe.get_memory_mapped_image())
    machine.mem_map(ARENA, ARENA_SIZE)
    machine.mem_map(0x300000, 0x10000)
    machine.mem_map(STOP, 4096)
    observed = {address: set() for address in tables}
    body = instructions(function)
    flags = [i.operands[0].mem.disp & 0xFFFFFFFF for i in body
             if owner <= i.address < owner + 0x60 and i.mnemonic == 'cmp' and
             len(i.operands) == 2 and i.operands[0].type == X86_OP_MEM and
             not i.operands[0].mem.base and not i.operands[0].mem.index and
             i.operands[1].type == X86_OP_IMM and i.operands[1].imm == 0]
    assert len(flags) <= 1
    flag = flags[0] if flags else 0
    helpers = [i.get_branch_target() for i in body
               if owner <= i.address < owner + 0x60 and i.mnemonic == 'jmp' and
               i.get_branch_target() is not None]
    assert len(helpers) == bool(flag), (hex(owner), helpers, flags)
    hits, active_sse = set(), [0]

    def helper_hit(vm, address, size, _):
        hits.add(active_sse[0])

    for helper in helpers:
        machine.hook_add(UC_HOOK_CODE, helper_hit, begin=helper, end=helper)
    sites = {}
    for instruction in body:
        if instruction.mnemonic != 'jmp':
            continue
        for operand in instruction.operands:
            if operand.type != X86_OP_MEM:
                continue
            memory = operand.mem
            table = memory.disp & 0xFFFFFFFF
            if table in tables:
                assert not memory.base and memory.scale == 4 and memory.index in REGISTERS
                sites[instruction.address] = (table, REGISTERS[memory.index])

    def branch(vm, address, size, _):
        table, register = sites[address]
        value = vm.reg_read(register)
        observed[table].add(value - 0x100000000 if value & 0x80000000 else value)

    for address in sites:
        machine.hook_add(UC_HOOK_CODE, branch, begin=address, end=address)
    vectors = []
    for size, alignment, delta, sse in cases(bool(flag)):
        source, destination = 8192 + alignment, 8192 + alignment + delta
        expected = bytearray(INITIAL)
        expected[destination:destination + size] = INITIAL[source:source + size]
        machine.mem_write(ARENA, INITIAL)
        if flag:
            machine.mem_write(flag, struct.pack('<I', sse))
        active_sse[0] = sse
        machine.mem_write(STACK, struct.pack('<4I', STOP, ARENA + destination, ARENA + source, size))
        for register, value in ((UC_X86_REG_ESP, STACK), (UC_X86_REG_EBP, 0x71234567),
                                (UC_X86_REG_EBX, 0xB1234567), (UC_X86_REG_ESI, 0x51234567),
                                (UC_X86_REG_EDI, 0x61234567), (UC_X86_REG_EFLAGS, 0x246)):
            machine.reg_write(register, value)
        machine.emu_start(owner, STOP, count=100000)
        context = hex(owner), size, alignment, delta, sse
        assert machine.mem_read(ARENA, ARENA_SIZE) == expected, context
        assert machine.reg_read(UC_X86_REG_EAX) == ARENA + destination, context
        assert machine.reg_read(UC_X86_REG_ESP) == STACK + 4, context
        assert machine.reg_read(UC_X86_REG_EBP) == 0x71234567, context
        assert machine.reg_read(UC_X86_REG_EBX) == 0xB1234567, context
        assert machine.reg_read(UC_X86_REG_ESI) == 0x51234567, context
        assert machine.reg_read(UC_X86_REG_EDI) == 0x61234567, context
        assert not machine.reg_read(UC_X86_REG_EFLAGS) & 0x400, context
        vectors.append((owner, flag, size, alignment, delta, sse))
    for address, (first, count) in tables.items():
        assert observed[address] == set(range(first, first + count)), (hex(owner), hex(address), observed[address])
    assert hits == ({1} if flag else set()), (hex(owner), hits)
    return vectors, {'owner': hex(owner), 'cases': len(vectors), 'sse_flag': hex(flag) if flag else None,
                     'sse_helpers': [hex(a) for a in helpers],
                     'table_indices': {hex(a): sorted(values) for a, values in observed.items()}}


def main():
    inputs = json.loads((ROOT / 'config/inputs.json').read_text())
    specs = json.loads((ROOT / 'config/modules.json').read_text())['modules']
    owners = json.loads((ROOT / 'research/crt-table-analogs.json').read_text())['functions']
    output = ROOT / 'build/crt-family-test'
    generated = output / 'generated'
    generated.mkdir(parents=True, exist_ok=True)
    fixtures, addresses, headers, sources, vectors, results = [], [], [], [], [], []
    for spec in specs:
        members = [row for row in owners if row['module'] == spec['name']]
        if not members:
            continue
        source = Path(spec['source']) if 'source' in spec else Path(inputs['source_root']) / spec['path']
        raw = source.read_bytes()
        assert hashlib.sha256(raw).hexdigest() == spec['sha256']
        raw, _ = rebase_image(raw, int(spec['base'], 0))
        pe = pefile.PE(data=raw, fast_load=True)
        tables = {}
        for row in spec['jump_tables']:
            address, first, count = int(row['address'], 0), row['first_index'], row['count']
            payload = pe.get_data(address - pe.OPTIONAL_HEADER.ImageBase + first * 4, count * 4)
            assert hashlib.sha256(payload).hexdigest() == row['sha256']
            tables[address] = (first, count)
        image = output / spec['file']
        image.write_bytes(raw)
        fixtures.append((image, pe.OPTIONAL_HEADER.ImageBase))
        info = lift_game.analyze_pe(str(image))
        imports = lift_game.build_iat_map(info)
        decoder = lift_game.BoundedDisassembler(raw, info.image_base, info.sections, jump_tables=tables)
        functions, pending = {}, [int(row['owner'], 0) for row in members]
        while pending:
            address = pending.pop()
            if address in functions:
                continue
            function = decoder.disassemble_function(address, imports)
            assert function and function.blocks
            functions[address] = function
            for target in lift_game.function_dispatch_targets(function):
                assert decoder.is_code_address(target), (hex(address), hex(target))
                if target not in functions:
                    pending.append(target)
            assert len(functions) <= 4  # Two copy bodies and, where present, their two SSE helpers.
        header = spec['name'] + '.h'
        report = lift_game.emit_functions(generated, imports, decoder, functions,
                                         label=spec['name'] + ' CRT copy test', header_name=header,
                                         chunk_prefix=spec['name'])
        assert not report['functions_containing_traps']
        addresses.extend(functions)
        headers.append(header)
        sources.extend(generated / name for name in report['sources'])
        for member in members:
            owner = int(member['owner'], 0)
            declared = {int(row['address'], 0): (row['first_index'], row['count'])
                        for row in spec['jump_tables'] if int(row['owner'], 0) == owner}
            assert len(declared) == 7
            original_vectors, result = oracle(pe, owner, functions[owner], declared)
            vectors.extend(original_vectors)
            result['module'] = spec['name']
            results.append(result)
            print('Original Unicorn PASS:', spec['name'], hex(owner), len(original_vectors), flush=True)
        pe.close()
    assert len(results) == len(owners) == 20 and len(fixtures) == 10
    lift_game.emit_dispatch(generated / 'dispatch.c', addresses, headers)
    sources.append(generated / 'dispatch.c')
    (generated / 'fixtures.h').write_text('#include <stdint.h>\nstatic const struct { const char *file; uint32_t base; } fixtures[] = {\n' +
        ''.join(f'    {{"{image.name}", 0x{base:08X}u}},\n' for image, base in fixtures) + '};\n')
    vector_file = output / 'vectors.bin'
    vector_file.write_bytes(struct.pack('<I', len(vectors)) +
                            b''.join(struct.pack('<IIIIiI', *row) for row in vectors))
    sdk = Path(os.environ.get('ANDROID_SDK_ROOT', Path.home() / 'AppData/Local/Android/Sdk'))
    ndk = Path(os.environ.get('ANDROID_NDK_HOME', sdk / 'ndk/27.2.12479018'))
    clang = ndk / 'toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe'
    flags = ['--target=aarch64-linux-android29', '-O1', '-DRECOMP_X87_WINDOW', '-DRECOMP_TRACE',
             '-Iandroid/runtime', '-Iandroid/host', '-Ibuild/host-smoke', '-I' + str(generated)]
    own = []
    for index, source in enumerate([ROOT / 'tests/test_crt_copy_family.c', *sources]):
        obj = output / f'test-{index}.o'
        subprocess.run([str(clang), *flags, '-c', str(source), '-o', str(obj)], cwd=ROOT, check=True)
        own.append('"' + obj.as_posix() + '"')
    objects = (ROOT / 'build/host-smoke/test-pe-imports.rsp').read_text().splitlines()[1:]
    objects = [path for path in objects if not path.endswith('/dispatch.o"')]
    response = output / 'test.rsp'
    response.write_text('\n'.join(own + objects) + '\n')
    binary = output / 'test-crt-copy-family'
    subprocess.run([str(clang), '--target=aarch64-linux-android29', '-Wl,-z,nostart-stop-gc',
                    '@' + str(response), '-o', str(binary), '-lm', '-ldl', '-llog', '-landroid',
                    '-lEGL', '-lGLESv3', '-laaudio', '-lz'], cwd=ROOT, check=True)
    adb = [str(sdk / 'platform-tools/adb.exe'), '-s', os.environ['UT3_TEST_ANDROID']]
    remote = '/data/local/tmp/ut3-test-crt-family'
    subprocess.run([*adb, 'shell', 'mkdir', '-p', remote], check=True)
    for image, _ in fixtures:
        subprocess.run([*adb, 'push', str(image), remote + '/' + image.name], check=True)
    subprocess.run([*adb, 'push', str(binary), remote + '/test'], check=True)
    subprocess.run([*adb, 'push', str(vector_file), remote + '/vectors.bin'], check=True)
    subprocess.run([*adb, 'shell', 'chmod', '755', remote + '/test'], check=True)
    subprocess.run([*adb, 'shell', remote + '/test', remote, remote + '/vectors.bin'], check=True)
    (ROOT / 'research/crt-copy-family-test.json').write_text(json.dumps({
        'status': 'PASS', 'device': adb[-1], 'owners': len(results), 'modules': len(fixtures),
        'copies_per_backend': len(vectors), 'functions': results,
        'checks': ['original module SHA-256', 'rebased table payload SHA-256',
                   'every declared index executed', 'SSE helper entered only with capability enabled',
                   'whole 16-KiB arena', 'return pointer', 'callee-saved registers', 'cdecl stack',
                   'original x86 DF clear']}, indent=2) + '\n')


if __name__ == '__main__':
    main()
