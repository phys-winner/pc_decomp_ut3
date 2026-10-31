"""Original PE owner consumers, independent of the production host constant."""
import hashlib
import json
from pathlib import Path
import struct
import unittest

import pefile
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_ESP

ROOT = Path(__file__).resolve().parents[1]


class OriginalOuterTest(unittest.TestCase):
    def test_pinned_getter_and_list_string_owner_consumer(self):
        cfg = json.loads((ROOT / 'config/inputs.json').read_text())
        path = Path(cfg['source_root']) / 'Binaries/UT3.exe'
        expected = next(x['sha256'] for x in cfg['modules'] if x['path'] == 'Binaries/UT3.exe')
        self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), expected)
        pe = pefile.PE(str(path), fast_load=True)
        base = pe.OPTIONAL_HEADER.ImageBase
        read = lambda address, count: pe.get_data(address - base, count)
        self.assertEqual(read(0xF70A60, 4), bytes.fromhex('8b4128c3'))
        # Actual UIListString constructor installs this vtable, whose slot110
        # consumes the object's owner before calling a widget method.
        self.assertEqual(read(0xF74C40, 6), bytes.fromhex('c706086aa901'))
        self.assertEqual(struct.unpack('<I', read(0x1A96A08 + 0x110, 4))[0], 0xF70BD0)
        self.assertEqual(read(0xF70BD0, 5), bytes.fromhex('8b49286aff'))
        call = read(0xF70BD5, 5)
        self.assertEqual(call[0], 0xE8)
        self.assertEqual(0xF70BDA + struct.unpack('<i', call[1:])[0], 0xFE4520)
        # Constructor independently seeds +20=-1, while the owner field is +28.
        self.assertEqual(read(0xB82832, 3), bytes.fromhex('895020'))
        self.assertEqual(read(0xB8285B, 3), bytes.fromhex('895028'))

        obj, owner = 0x13431600, 0x0F37D900
        # Numeric-only live header supplied by root's read-only PID10160 probe.
        words = [0xFFFFFFFF, 0xFFFFFFFF, owner, 0xCC9, 1, 0x028BEE80, 0x023A7100]
        machine = Uc(UC_ARCH_X86, UC_MODE_32)
        for address, size in [(0xF70000, 0x1000), (0xFE4000, 0x1000),
                              (0x13431000, 0x1000), (0x30000, 0x1000), (0x10000, 0x1000)]:
            machine.mem_map(address, size)
        machine.mem_write(0xF70000, read(0xF70000, 0x1000))
        machine.mem_write(obj + 0x20, struct.pack('<7I', *words))
        # Widget-call boundary echoes its actual ECX and cleans its one arg.
        machine.mem_write(0xFE4520, bytes.fromhex('8bc1c20400'))
        observed = []

        def hook(vm, address, size, data):
            if address == 0xFE4520:
                esp = vm.reg_read(UC_X86_REG_ESP)
                observed.append((vm.reg_read(UC_X86_REG_ECX),
                    struct.unpack('<I', vm.mem_read(esp + 4, 4))[0]))

        machine.hook_add(UC_HOOK_CODE, hook)
        results = []
        for entry in (0xF70A60, 0xF70BD0):
            machine.mem_write(0x30080, struct.pack('<I', 0x10000))
            machine.reg_write(UC_X86_REG_ESP, 0x30080)
            machine.reg_write(UC_X86_REG_ECX, obj)
            machine.emu_start(entry, 0x10000, count=32)
            self.assertEqual(machine.reg_read(UC_X86_REG_EAX), owner)
            self.assertEqual(machine.reg_read(UC_X86_REG_ESP), 0x30084)
            results.append({'entry': hex(entry), 'returned_owner': hex(owner)})
        self.assertEqual(observed, [(owner, 0xFFFFFFFF)])
        report = {'status': 'PASS_ORIGINAL_PE_OWNER_CONSUMERS', 'original_exe_sha256': expected,
            'source_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            'device_executed_by_this_test': False,
            'live_header_origin': 'Root read-only PID10160 guest13431620 length28; numeric only',
            'header_offset20_words': [hex(x) for x in words],
            'ui_list_string_constructor': hex(0xF74C30), 'vtable': hex(0x1A96A08),
            'vslot': hex(0x110), 'actual_consumer': hex(0xF70BD0),
            'owner_offset': hex(0x28), 'wrong_linker_index_offset': hex(0x20),
            'executions': results, 'widget_boundary_ecx_and_argument': observed,
            'boundary': 'Only FE4520 widget call is stubbed to echo ECX; both original owner-read paths execute real PE instructions'}
        (ROOT / 'research/native-uobject-outer.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    unittest.main()
