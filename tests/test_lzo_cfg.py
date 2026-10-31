"""Pin the 768-way UE3 LZO CFG and compare original x86 with production ARM64."""
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

import pefile
from unicorn import Uc, UC_ARCH_X86, UC_HOOK_CODE, UC_MODE_32
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EBX, UC_X86_REG_EDI,
                              UC_X86_REG_EDX, UC_X86_REG_EFLAGS, UC_X86_REG_ESI, UC_X86_REG_ESP)

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import lift_game

OWNER, TABLE, DISPATCH = 0x01579230, 0x0157E608, 0x015792F9
INPUT, OUTPUT, LENGTH, REGION, STACK, STOP = 0x30000000, 0x31000000, 0x32000000, 0x40000, 0x308000, 0x100000
PACKAGES = {
    'Core.u': '1384474da4bed048b1b3df3bdad0d2b9e0a1aa4d6eeadca2dde894b9f9171993',
    'Engine.u': '5f08a77ea18fc4a84cafd6832066231ad5c230b034d008f964bf4b5047cb8983',
    'UTGame.u': '4afc01d750a21a33d2ab1dcf0cb7b914fb4be11f2c4f265b5aef0d8919a82927',
    'Maps/DM-Deck.ut3': 'be45d3c308f9b63d9a7785751e55a8e02f9c30655c895f59002e3edd55cec566',
}


def fixture_blocks():
    source = Path(json.loads((ROOT / 'config/inputs.json').read_text())['source_root']) / 'UTGame/CookedPC'
    for name, checksum in PACKAGES.items():
        raw = (source / name).read_bytes()
        assert hashlib.sha256(raw).hexdigest() == checksum, name
        header = raw.find(struct.pack('<I', 0x9E2A83C1), 1)
        assert header >= 0
        magic, block_size, compressed_total, output_total = struct.unpack_from('<4I', raw, header)
        assert magic == 0x9E2A83C1 and block_size == 0x20000
        count = (output_total + block_size - 1) // block_size
        entries = [struct.unpack_from('<2I', raw, header + 16 + 8 * i) for i in range(count)]
        assert sum(c for c, _ in entries) == compressed_total and sum(u for _, u in entries) == output_total
        position = header + 16 + 8 * count
        for index, (compressed, uncompressed) in enumerate(entries[:2]):
            assert 0 < compressed < REGION - 128 and 0 < uncompressed <= block_size
            payload = raw[position:position + compressed]
            assert len(payload) == compressed
            yield f'{name}:block{index}', payload, uncompressed, position
            position += compressed


def oracle(machine, payload, capacity, source_alignment, output_alignment):
    source = bytes([0xCC]) * REGION
    machine.mem_write(INPUT, source)
    machine.mem_write(INPUT + 64 + source_alignment, payload)
    before_source = machine.mem_read(INPUT, REGION)
    machine.mem_write(OUTPUT, bytes([0xA5]) * REGION)
    machine.mem_write(LENGTH, bytes([0xA5]) * 16)
    machine.mem_write(LENGTH + 4, struct.pack('<I', capacity))
    machine.mem_write(STACK, struct.pack('<6I', STOP, INPUT + 64 + source_alignment, len(payload),
                                         OUTPUT + 64 + output_alignment, LENGTH + 4, 0))
    for register, value in ((UC_X86_REG_ESP, STACK), (UC_X86_REG_EBP, 0x71234567),
                            (UC_X86_REG_EBX, 0xB1234567), (UC_X86_REG_ESI, 0x51234567),
                            (UC_X86_REG_EDI, 0x61234567), (UC_X86_REG_EFLAGS, 0x246)):
        machine.reg_write(register, value)
    machine.emu_start(OWNER, STOP, count=20000000)
    result = machine.reg_read(UC_X86_REG_EAX)
    size = struct.unpack('<I', machine.mem_read(LENGTH + 4, 4))[0]
    assert size <= capacity
    assert machine.mem_read(INPUT, REGION) == before_source
    assert machine.mem_read(LENGTH, 4) == b'\xA5' * 4 and machine.mem_read(LENGTH + 8, 8) == b'\xA5' * 8
    output = machine.mem_read(OUTPUT, REGION)
    assert output[:64 + output_alignment] == b'\xA5' * (64 + output_alignment)
    assert output[64 + output_alignment + capacity:] == b'\xA5' * (REGION - 64 - output_alignment - capacity)
    assert machine.reg_read(UC_X86_REG_ESP) == STACK + 4
    for register, value in ((UC_X86_REG_EBP, 0x71234567), (UC_X86_REG_EBX, 0xB1234567),
                            (UC_X86_REG_ESI, 0x51234567), (UC_X86_REG_EDI, 0x61234567)):
        assert machine.reg_read(register) == value
    assert not machine.reg_read(UC_X86_REG_EFLAGS) & 0x400
    return result, size, bytes(output)


