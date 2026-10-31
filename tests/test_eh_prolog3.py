"""Verify VC8 helper frames against the original pinned MSVCP80 instructions."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EBX,
                              UC_X86_REG_EDI, UC_X86_REG_ESI, UC_X86_REG_ESP)

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import lift_game
from tools.lift.eh32 import frame_handler


class Prolog3Test(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        modules = json.loads((ROOT / "config/modules.json").read_text())["modules"]
        spec = next(row for row in modules if row["name"] == "msvcp80")
        source = Path(spec["source"])
        if not source.is_file():
            raise unittest.SkipTest("requires the pinned original MSVCP80 DLL")
        raw = source.read_bytes()
        if hashlib.sha256(raw).hexdigest() != spec["sha256"]:
            raise ValueError("MSVCP80 original SHA-256 mismatch")
        info = lift_game.analyze_pe(str(source))
        cls.base = info.image_base
        cls.decoder = lift_game.BoundedDisassembler(raw, info.image_base, info.sections)
        cls.md = Cs(CS_ARCH_X86, CS_MODE_32)
        cls.md.detail = True
        import pefile
        pe = pefile.PE(data=raw, fast_load=True)
        cls.image = pe.get_memory_mapped_image()
        cls.image_size = (pe.OPTIONAL_HEADER.SizeOfImage + 4095) & -4096

    def caller(self, helper, handler, locals_size=12):
        start = 0x20000
        code = b"\x6A" + bytes([locals_size]) + b"\xB8" + struct.pack("<I", handler)
        code += b"\xE8" + struct.pack("<i", helper - start - len(code) - 5)
        return start, code

    def test_original_helpers_preserve_runtime_frame_contract(self):
        handler = self.base + 0x313E8
        for rva, saved_esp in ((0x30884, False), (0x308B7, True), (0x308ED, False), (0x30923, True)):
            with self.subTest(helper=hex(rva)):
                helper = self.base + rva
                start, code = self.caller(helper, handler)
                insns = list(self.md.disasm(code, start))
                self.assertEqual(frame_handler(self.decoder.read_bytes, insns), (handler, saved_esp))
                uc = Uc(UC_ARCH_X86, UC_MODE_32)
                uc.mem_map(self.base, self.image_size)
                uc.mem_write(self.base, self.image)
                uc.mem_map(0, 4096)
                uc.mem_map(start, 4096)
                uc.mem_write(start, code)
                uc.mem_map(0x100000, 0x10000)
                entry_esp, previous_ebp = 0x108000, 0x12345678
                uc.mem_write(0, struct.pack("<I", 0x76543210))
                for register, value in ((UC_X86_REG_ESP, entry_esp), (UC_X86_REG_EBP, previous_ebp),
                                        (UC_X86_REG_EBX, 0x11223344), (UC_X86_REG_ESI, 0x22334455),
                                        (UC_X86_REG_EDI, 0x33445566)):
                    uc.reg_write(register, value)
                uc.emu_start(start, start + len(code), count=100)
                word = lambda address: struct.unpack("<I", uc.mem_read(address, 4))[0]
                ebp, esp = uc.reg_read(UC_X86_REG_EBP), uc.reg_read(UC_X86_REG_ESP)
                self.assertEqual(ebp + 4, entry_esp)
                self.assertEqual(word(ebp), previous_ebp)
                self.assertEqual(word(0), ebp - 12)
                self.assertEqual(word(ebp - 12), 0x76543210)
                self.assertEqual(word(ebp - 8), handler)
                self.assertEqual(word(ebp - 4), 0xFFFFFFFF)
                self.assertEqual(esp, entry_esp - 12 - 32)
                self.assertEqual(word(ebp - 16) == esp, saved_esp)
                self.assertEqual([word(esp + offset) for offset in (4, 8, 12)],
                                 [0x33445566, 0x22334455, 0x11223344])

    def test_real_catch_owner_adds_cold_continuation(self):
        function = self.decoder.disassemble_function(self.base + 0x32C0, {})
        lift_game.complete_eh_owner(self.decoder, function, {})
        self.assertEqual(function.eh_resume, {self.base + 0x333C})
        self.assertEqual(function.eh_frame_bias, 0)
        self.assertEqual(function.eh_added_instructions, 3)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            report = lift_game.emit_functions(output, {}, self.decoder, {function.address: function}, label="EH oracle")
            source = (output / report["sources"][0]).read_text()
            target = self.base + 0x333C
            self.assertEqual(source.count("    RECOMP_EH_ENTER();"), 1)
            self.assertIn(f"case 0x{target:08X}u: goto L_{target:08X};", source)

    def test_reject_changed_helper_and_seh4(self):
        helper, handler = self.base + 0x308B7, self.base + 0x313E8
        start, code = self.caller(helper, handler)
        insns = list(self.md.disasm(code, start))
        for offset in (0, 8, 12, 19, 21, 28, 33, 48, 53):
            original = bytearray(self.decoder.read_bytes(helper, 64))
            original[offset] ^= 1
            def changed_read(address, size):
                return bytes(original[:size]) if address == helper else self.decoder.read_bytes(address, size)
            with self.subTest(changed_byte=offset):
                self.assertIsNone(frame_handler(changed_read, insns))
        # The CRT's SEH4 startup pushes a ScopeTable, rather than a C++ handler in EAX.
        seh4 = list(self.decoder.disassemble_at(self.base + 0x3099A, 12))[:3]
        self.assertIsNone(frame_handler(self.decoder.read_bytes, seh4))


if __name__ == "__main__":
    unittest.main()
