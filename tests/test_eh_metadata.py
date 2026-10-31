"""VC8 metadata recognition, with optional execution of the C reader on Android."""
import importlib.util
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("eh32", ROOT / "reference/pcrecomp/tools/lift/eh32.py")
eh32 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(eh32)

HANDLER, CHECK, INFO = 0x015AC9AB, 0x01288EF2, 0x01BD5360
# UT3.exe's SafeSEH handler and its __security_check_cookie, copied verbatim.
GS = bytes.fromhex("8b5424088d420c8b4af033c8e836c5cdffb86053bd01e926c5cdff")
COOKIE = bytes.fromhex("3b0de024d3017502f3c3e9390d0000")
METADATA = struct.pack("<5I", 0x19930522, 0, 0, 0, 0)


def vectors():
    def case(name, handler=GS, cookie=COOKIE, metadata=METADATA, expected=None, address=HANDLER, info=INFO):
        return name, [(address, handler), (CHECK, cookie), (info, metadata)], info if expected is None else expected

    cases = [case("real GS wrapper")]
    cases += [case("classic rel32", bytes.fromhex("b86053bd01e900000000")),
              case("classic indirect", bytes.fromhex("b86053bd01ff2500000000"))]
    cases += [case("real two cookies disp8/disp32", bytes.fromhex(
        "8b5424088d420c8b8a18f7ffff33c8e847c4cdff8b4afc33c8e83dc4cdffb80854bd01e92dc4cdff"),
        address=0x015ACA97, info=0x01BD5408),
        case("real two cookies disp32/disp32 with frame adjustment", bytes.fromhex(
        "8b5424088d8258ffffff8b8a54ffffff33c8e809b4cdff83c0108b4afc33c8e8fcb3cdffb8ec63bd01e9ecb3cdff"),
        address=0x015ADAD2, info=0x01BD63EC),
        case("real zero frame displacement", bytes.fromhex(
        "8b5424088d028b4afc33c8e8c4b1cdffb83466bd01e9b4b1cdff"),
        address=0x015ADD1E, info=0x01BD6634)]
    for name, regions, expected in list(cases[3:]):
        for length in (len(regions[0][1]) - 1, len(regions[0][1]) - 5):
            cases.append((f"{name} truncated {length}",
                          [(regions[0][0], regions[0][1][:length]), *regions[1:]], 0))
    for index in (0, 1, 2, 3, 4, 5, 7, 8, 10, 11, 12):
        raw = bytearray(GS)
        raw[index] ^= 1
        cases.append(case(f"changed GS prefix byte {index}", bytes(raw), expected=0))
    for index in (0, 1, 6, 7, 8, 9, 10):
        raw = bytearray(COOKIE)
        raw[index] ^= 1
        cases.append(case(f"changed cookie helper byte {index}", cookie=bytes(raw), expected=0))
    for length in (0, 9, 17, 26):
        cases.append(case(f"truncated handler {length}", GS[:length], expected=0))
    cases += [case("truncated helper", cookie=COOKIE[:-1], expected=0),
              case("truncated metadata", metadata=METADATA[:-1], expected=0),
              case("bad metadata magic", metadata=b"bad!" + METADATA[4:], expected=0),
              case("changed helper target", GS[:13] + struct.pack("<i", CHECK - HANDLER - 16) + GS[17:], expected=0),
              case("bad terminal jump", GS[:22] + b"\x90" + GS[23:], expected=0)]
    return cases


def reader(regions):
    def read(address, size):
        for start, raw in regions:
            if start <= address < start + len(raw):
                return raw[address - start:address - start + size]
        return b""
    return read


def c_probe():
    source = (ROOT / "android/host/eh.c").read_text()
    function = source[source.index("static uint32_t frame_operand_end("):source.index("/* Run guest code")]
    arrays, tests = [], []
    for index, (name, regions, expected) in enumerate(vectors()):
        rows = []
        for region, (address, raw) in enumerate(regions):
            symbol = f"bytes_{index}_{region}"
            arrays.append(f"static uint8_t {symbol}[] = {{{','.join(map(str, raw)) or '0'}}};")
            rows.append(f"{{{address}u, {len(raw)}u, {symbol}}}")
        tests.append("{ region_t r[] = {" + ",".join(rows) + "}; regions=r; count=3; "
                     f'if (funcinfo({regions[0][0]}u) != {expected}u) {{ puts("{name}"); return {index + 1}; }} }}')
    return """
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define EH_MAGIC_VC6 0x19930520u
#define EH_MAGIC_VC7 0x19930521u
#define EH_MAGIC_VC8 0x19930522u
typedef struct { uint32_t start, size; uint8_t *bytes; } region_t;
static region_t *regions;
static unsigned count;
static void *guest_ptr(uint32_t va) {
    for (unsigned i=0; i<count; ++i)
        if (va >= regions[i].start && va - regions[i].start < regions[i].size)
            return regions[i].bytes + va - regions[i].start;
    return NULL;
}
static int void_vm_readable(uint32_t va, uint32_t size) {
    for (unsigned i=0; i<count; ++i)
        if (va >= regions[i].start && size <= regions[i].size &&
            va - regions[i].start <= regions[i].size - size) return 1;
    return 0;
}
static uint32_t read32(uint32_t va) { uint32_t value; memcpy(&value, guest_ptr(va), 4); return value; }
#define MEM8(va) (*(uint8_t *)guest_ptr(va))
#define MEM32(va) read32(va)
#define GPTR(va) guest_ptr(va)
""" + function + "\n" + "\n".join(arrays) + "\nint main(void) {\n" + "\n".join(tests) + '\nputs("EH metadata C vectors PASS"); return 0; }\n'


class MetadataTest(unittest.TestCase):
    def test_real_wrapper_and_rejected_mutations(self):
        for name, regions, expected in vectors():
            with self.subTest(name=name):
                self.assertEqual(eh32.funcinfo(reader(regions), regions[0][0]), expected or None)

    @unittest.skipUnless(os.environ.get("UT3_TEST_ANDROID"), "set UT3_TEST_ANDROID to an adb serial for the C reader")
    def test_c_reader_on_android(self):
        sdk = Path(os.environ.get("ANDROID_SDK_ROOT", Path.home() / "AppData/Local/Android/Sdk"))
        ndk = Path(os.environ.get("ANDROID_NDK_HOME", sdk / "ndk/27.2.12479018"))
        clang = ndk / "toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe"
        adb = sdk / "platform-tools/adb.exe"
        remote = "/data/local/tmp/ut3-eh-metadata-test"
        with tempfile.TemporaryDirectory() as directory:
            source, binary = Path(directory) / "probe.c", Path(directory) / "probe"
            source.write_text(c_probe())
            subprocess.run([str(clang), "--target=aarch64-linux-android29", "-O2", str(source), "-o", str(binary)], check=True)
            target = [str(adb), "-s", os.environ["UT3_TEST_ANDROID"]]
            subprocess.run([*target, "push", str(binary), remote], check=True, capture_output=True)
            subprocess.run([*target, "shell", "chmod", "755", remote], check=True)
            subprocess.run([*target, "shell", remote], check=True)


if __name__ == "__main__":
    unittest.main()