def main():
    spec = next(row for row in json.loads((ROOT / 'config/modules.json').read_text())['modules'] if row['name'] == 'ut3')
    game = Path(json.loads((ROOT / 'config/inputs.json').read_text())['source_root'])
    image = game / spec['path']
    raw = image.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == spec['sha256']
    info = lift_game.analyze_pe(str(image))
    imports = lift_game.build_iat_map(info)
    contract = next(row for row in spec['jump_tables'] if int(row['address'], 0) == TABLE)
    assert (contract['first_index'], contract['count'], int(contract['owner'], 0)) == (0, 768, OWNER)
    pe = pefile.PE(data=raw, fast_load=True)
    payload = pe.get_data(TABLE - info.image_base, 768 * 4)
    assert hashlib.sha256(payload).hexdigest() == contract['sha256']
    decoder = lift_game.BoundedDisassembler(raw, info.image_base, info.sections, jump_tables={TABLE: (0, 768)})
    jump = next(decoder.md.disasm(decoder.read_bytes(DISPATCH, 7), DISPATCH))
    assert len(decoder.jump_table_targets(jump)) == 768
    legacy = lift_game.BoundedDisassembler(raw, info.image_base, info.sections)
    assert len(legacy.jump_table_targets(jump)) == 256
    for first, count in ((-4097, 1), (0, 4097), (4095, 2), (4096, 1), (0, 0)):
        try:
            lift_game.BoundedDisassembler(raw, info.image_base, info.sections, jump_tables={TABLE: (first, count)})
        except ValueError:
            pass
        else:
            raise AssertionError((first, count))
    function = decoder.disassemble_function(OWNER, imports)
    addresses = {i.address for b in function.blocks.values() for i in b.instructions}
    assert 0x01579405 in addresses
    assert set(struct.unpack('<768I', payload)) <= addresses
    assert not function.calls_to and not lift_game.function_dispatch_targets(function)
    output = ROOT / 'build/lzo-cfg-test'
    generated = output / 'generated'
    report = lift_game.emit_functions(generated, imports, decoder, {OWNER: function}, label='LZO CFG test', header_name='functions.h')
    assert not report['functions_containing_traps']
    lift_game.emit_dispatch(generated / 'dispatch.c', [OWNER], ['functions.h'])
    machine = Uc(UC_ARCH_X86, UC_MODE_32)
    machine.mem_map(info.image_base, (pe.OPTIONAL_HEADER.SizeOfImage + 4095) & -4096)
    machine.mem_write(info.image_base, pe.get_memory_mapped_image())
    for address, size in ((INPUT, REGION), (OUTPUT, REGION), (LENGTH, 4096), (0x300000, 0x10000), (STOP, 4096)):
        machine.mem_map(address, size)
    seen = set()

    def dispatch(vm, address, size, _):
        seen.add(vm.reg_read(UC_X86_REG_EDX))

    machine.hook_add(UC_HOOK_CODE, dispatch, begin=DISPATCH, end=DISPATCH)
    fixtures = list(fixture_blocks())
    cases = []
    for name, data, size, offset in fixtures:
        for source_alignment, output_alignment in ((0, 0), (1, 3), (3, 1)):
            cases.append((name, data, size, source_alignment, output_alignment, True))
    first_name, first, first_size, _ = fixtures[0]
    for count in (0, 1, 2, 3, 4, 8, 16, len(first) // 2, len(first) - 3, len(first) - 1):
        cases.append((f'{first_name}:truncated{count}', first[:count], first_size, 0, 0, False))
    for capacity in (0, 1, 5, first_size // 2, first_size - 1):
        cases.append((f'{first_name}:capacity{capacity}', first, capacity, 1, 3, False))
    cases.append((f'{first_name}:trailing', first + b'\x00', first_size, 0, 0, False))
    vector_file = output / 'vectors.bin'
    results = []
    with vector_file.open('wb') as file:
        file.write(struct.pack('<I', len(cases)))
        for name, data, capacity, source_alignment, output_alignment, valid in cases:
            result, size, expected = oracle(machine, data, capacity, source_alignment, output_alignment)
            assert (result == 0 and size == capacity) if valid else result != 0, (name, result, size)
            file.write(struct.pack('<6I', len(data), capacity, source_alignment, output_alignment, result, size))
            file.write(data); file.write(expected)
            results.append({'name': name, 'compressed_size': len(data), 'capacity': capacity,
                            'alignment': [source_alignment, output_alignment], 'return_code': struct.unpack('<i', struct.pack('<I', result))[0],
                            'output_size': size, 'whole_output_sha256': hashlib.sha256(expected).hexdigest()})
    assert 0x103 in seen and any(index >= 0x200 for index in seen)
    print('Original LZO Unicorn PASS:', len(cases), 'cases;', len(seen), 'dispatch indices', flush=True)
    sdk = Path(os.environ.get('ANDROID_SDK_ROOT', Path.home() / 'AppData/Local/Android/Sdk'))
    ndk = Path(os.environ.get('ANDROID_NDK_HOME', sdk / 'ndk/27.2.12479018'))
    clang = ndk / 'toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe'
    flags = ['--target=aarch64-linux-android29', '-O1', '-DRECOMP_X87_WINDOW', '-DRECOMP_TRACE',
             '-Iandroid/runtime', '-Iandroid/host', '-Ibuild/host-smoke', '-I' + str(generated)]
    own = []
    for index, source in enumerate([ROOT / 'tests/test_lzo_cfg.c', *sorted(generated.glob('*.c'))]):
        obj = output / f'test-{index}.o'
        subprocess.run([str(clang), *flags, '-c', str(source), '-o', str(obj)], cwd=ROOT, check=True)
        own.append('"' + obj.as_posix() + '"')
    objects = (ROOT / 'build/host-smoke/test-pe-imports.rsp').read_text().splitlines()[1:]
    objects = [path for path in objects if not path.endswith('/dispatch.o"')]
    response = output / 'test.rsp'
    response.write_text('\n'.join(own + objects) + '\n')
    binary = output / 'test-lzo-cfg'
    subprocess.run([str(clang), '--target=aarch64-linux-android29', '-Wl,-z,nostart-stop-gc',
                    '@' + str(response), '-o', str(binary), '-lm', '-ldl', '-llog', '-landroid',
                    '-lEGL', '-lGLESv3', '-laaudio', '-lz'], cwd=ROOT, check=True)
    adb = [str(sdk / 'platform-tools/adb.exe'), '-s', os.environ['UT3_TEST_ANDROID']]
    remote = '/data/local/tmp/ut3-test-lzo-cfg'
    for local, target in ((binary, remote), (image, remote + '.exe'), (vector_file, remote + '.bin')):
        subprocess.run([*adb, 'push', str(local), target], check=True)
    subprocess.run([*adb, 'shell', 'chmod', '755', remote], check=True)
    subprocess.run([*adb, 'shell', remote, remote + '.exe', remote + '.bin'], check=True)
    (ROOT / 'research/lzo-cfg-blocker.json').write_text(json.dumps({
        'status': 'PASS', 'device': adb[-1], 'owner': hex(OWNER), 'contract': contract,
        'instructions': function.num_instructions, 'blocks': len(function.blocks), 'external_dispatch_targets': [],
        'all_table_targets_decoded': True, 'executed_table_indices': sorted(seen), 'cases': results,
        'fixtures': [{'package': name, 'payload_offset': offset, 'compressed_size': len(data),
                      'uncompressed_size': size, 'compressed_sha256': hashlib.sha256(data).hexdigest()}
                     for name, data, size, offset in fixtures]}, indent=2) + '\n')


if __name__ == '__main__':
    main()
