"""Compare lifted PADDW with original x86 bytes in Unicorn and compiled ARM64 C."""
import os
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile
import unittest

from capstone import Cs, CS_ARCH_X86, CS_MODE_32
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EFLAGS

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from lift_game import CheckedLifter

CODE, DATA, FLAGS = 0x1000, 0x2000, 0xED7
# First two opcodes are verbatim CRT80 instructions at rebased 0x10055d91/0x10055db1.
FORMS = [("660ffde9", 16, 5, 1), ("660ffddc", 16, 3, 4),
         ("660ffded", 16, 5, 5), ("660ffd6810", 16, 5, None),
         ("0ffdc1", 8, 0, 1), ("0ffdc0", 8, 0, 0), ("0ffd4010", 8, 0, None)]


def lift(raw):
    decoder = Cs(CS_ARCH_X86, CS_MODE_32)
    decoder.detail = True
    instruction = next(decoder.disasm(raw, CODE, count=1))
    return "\n".join(CheckedLifter().lift_instruction(instruction))


def oracle(raw, width, destination, source, a, b):
    xmm = width == 16
    load = b"\xf3\x0f\x6f" if xmm else b"\x0f\x6f"
    store = b"\xf3\x0f\x7f" if xmm else b"\x0f\x7f"
    prefix = load + bytes([destination << 3])
    if source is not None and source != destination:
        prefix += load + bytes([0x40 | source << 3, 16])
    code = prefix + raw + store + bytes([0x40 | destination << 3, 32])
    machine = Uc(UC_ARCH_X86, UC_MODE_32)
    machine.mem_map(CODE, 0x2000)
    machine.mem_write(CODE, code)
    machine.mem_write(DATA, a)
    machine.mem_write(DATA + 16, b)
    machine.reg_write(UC_X86_REG_EAX, DATA)
    machine.reg_write(UC_X86_REG_EFLAGS, FLAGS)
    machine.emu_start(CODE, CODE + len(code))
    if machine.reg_read(UC_X86_REG_EFLAGS) != FLAGS:
        raise AssertionError("PADDW changed original EFLAGS")
    return bytes(machine.mem_read(DATA + 32, width))


def vectors():
    random_source = random.Random(0x55D91)
    edge = [0, 1, 0x7FFF, 0x8000, 0xFFFF, 0xFFFE, 2, 0x1234]
    for raw, width, destination, source in FORMS:
        for index in range(36):
            a = edge[:width // 2] if index < 4 else [random_source.randrange(65536) for _ in range(width // 2)]
            b = [index, 0xFFFF, 1, 0x8000, 0xFFFF, 2, 0xFFFE, 0xEDCC][:width // 2] if index < 4 else [random_source.randrange(65536) for _ in a]
            if source == destination:
                b = a
            a = struct.pack("<" + "H" * len(a), *a)
            b = struct.pack("<" + "H" * len(b), *b)
            yield bytes.fromhex(raw), width, destination, source, a, b


def c_probe():
    rows = []
    for index, (raw, width, destination, source, a, b) in enumerate(vectors()):
        expected = oracle(raw, width, destination, source, a, b)
        output = f"_xmm[{destination}]" if width == 16 else f"_mm[{destination}]"
        row = f"{{ xmm_t _xmm[8] = {{0}}; uint64_t _mm[8] = {{0}}; uint32_t eax = {DATA}u;\n"
        row += "uint8_t a[] = {" + ",".join(map(str, a)) + "}, b[] = {" + ",".join(map(str, b)) + "};\n"
        row += "uint8_t expected[] = {" + ",".join(map(str, expected)) + "};\n"
        row += f"for (unsigned i=0; i<{width}; ++i) {{ test_input[i]=a[i]; test_input[16+i]=b[i]; }}\n"
        row += f"for (unsigned i=0; i<{width}; ++i) {{ a[i]=test_input[i]; b[i]=test_input[16+i]; }}\n"
        row += f"memcpy(test_memory + {DATA}, a, {width}); memcpy(test_memory + {DATA + 16}, b, {width});\n"
        row += f"memcpy(&{output}, a, {width});\n"
        if source is not None and source != destination:
            operand = f"_xmm[{source}]" if width == 16 else f"_mm[{source}]"
            row += f"memcpy(&{operand}, b, {width});\n"
        row += lift(raw) + "\n"
        row += f'if (memcmp(&{output}, expected, {width})) {{ puts("PADDW vector {index}"); return 1; }} }}\n'
        rows.append(row)
    return """
#include <stdint.h>
static uint8_t test_memory[0x3000] __attribute__((aligned(16)));
static volatile uint8_t test_input[32]; /* Keep the arithmetic executable under -O2. */
#define VOID_GUEST_BASE ((uintptr_t)test_memory)
#include "recomp_types.h"
int main(void) {
""" + "\n".join(rows) + '\nputs("PADDW ARM64: 252 original-byte Unicorn vectors PASS"); return 0; }\n'


class PaddwTest(unittest.TestCase):
    def test_original_bytes_and_wrapping(self):
        for raw, width, destination, source, a, b in vectors():
            with self.subTest(opcode=raw.hex(), a=a.hex(), b=b.hex()):
                unpack = "<" + "H" * (width // 2)
                expected = struct.pack(unpack, *[(x + y) & 0xFFFF for x, y in zip(struct.unpack(unpack, a), struct.unpack(unpack, b))])
                self.assertEqual(oracle(raw, width, destination, source, a, b), expected)
                self.assertNotIn("void_lift_trap", lift(raw))

    def test_fpu_environment_is_never_silently_dropped(self):
        for raw in ("dd30", "dd20", "d930", "d920", "0fae00", "0fae08", "0fae20", "0fae28", "dbe3", "dbe2"):
            with self.subTest(opcode=raw):
                self.assertIn("void_lift_trap", lift(bytes.fromhex(raw)))

    @unittest.skipUnless(os.environ.get("UT3_TEST_ANDROID"), "set UT3_TEST_ANDROID to execute lifted C on Android")
    def test_lifted_c_on_android(self):
        sdk = Path(os.environ.get("ANDROID_SDK_ROOT", Path.home() / "AppData/Local/Android/Sdk"))
        ndk = Path(os.environ.get("ANDROID_NDK_HOME", sdk / "ndk/27.2.12479018"))
        clang = ndk / "toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe"
        adb = sdk / "platform-tools/adb.exe"
        remote = "/data/local/tmp/ut3-paddw-test"
        with tempfile.TemporaryDirectory() as directory:
            source, binary = Path(directory) / "probe.c", Path(directory) / "probe"
            source.write_text(c_probe())
            subprocess.run([str(clang), "--target=aarch64-linux-android29", "-O2", "-I", str(ROOT / "android/runtime"), str(source), "-o", str(binary)], check=True)
            target = [str(adb), "-s", os.environ["UT3_TEST_ANDROID"]]
            subprocess.run([*target, "push", str(binary), remote], check=True, capture_output=True)
            subprocess.run([*target, "shell", "chmod", "755", remote], check=True)
            subprocess.run([*target, "shell", remote], check=True)


if __name__ == "__main__":
    unittest.main()
