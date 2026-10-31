"""Oracle the first CRT blocker against its pinned original x86 bytes."""
import hashlib
import json
from pathlib import Path
import sys
import unittest

import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import (UC_X86_REG_EFLAGS, UC_X86_REG_FPCW, UC_X86_REG_FPSW,
                              UC_X86_REG_MXCSR, UC_X86_REG_FP0)

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from lift_game import CheckedLifter


class FnclexTest(unittest.TestCase):
    def test_original_status_mask_and_preservation(self):
        specs = json.loads((ROOT / "config/modules.json").read_text())["modules"]
        spec = next(row for row in specs if row["name"] == "msvcr80")
        raw = Path(spec["source"]).read_bytes()
        self.assertEqual(hashlib.sha256(raw).hexdigest(), spec["sha256"])
        pe = pefile.PE(data=raw, fast_load=True)
        original = pe.get_data(0x19376, 2)
        self.assertEqual(original, bytes.fromhex("DBE2"))
        uc = Uc(UC_ARCH_X86, UC_MODE_32)
        uc.mem_map(0x10000, 4096)
        uc.mem_write(0x10000, original)
        uc.reg_write(UC_X86_REG_FPCW, 0x0B7F)
        uc.reg_write(UC_X86_REG_MXCSR, 0x7FFF)
        uc.reg_write(UC_X86_REG_FP0, (0x8000000000000001, 0x3FFF))
        for status in range(0x10000):
            uc.reg_write(UC_X86_REG_FPSW, status)
            uc.reg_write(UC_X86_REG_EFLAGS, 0xAD7)
            initial = uc.reg_read(UC_X86_REG_FPSW)
            uc.emu_start(0x10000, 0x10002, count=1)
            self.assertEqual(uc.reg_read(UC_X86_REG_FPSW), initial & ~0x80FF)
        self.assertEqual(uc.reg_read(UC_X86_REG_FPCW), 0x0B7F)
        self.assertEqual(uc.reg_read(UC_X86_REG_MXCSR), 0x7FFF)
        self.assertEqual(uc.reg_read(UC_X86_REG_EFLAGS), 0xAD7)
        self.assertEqual(uc.reg_read(UC_X86_REG_FP0), (0x8000000000000001, 0x3FFF))

    def test_lifter_calls_checked_helper(self):
        md = Cs(CS_ARCH_X86, CS_MODE_32)
        md.detail = True
        instruction = next(md.disasm(bytes.fromhex("DBE2"), 0x10019376))
        text = "\n".join(CheckedLifter().lift_instruction(instruction))
        self.assertIn("void_fpu_clear_exceptions(0x10019376u)", text)
        self.assertNotIn("void_lift_trap", text)


if __name__ == "__main__":
    unittest.main()
