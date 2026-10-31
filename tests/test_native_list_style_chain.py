"""Pinned original cell style -> string style copy -> full formatter oracle.

Only fixture state/style resolution and font/node boundaries are supplied.
There is no game bootstrap, production hook, APK build, or device access.
"""
import hashlib
import json
from pathlib import Path
import struct
import unittest
from collections import deque

import pefile
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE, UC_HOOK_MEM_INVALID
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDI, UC_X86_REG_EIP, UC_X86_REG_ESP

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/ui-list-style-chain'
BASE = 0x400000
HEAP = 0x4000000
STOP = 0x100000


class NativeListStyleChain(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        inputs = json.loads((ROOT / 'config/inputs.json').read_text())
        path = Path(inputs['source_root']) / 'Binaries/UT3.exe'
        cls.original_sha = hashlib.sha256(path.read_bytes()).hexdigest()
        assert cls.original_sha == next(x['sha256'] for x in inputs['modules'] if x['path'] == 'Binaries/UT3.exe')
        cls.pe = pefile.PE(str(path), fast_load=True)
        cls.results = []
        cls.pages = set()

    @staticmethod
    def word(vm, address):
        return struct.unpack('<I', vm.mem_read(address, 4))[0]

    @staticmethod
    def put(vm, address, *values):
        vm.mem_write(address, struct.pack('<' + 'I' * len(values), *values))

    def fixture(self, baseline_x, baseline_y, factor, inject, mode=0, adjust=False):
        vm = Uc(UC_ARCH_X86, UC_MODE_32)
        mapped = set()
        recent = deque(maxlen=16)

        def original_page(address):
            page = address & ~0xfff
            if page in mapped:
                return True
            if not BASE <= page < BASE + self.pe.OPTIONAL_HEADER.SizeOfImage:
                return False
            vm.mem_map(page, 0x1000)
            vm.mem_write(page, self.pe.get_data(page - BASE, 0x1000).ljust(0x1000, b'\0'))
            mapped.add(page)
            self.pages.add(page)
            return True

        def invalid(vm, access, address, size, value, opaque):
            del vm, access, size, value, opaque
            return original_page(address)

        vm.hook_add(UC_HOOK_MEM_INVALID, invalid)
        for page, size in ((0, 0x1000), (STOP, 0x1000), (0x300000, 0x10000), (HEAP, 0x10000), (0x5000000, 0x10000)):
            vm.mem_map(page, size)
        # String layout follows the independently tested complete formatter fixture.
        string, node, node_vt, bounds = HEAP, HEAP+0x1000, HEAP+0x2000, HEAP+0x3000
        string_vt, payload_at, array = HEAP+0x4000, HEAP+0x5000, HEAP+0x800
        cell, owner, presenter, row, owner_vt = (HEAP+x for x in (0x7000, 0x8000, 0x9000, 0xA000, 0xB000))
        resolved, text, font = HEAP+0xC000, HEAP+0xD000, HEAP+0xE000
        provider = HEAP+0xF000
        self.put(vm, cell, 0, owner, string)
        self.put(vm, owner, owner_vt)
        self.put(vm, owner+0x620, presenter)
        self.put(vm, owner_vt+0x2E4, STOP+0x300)
        self.put(vm, presenter+0x58, row, 1, 1)
        self.put(vm, row+0x10, 0)
        self.put(vm, string, string_vt)
        self.put(vm, string_vt+0x118, STOP+0x140)
        self.put(vm, string_vt+0x110, STOP+0x160)
        self.put(vm, string+0x3C, array, 1, 1)
        self.put(vm, array, node)
        self.put(vm, node, node_vt)
        for offset, method in ((0x2C, STOP+0x110), (0x20, STOP+0x120), (0xC, STOP+0x200),
                               (4, 0xF70640), (8, 0xF70ED0)):
            self.put(vm, node_vt+offset, method)
        vm.mem_write(node+0x18, struct.pack('<ffff', 100, 20, 1, 1))
        vm.mem_write(bounds, struct.pack('<6f', 0, 0, 600, 80, baseline_x, baseline_y))
        vm.mem_write(bounds+0x40, struct.pack('<f', 1))
        payload = 'Vulkan (DXVK / Turnip)\0'.encode('utf-16-le')
        vm.mem_write(payload_at, payload)
        self.put(vm, node+0x2C, payload_at, len(payload)//2, len(payload)//2)
        raw_provider = b'Strings\0OpenGL ES\0Vulkan (DXVK)\0Vulkan (DXVK / Turnip)\0'
        vm.mem_write(provider, raw_provider)
        self.put(vm, resolved+0x64, 0x12345678)
        self.put(vm, resolved+0x84, 0x23456789)
        # A resolved native text style supplies the real base policy. Its font
        # pointer and color/scaling fields are copied by original F31560.
        self.put(vm, text+0x64, font)
        vm.mem_write(text+0x50, struct.pack('<4f', .2, .4, .6, 1))
        vm.mem_write(text+0x70, struct.pack('<fIff', 1, mode, 1, 1))
        # Mimic the old script pass: the string temporarily has enlarged Normal
        # policy before original ApplyCellStyleData overwrites it.
        self.put(vm, string+0x78, font)
        self.put(vm, string+0xD0, 1)
        vm.mem_write(string+0xB8, struct.pack('<fI', 1.5, 1))
        vm.mem_write(string+0xDC, struct.pack('<ffI', 1.5, 1.5, int(adjust)))
        self.put(vm, HEAP+0x6F00, 0x42C80000, 0x41A00000)
        measure = (bytes.fromhex('f30f104120f30f5905') + struct.pack('<I', HEAP+0x6F00) +
                   bytes.fromhex('f30f114118f30f104124f30f5905') + struct.pack('<I', HEAP+0x6F04) +
                   bytes.fromhex('f30f11411cc20800'))
        vm.mem_write(STOP+0x200, measure)
        original_page(0x1729690)
        self.put(vm, 0x1729690, STOP+0x150)
        original_page(0x1DAC458)
        allocator, allocator_vt = HEAP+0x6800, HEAP+0x6900
        self.put(vm, 0x1DAC458, allocator);self.put(vm, allocator, allocator_vt)
        self.put(vm, allocator_vt+8, STOP+0x400);self.put(vm, allocator_vt+12, STOP+0x410)
        allocated = {}
        arena_next = 0x5000000
        calls = []
        policy = None
        entered = 0

        def finish(result=0, pop=0):
            sp = vm.reg_read(UC_X86_REG_ESP)
            vm.reg_write(UC_X86_REG_EAX, result & 0xffffffff)
            vm.reg_write(UC_X86_REG_EIP, self.word(vm, sp))
            vm.reg_write(UC_X86_REG_ESP, sp+4+pop)

        def instruction(vm, address, size, opaque):
            nonlocal policy, entered, arena_next
            del size, opaque
            sp, receiver = vm.reg_read(UC_X86_REG_ESP), vm.reg_read(UC_X86_REG_ECX)
            recent.append(address)
            if address in (0x10987F0, 0xF77D40, 0xF31560, 0xF77C70, 0xF77D12, 0xF70640):
                calls.append(hex(address))
            if address == 0xF2F8F0:  # fixture-owned cell style resolution
                finish(resolved, 8)
            elif address == 0xB55380:
                raise AssertionError(f'Original assertion return={self.word(vm,sp):08x} args={[hex(self.word(vm,sp+x)) for x in (4,8,12,16)]}')
            elif address == STOP+0x400:  # fixture-owned TArray realloc
                pointer, count = self.word(vm, sp+4), self.word(vm, sp+8)
                self.assertLessEqual(count, 0x4000)
                output = arena_next;arena_next += (count+15)&~15
                self.assertLess(arena_next, 0x5010000)
                if pointer and count:
                    vm.mem_write(output, bytes(vm.mem_read(pointer, min(count, allocated[pointer]))))
                allocated[output] = count;finish(output, 12)
            elif address == STOP+0x410:
                finish(0, 4)
            elif address == STOP+0x300:  # current UI state
                finish(0x34567890, 4)
            elif address == 0x1096890:  # current state to style state
                finish(0x34567890, 4)
            elif address == 0xF30C10:  # resolved style data for fixture state
                finish(resolved, 4)
            elif address == 0x4BA740:  # UUIStyle_Combo class resolution boundary
                finish(resolved, 0)
            elif address == 0xF312C0:  # text/image references of the resolved combo
                finish(text if receiver == resolved+0x84 else 0, 0)
            elif address == 0x4F4BE0:  # UUIStyle_Text class resolution
                finish(text if self.word(vm, sp+4) == text else 0, 0)
            elif address == 0x4F4C40:  # no image style in this text-only fixture
                finish(0, 0)
            elif address == 0xF772D0:  # execute original existing-node propagation
                calls.append('real_node_style_propagation')
            elif address == 0xF73D90:  # unit fixture font resolution, returns x87 1
                vm.mem_write(STOP+0x100, bytes.fromhex('d9e8c20400'))
                vm.reg_write(UC_X86_REG_EIP, STOP+0x100)
            elif address in (STOP+0x110, STOP+0x140, STOP+0x150, STOP+0x160):
                finish(owner if address == STOP+0x160 else 0, 4 if address == STOP+0x140 else 0)
            elif address == STOP+0x120:
                finish(1, 0)
            elif address == 0xF75DB0:
                entered += 1
                mode_now = self.word(vm, string+0xBC) & 0xff
                if inject and mode_now == 0 and baseline_x == baseline_y and baseline_x > 0 and factor > 1:
                    policy = bytes(vm.mem_read(string+0xB8, 8)), self.word(vm, string+0xE4)
                    scaled = baseline_x * factor
                    vm.mem_write(string+0xB8, struct.pack('<fI', scaled, 1))
                    vm.mem_write(string+0xDC, struct.pack('<ff', scaled, scaled))
                    self.put(vm, string+0xE4, self.word(vm, string+0xE4) | 1)

        vm.hook_add(UC_HOOK_CODE, instruction)
        self.put(vm, 0x308000, STOP, 0)
        vm.reg_write(UC_X86_REG_ESP, 0x308000)
        vm.reg_write(UC_X86_REG_ECX, cell)
        try:
            vm.emu_start(0x10987F0, STOP, count=200000)
        except Exception as error:
            raise AssertionError(f'Apply chain stopped EIP={vm.reg_read(UC_X86_REG_EIP):08x}, recent={[hex(x) for x in recent]}: {error}') from error
        self.assertEqual(vm.reg_read(UC_X86_REG_EIP), STOP, 'Apply chain instruction limit')
        self.assertEqual(calls[:5], ['0x10987f0', '0xf77d40', '0xf31560', '0xf77c70', '0xf77d12'])
        self.assertEqual(self.word(vm, string+0xBC) & 0xff, mode)
        self.assertAlmostEqual(struct.unpack('<f', vm.mem_read(string+0xB8, 4))[0], 1)
        self.assertEqual(self.word(vm, string+0x78), font)
        after_copy = bytes(vm.mem_read(string+0x48, 0x8C))
        self.assertEqual(calls.count('real_node_style_propagation'), 1)
        self.assertEqual(calls.count('0xf70640'), 1)
        self.assertEqual(self.word(vm, node+0x68), font)
        node_style = bytes(vm.mem_read(node+0x38, 0x8C))
        self.assertEqual(node_style, after_copy)
        array_before = bytes(vm.mem_read(string+0x3C, 12)) + bytes(vm.mem_read(array, 4))
        source_before = bytes(vm.mem_read(node+0xC, 12)) + bytes(vm.mem_read(node+0x2C, 12))
        self.put(vm, 0x308000, STOP, bounds, 0)
        vm.reg_write(UC_X86_REG_ESP, 0x308000)
        vm.reg_write(UC_X86_REG_ECX, string)
        try:
            vm.emu_start(0xF75DB0, STOP, count=200000)
        except Exception as error:
            raise AssertionError(f'Formatter stopped EIP={vm.reg_read(UC_X86_REG_EIP):08x}: {error}') from error
        self.assertEqual(vm.reg_read(UC_X86_REG_EIP), STOP, 'Formatter instruction limit')
        if policy:
            vm.mem_write(string+0xB8, policy[0]);self.put(vm, string+0xE4, policy[1])
        self.assertEqual(entered, 1)
        self.assertEqual(bytes(vm.mem_read(string+0x48, 0x8C)), after_copy)
        self.assertEqual(bytes(vm.mem_read(node+0x38, 0x8C)), node_style)
        self.assertEqual(bytes(vm.mem_read(string+0x3C, 12)) + bytes(vm.mem_read(array, 4)), array_before)
        self.assertEqual(bytes(vm.mem_read(node+0xC, 12)) + bytes(vm.mem_read(node+0x2C, 12)), source_before)
        self.assertEqual(bytes(vm.mem_read(payload_at, len(payload))), payload)
        self.assertEqual(bytes(vm.mem_read(provider, len(raw_provider))), raw_provider)
        result = {'baseline': [baseline_x, baseline_y], 'factor': factor, 'inject_requested': inject,
                  'injected': bool(policy), 'mode': mode, 'adjust': adjust,
                  'string_scale': struct.unpack('<ff', vm.mem_read(string+0xDC, 8)),
                  'node_extent_scale': struct.unpack('<ffff', vm.mem_read(node+0x18, 16)),
                  'calls': calls, 'formatter_calls': entered,
                  'preserved_style_sha256': hashlib.sha256(after_copy).hexdigest(),
                  'preserved_node_style_sha256': hashlib.sha256(node_style).hexdigest(),
                  'final_string_bytes_sha256': hashlib.sha256(bytes(vm.mem_read(string, 0xE8))).hexdigest(),
                  'final_node_bytes_sha256': hashlib.sha256(bytes(vm.mem_read(node, 0xC4))).hexdigest()}
        self.results.append(result)
        return result

    def test_copy_overwrites_script_policy_and_entry_override_is_relative(self):
        for adjust in (False, True):
            for baseline in (.75, 1, 1.25):
                original = self.fixture(baseline, baseline, 1, False, adjust=adjust)
                for factor in (1.01, 1.26, 1.5):
                    result = self.fixture(baseline, baseline, factor, True, adjust=adjust)
                    self.assertTrue(result['injected'])
                    for before, after in zip(original['node_extent_scale'], result['node_extent_scale']):
                        self.assertAlmostEqual(after, before*factor, places=4)

    def test_default_non_none_and_anisotropic_bypass(self):
        for x, y, mode, factor in ((1,1,0,1), (1,1,2,1.5), (.75,1.25,0,1.5)):
            original = self.fixture(x, y, factor, False, mode)
            bypass = self.fixture(x, y, factor, True, mode)
            self.assertFalse(bypass['injected'])
            self.assertEqual(original['string_scale'], bypass['string_scale'])
            self.assertEqual(original['node_extent_scale'], bypass['node_extent_scale'])
            self.assertEqual(original['final_string_bytes_sha256'], bypass['final_string_bytes_sha256'])
            self.assertEqual(original['final_node_bytes_sha256'], bypass['final_node_bytes_sha256'])

    @classmethod
    def tearDownClass(cls):
        OUT.mkdir(parents=True, exist_ok=True)
        report = {'original_pe_sha256': cls.original_sha,
                  'source_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  'scope': 'Scratch original x86 cell ApplyCellStyleData -> actual140B style copy -> actual existing-node propagation F772D0/F70640 -> full original formatter; only state/style resolution, generic fixture allocation and fixed font/type/measurement boundaries supplied; no production/device operations',
                  'original_mapped_pages_sha256': {hex(p):hashlib.sha256(cls.pe.get_data(p-BASE,0x1000).ljust(0x1000,b'\0')).hexdigest() for p in sorted(cls.pages)},
                  'results': cls.results}
        (OUT/'receipt.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')


if __name__ == '__main__':
    unittest.main()
