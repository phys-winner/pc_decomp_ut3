"""Validate pinned CRT table contracts and execute original x86 versus lifted ARM64."""
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

import pefile
from unicorn import Uc, UC_ARCH_X86, UC_HOOK_CODE, UC_MODE_32
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EBX, UC_X86_REG_ECX,
                              UC_X86_REG_EDI, UC_X86_REG_EDX, UC_X86_REG_EFLAGS, UC_X86_REG_ESI,
                              UC_X86_REG_ESP)

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import lift_game
from pe_rebase import rebase_image


def main():
    spec = next(row for row in json.loads((ROOT / 'config/modules.json').read_text())['modules']
                if row['name'] == 'msvcr80')
    raw = Path(spec['source']).read_bytes()
    assert hashlib.sha256(raw).hexdigest() == spec['sha256']
    raw, _ = rebase_image(raw, int(spec['base'], 0))
    pe = pefile.PE(data=raw, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    tables = {}
    for row in spec['jump_tables']:
        if int(row['owner'], 0) != 0x10015320:
            continue
        address, first, count = int(row['address'], 0), row['first_index'], row['count']
        table = pe.get_data(address - base + first * 4, count * 4)
        assert hashlib.sha256(table).hexdigest() == row['sha256']
        tables[address] = (first, count)
    output = ROOT / 'build/crt-memmove-test'
    output.mkdir(parents=True, exist_ok=True)
    image = output / 'msvcr80.dll'
    image.write_bytes(raw)
    info = lift_game.analyze_pe(str(image))
    imports = lift_game.build_iat_map(info)
    decoder = lift_game.BoundedDisassembler(raw, base, info.sections, jump_tables=tables)
    functions = {address: decoder.disassemble_function(address, imports)
                 for address in (0x10015320, 0x10017424, 0x1001739D)}
    assert any(i.address == 0x10015614 for block in functions[0x10015320].blocks.values() for i in block.instructions)
    generated = output / 'generated'
    report = lift_game.emit_functions(generated, imports, decoder, functions, label='CRT memmove test', header_name='functions.h')
    assert not report['functions_containing_traps']
    lift_game.emit_dispatch(generated / 'dispatch.c', functions, ['functions.h'])
    cases = []
    for size in range(65):
        for alignment in range(16):
            cases.extend((size, alignment, delta, 0) for delta in range(-size - 1, size + 2))
    for size in (65, 66, 67, 127, 128, 129, 255, 256, 257, 511, 512, 513, 1023):
        distances = {0, 1, 2, 3, 15, 16, 17, 31, 32, 33, 63, 64, size - 1, size, size + 1}
        for alignment in range(16):
            for delta in sorted(distances | {-value for value in distances}):
                for sse in (0, 1):
                    cases.append((size, alignment, delta, sse))
    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    uc.mem_map(base, (pe.OPTIONAL_HEADER.SizeOfImage + 4095) & -4096)
    uc.mem_write(base, pe.get_memory_mapped_image())
    arena, stop, stack = 0x22000000, 0x400000, 0x308000
    uc.mem_map(arena, 8192); uc.mem_map(0x300000, 0x10000); uc.mem_map(stop, 4096)
    original = bytes((i * 73 + (i >> 4) * 17 + 0x35) & 255 for i in range(8192))
    observed = {address: set() for address in tables}
    sites = {0x10015395: (0x100153A8, UC_X86_REG_EAX), 0x1001539C: (0x100154A4, UC_X86_REG_ECX),
             0x100153A4: (0x10015428, UC_X86_REG_ECX), 0x1001537C: (0x10015494, UC_X86_REG_EDX),
             0x100153DA: (0x10015494, UC_X86_REG_EDX), 0x10015400: (0x10015494, UC_X86_REG_EDX),
             0x1001541E: (0x10015494, UC_X86_REG_EDX), 0x1001548B: (0x10015494, UC_X86_REG_EDX),
             0x10015503: (0x10015630, UC_X86_REG_EDX), 0x1001550E: (0x100155E0, UC_X86_REG_ECX),
             0x10015529: (0x10015534, UC_X86_REG_EAX), 0x10015530: (0x10015630, UC_X86_REG_ECX),
             0x1001555E: (0x10015630, UC_X86_REG_EDX), 0x10015588: (0x10015630, UC_X86_REG_EDX),
             0x100155BA: (0x10015630, UC_X86_REG_EDX), 0x10015627: (0x10015630, UC_X86_REG_EDX)}

    def branch(vm, address, size, _):
        table, register = sites[address]
        value = vm.reg_read(register)
        observed[table].add(value - 0x100000000 if value & 0x80000000 else value)

    for address in sites:
        uc.hook_add(UC_HOOK_CODE, branch, begin=address, end=address)
    for index, (size, alignment, delta, sse) in enumerate(cases):
        source = 4096 + alignment
        destination = source + delta
        expected = bytearray(original)
        expected[destination:destination + size] = original[source:source + size]
        uc.mem_write(arena, original)
        uc.mem_write(0x10095948, struct.pack('<I', sse))
        uc.mem_write(stack, struct.pack('<4I', stop, arena + destination, arena + source, size))
        for register, value in ((UC_X86_REG_ESP, stack), (UC_X86_REG_EBP, 0x71234567),
                                (UC_X86_REG_EBX, 0xB1234567), (UC_X86_REG_ESI, 0x51234567),
                                (UC_X86_REG_EDI, 0x61234567), (UC_X86_REG_EFLAGS, 0x246)):
            uc.reg_write(register, value)
        uc.emu_start(0x10015320, stop, count=100000)
        assert uc.mem_read(arena, 8192) == expected, (index, size, alignment, delta, sse)
        assert uc.reg_read(UC_X86_REG_EAX) == arena + destination
        assert uc.reg_read(UC_X86_REG_ESP) == stack + 4
        assert uc.reg_read(UC_X86_REG_EBP) == 0x71234567 and uc.reg_read(UC_X86_REG_EBX) == 0xB1234567
        assert uc.reg_read(UC_X86_REG_ESI) == 0x51234567 and uc.reg_read(UC_X86_REG_EDI) == 0x61234567
        assert not uc.reg_read(UC_X86_REG_EFLAGS) & 0x400
    for address, (first, count) in tables.items():
        assert observed[address] == set(range(first, first + count)), (hex(address), observed[address])
    vectors = output / 'vectors.bin'
    vectors.write_bytes(struct.pack('<I', len(cases)) + b''.join(struct.pack('<IIiI', *row) for row in cases))
    print('Original Unicorn PASS:', len(cases), 'copies; every declared table index executed', flush=True)
    sdk = Path(os.environ.get('ANDROID_SDK_ROOT', Path.home() / 'AppData/Local/Android/Sdk'))
    ndk = Path(os.environ.get('ANDROID_NDK_HOME', sdk / 'ndk/27.2.12479018'))
    clang = ndk / 'toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe'
    flags = ['--target=aarch64-linux-android29', '-O1', '-DRECOMP_X87_WINDOW', '-DRECOMP_TRACE',
             '-Iandroid/runtime', '-Iandroid/host', '-Ibuild/host-smoke', '-I' + str(generated)]
    own = []
    for number, source in enumerate([ROOT / 'tests/test_crt_memmove.c', *sorted(generated.glob('*.c'))]):
        obj = output / f'test-{number}.o'
        subprocess.run([str(clang), *flags, '-c', str(source), '-o', str(obj)], cwd=ROOT, check=True)
        own.append('"' + obj.as_posix() + '"')
    objects = (ROOT / 'build/host-smoke/test-pe-imports.rsp').read_text().splitlines()[1:]
    objects = [path for path in objects if not path.endswith('/dispatch.o"')]
    response = output / 'test.rsp'
    response.write_text('\n'.join(own + objects) + '\n')
    binary = output / 'test-crt-memmove'
    subprocess.run([str(clang), '--target=aarch64-linux-android29', '-Wl,-z,nostart-stop-gc',
                    '@' + str(response), '-o', str(binary), '-lm', '-ldl', '-llog', '-landroid',
                    '-lEGL', '-lGLESv3', '-laaudio', '-lz'], cwd=ROOT, check=True)
    adb = [str(sdk / 'platform-tools/adb.exe'), '-s', os.environ['UT3_TEST_ANDROID']]
    remote = '/data/local/tmp/ut3-test-crt-memmove'
    for local, target in [(binary, remote), (image, remote + '.dll'), (vectors, remote + '.bin')]:
        subprocess.run([*adb, 'push', str(local), target], check=True)
    subprocess.run([*adb, 'shell', 'chmod', '755', remote], check=True)
    subprocess.run([*adb, 'shell', remote, remote + '.dll', remote + '.bin'], check=True)


if __name__ == '__main__':
    main()
