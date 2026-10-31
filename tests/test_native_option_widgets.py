"""Execute the pinned original UT3 caption/order instructions, without a UI or GPU."""
import hashlib
import json
from pathlib import Path
import re
import struct
import unittest

import pefile
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDI, UC_X86_REG_EIP, UC_X86_REG_ESP

ROOT = Path(__file__).resolve().parents[1]


class NativeOptionWidgets(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        inputs = json.loads((ROOT / "config/inputs.json").read_text())
        path = Path(inputs["source_root"]) / "Binaries/UT3.exe"
        expected = next(row["sha256"] for row in inputs["modules"] if row["path"] == "Binaries/UT3.exe")
        assert hashlib.sha256(path.read_bytes()).hexdigest() == expected
        cls.pe = pefile.PE(str(path), fast_load=True)

    def machine(self, code, size):
        machine = Uc(UC_ARCH_X86, UC_MODE_32)
        machine.mem_map(code, size)
        machine.mem_write(code, self.pe.get_data(code - 0x400000, size))
        machine.mem_map(0x300000, 0x10000)
        machine.mem_map(0x2000000, 0x10000)
        machine.reg_write(UC_X86_REG_ESP, 0x308000)
        return machine

    @staticmethod
    def word(machine, address):
        return struct.unpack("<I", machine.mem_read(address, 4))[0]

    @staticmethod
    def put(machine, address, *values):
        machine.mem_write(address, struct.pack("<" + "I" * len(values), *values))

    def caption_integer(self, value, integer_range):
        machine = self.machine(0x657000, 0x1000)
        machine.mem_map(0xED9000, 0x1000)
        machine.mem_write(0xED9000, self.pe.get_data(0xED9000 - 0x400000, 0x1000))
        machine.mem_map(0, 0x1000)
        machine.mem_map(0x1D32000, 0x1000)
        self.put(machine, 0x200041C, 0x2001000)
        machine.mem_write(0x2000420, struct.pack("<f", value))
        self.put(machine, 0x2000430, int(integer_range))
        machine.reg_write(UC_X86_REG_ECX, 0x2000000)
        # Stop before FString::Printf: actual range getter and CVTTSS2SI ran.
        machine.emu_start(0x657040, 0x657097, count=200)
        stack = machine.reg_read(UC_X86_REG_ESP)
        self.assertEqual(self.word(machine, stack + 4), 0x17E0118)
        self.assertEqual(self.pe.get_data(0x17E0118 - 0x400000, 6), "%i\0".encode("utf-16-le"))
        return self.word(machine, stack + 8)

    def test_original_slider_caption_truncates_fractions(self):
        for value, expected in ((0.22, 0), (0.65, 0), (1.0, 1)):
            self.assertEqual(self.caption_integer(value, False), expected)
        for value in (4, 22, 65, 100, 160, 300, 500):
            self.assertEqual(self.caption_integer(value, True), value)

    def list_formatter(self, scale, mode=1, adjust=True, render_scale=1):
        machine = self.machine(0xF70000, 0x10000)
        for address in (0x1734000, 0x1737000, 0x1D32000, 0x1729000, 0x1288000):
            machine.mem_map(address, 0x1000)
            machine.mem_write(address, self.pe.get_data(address - 0x400000, 0x1000))
        machine.mem_map(0, 0x1000)
        machine.mem_map(0x100000, 0x1000)
        # Fixture boundaries: unit font resolution, fixed native node measurement,
        # text-node type, no-wrap mode and the CRT search for an absent newline.
        for address, code in ((0x100100, "d9e8c20400"), (0x100110, "31c0c3"),
                              (0x100120, "b801000000c3"), (0x100130, "c20800"),
                              (0x100140, "31c0c20400"), (0x100150, "31c0c3")):
            machine.mem_write(address, bytes.fromhex(code))
        machine.mem_write(0xF73D90, b"\xe9" + struct.pack("<i", 0x100100 - 0xF73D95))
        # A fixed glyph font: native node measurement consumes node Scaling.
        self.put(machine, 0x2006F00, 0x42C80000, 0x41A00000)
        measure = bytes.fromhex("f30f104120f30f5905006f0002f30f114118"
                                "f30f104124f30f5905046f0002f30f11411cc20800")
        machine.mem_write(0x100200, measure)
        self.put(machine, 0x1729690, 0x100150)
        string, node, vtable, bounds = 0x2000000, 0x2001000, 0x2002000, 0x2003000
        self.put(machine, string, 0x2004000)
        self.put(machine, 0x2004118, 0x100140)
        self.put(machine, string + 0x3C, 0x2000800, 1, 1)
        self.put(machine, 0x2000800, node)
        self.put(machine, node, vtable)
        for offset, target in ((0x2C, 0x100110), (0x20, 0x100120), (0xC, 0x100200)):
            self.put(machine, vtable + offset, target)
        machine.mem_write(node + 0x18, struct.pack("<ffff", 100, 20, 1, 1))
        machine.mem_write(string + 0xB8, struct.pack("<fI", scale, mode))
        machine.mem_write(string + 0xDC, struct.pack("<ffI", scale, scale, adjust))
        machine.mem_write(bounds, struct.pack("<6f", 0, 0, 600, 80, render_scale, render_scale))
        machine.mem_write(bounds + 0x40, struct.pack("<f", 1))
        payload = "Vulkan (DXVK / Turnip)\0".encode("utf-16-le")
        machine.mem_write(0x2005000, payload)
        self.put(machine, node + 0x2C, 0x2005000, len(payload) // 2, len(payload) // 2)
        before = bytes(machine.mem_read(string + 0x3C, 12))
        self.put(machine, 0x308000, 0x100000, bounds, 0)
        machine.reg_write(UC_X86_REG_ECX, string)
        # Execute the full original formatter, including its node loops and
        # final layout. A middle-entry clamp probe misses native policy resets.
        machine.emu_start(0xF75DB0, 0x100000, count=100000)
        self.assertEqual(bytes(machine.mem_read(string + 0x3C, 12)), before)
        self.assertEqual(bytes(machine.mem_read(0x2005000, len(payload))), payload)
        return struct.unpack("<ff", machine.mem_read(string + 0xDC, 8)), struct.unpack("<ffff", machine.mem_read(node + 0x18, 16))

    def test_original_list_formatter_scales_existing_nodes_without_reparsing(self):
        for scale in (1.01, 1.26, 1.5):
            string_scale, node = self.list_formatter(scale)
            for actual in (*string_scale, *node[2:]):
                self.assertAlmostEqual(actual, scale, places=6)
            self.assertAlmostEqual(node[0], 100 * scale, places=4)
            self.assertAlmostEqual(node[1], 20 * scale, places=4)
        # A retained manual factor cannot override an inherited None/Justified
        # policy, whether or not bAdjustStringScale is enabled.
        for mode in (0, 2):
            for adjust in (False, True):
                string_scale, node = self.list_formatter(1.5, mode, adjust)
                self.assertEqual(string_scale, (1, 1))
                self.assertEqual(node, (100, 20, 1, 1))

    def test_explicit_text_scale_requires_disabled_auto_fit(self):
        # These are the original UIComp_DrawString render parameters after
        # SetScale(1.5). Justified auto-fit replaces the explicit node scale.
        _, justified = self.list_formatter(1, mode=2, adjust=False, render_scale=1.5)
        _, explicit = self.list_formatter(1, mode=0, adjust=False, render_scale=1.5)
        self.assertEqual(justified, (100, 20, 1, 1))
        self.assertEqual(explicit, (150, 30, 1.5, 1.5))

    def test_uniform_native_font_baseline_is_preserved_by_relative_factor(self):
        for baseline in (0.75, 1, 1.25):
            _, original = self.list_formatter(1, mode=0, adjust=False, render_scale=baseline)
            for factor in (1.01, 1.26, 1.5):
                # Native None mode uses the incoming render-parameter scale.
                # Normal mode replaces it, so the injected factor must include
                # that original uniform baseline rather than assuming 100%.
                _, scaled = self.list_formatter(baseline * factor, render_scale=baseline)
                for prior, after in zip(original, scaled):
                    self.assertAlmostEqual(after, prior * factor, places=4)

    def provider_dispatch(self, collection, write):
        """Run the original C++ accessors, with fixtures at the VM/allocator boundary."""
        machine = self.machine(0x674000, 0x6000)
        for address, size in ((0x685000, 0x1000), (0x65C000, 0x1000)):
            machine.mem_map(address, size)
            machine.mem_write(address, self.pe.get_data(address - 0x400000, size))
        machine.mem_map(0, 0x1000)
        machine.mem_map(0x1D32000, 0x1000)
        machine.mem_map(0x1DEC000, 0x1000)
        for address in (0x1288000, 0x40C000, 0x522000, 0xB81000, 0xB86000):
            machine.mem_map(address, 0x1000)
        for address in (0x1288FD4, 0x40CA80, 0x522A30, 0xB81E70, 0xB86460, 0x2007000, 0x2007100):
            machine.mem_write(address, b"\xC3")
        for address, name in ((0x1DEC160, 11), (0x1DEC418, 12)):
            self.put(machine, address, name, 0)
        store, table, field, value = 0x2000000, 0x2001000, 0x2002000, 0x2003000
        event, lookup = 0x2007000, 0x2007100
        self.put(machine, store, table)
        self.put(machine, table + 0xEC, event)
        self.put(machine, table + 0x150, lookup)
        self.put(machine, field, 0x2004000, 17, 17)
        machine.mem_write(0x2004000, "touchSensitivity\0".encode("utf-16-le"))
        machine.mem_write(value + 0x28, struct.pack("<ffffI", 22, 4, 300, 2, 1))
        self.put(machine, 0x308000, 0x2007F00, field, value, 0xFFFFFFFF)
        machine.reg_write(UC_X86_REG_ECX, store)
        calls = []

        def finish(vm, result, argument_bytes=0):
            stack = vm.reg_read(UC_X86_REG_ESP)
            vm.reg_write(UC_X86_REG_EAX, result & 0xFFFFFFFF)
            vm.reg_write(UC_X86_REG_EIP, self.word(vm, stack))
            vm.reg_write(UC_X86_REG_ESP, stack + 4 + argument_bytes)

        def instruction(vm, address, size, _):
            stack = vm.reg_read(UC_X86_REG_ESP)
            target = vm.reg_read(UC_X86_REG_ECX)
            if address == 0x1288FD4:  # memset; caller clears its arguments
                dest, fill, count = (self.word(vm, stack + offset) for offset in (4, 8, 12))
                vm.mem_write(dest, bytes([fill]) * count)
                finish(vm, dest)
            elif address in (0x40CA80, 0x522A30):  # empty/shared FString and TArray fixtures
                source = self.word(vm, stack + 4)
                vm.mem_write(target, bytes(vm.mem_read(source, 12)))
                finish(vm, target, 4)
            elif address in (0x676B20, 0x676A90):  # fixture-owned strings/arrays require no free
                finish(vm, 0)
            elif address == 0xB81E70:  # FName construction
                self.put(vm, target, 99, 0)
                finish(vm, target, 12)
            elif address == lookup:
                finish(vm, -1, 8)  # no StringData entry for this scalar field
            elif address == 0xB86460:  # FindFunctionChecked; leave ProcessEvent's arguments on stack
                self.assertEqual(self.word(vm, stack + 4), 12 if write else 11)
                calls.append("lookup")
                finish(vm, 0x2007200, 12)
            elif address == event:
                self.assertEqual(self.word(vm, stack + 4), 0x2007200)
                params = self.word(vm, stack + 8)
                self.assertEqual(self.word(vm, params + 0x48), 0xFFFFFFFF)
                if write:
                    self.assertEqual(struct.unpack("<f", vm.mem_read(params + 0x34, 4))[0], 22)
                else:
                    vm.mem_write(params + 0x14, b"\x02")  # DATATYPE_RangeProperty
                    vm.mem_write(params + 0x34, struct.pack("<ffffI", 65, 20, 100, 5, 1))
                self.put(vm, params + 0x4C, 1)
                calls.append("event")
                finish(vm, 0, 12)

        machine.hook_add(UC_HOOK_CODE, instruction)
        if collection:
            entry = 0x679370 if write else 0x679280
        else:
            entry = 0x685C70 if write else 0x685D40
        machine.emu_start(entry, 0x2007F00, count=1000)
        return machine.reg_read(UC_X86_REG_EAX), calls, bytes(machine.mem_read(value, 0x3C))

    def test_original_string_list_rejects_scalar_script_events(self):
        for write in (False, True):
            result, calls, _ = self.provider_dispatch(True, write)
            self.assertEqual((result, calls), (0, []))
            result, calls, value = self.provider_dispatch(False, write)
            self.assertEqual((result, calls), (1, ["lookup", "event"]))
            if not write:
                self.assertEqual(value[8], 2)
                self.assertEqual(struct.unpack_from("<ffffI", value, 0x28), (65, 20, 100, 5, 1))

    def native_order(self, indices):
        machine = self.machine(0x69E000, 0x2000)
        machine.mem_map(0x4000000, 0x10000)
        providers = [0x2001000 + index * 0x100 for index in range(len(indices))]
        for pointer, index in zip(providers, indices):
            self.put(machine, pointer, 0x2008000)
            self.put(machine, pointer + 0xDC, index)
        self.put(machine, 0x308048, len(providers))
        machine.reg_write(UC_X86_REG_EBP, 0x2000000)
        machine.reg_write(UC_X86_REG_EBX, 0)
        allocated = {}

        def insert(vector, index):
            count = self.word(machine, vector + 4)
            assert 0 <= index <= count < 64
            if vector not in allocated:
                allocated[vector] = 0x4000000 + len(allocated) * 0x1000
            data = allocated[vector]
            if count > index:
                machine.mem_write(data + (index + 1) * 4, bytes(machine.mem_read(data + index * 4, (count - index) * 4)))
            self.put(machine, vector, data, count + 1, 64)
            return data + index * 4

        def instruction(vm, address, size, _):
            stack = vm.reg_read(UC_X86_REG_ESP)
            if address == 0x69EDD0:
                index = self.word(vm, stack + 0x18)
                self.put(vm, stack + 0x1C, 1, providers[index])
                vm.reg_write(UC_X86_REG_EIP, 0x69EE04)
            elif address == 0x69EE1F:
                vm.reg_write(UC_X86_REG_EAX, vm.reg_read(UC_X86_REG_ECX))
                vm.reg_write(UC_X86_REG_EIP, 0x69EE21)
            elif address == 0x69EE22:
                vm.reg_write(UC_X86_REG_EAX, self.word(vm, stack))
                vm.reg_write(UC_X86_REG_EIP, 0x69EE27)
            elif address in (0x69EE47, 0x69EEEF):
                vector = vm.reg_read(UC_X86_REG_ECX)
                count = self.word(vm, vector + 4)
                value = self.word(vm, self.word(vm, stack))
                self.put(vm, insert(vector, count), value)
                vm.reg_write(UC_X86_REG_EAX, count)
                vm.reg_write(UC_X86_REG_ESP, stack + 4)
                vm.reg_write(UC_X86_REG_EIP, address + 5)
            elif address in (0x69EE87, 0x69EED8):
                self.assertEqual(tuple(self.word(vm, stack + offset) for offset in (4, 8, 12)), (1, 4, 8))
                insert(vm.reg_read(UC_X86_REG_ECX), self.word(vm, stack))
                vm.reg_write(UC_X86_REG_ESP, stack + 16)
                vm.reg_write(UC_X86_REG_EIP, address + 5)

        # Only provider lookup/class-cast and generic TArray allocation are fixtures.
        # The original two ordering passes execute unchanged.
        machine.hook_add(UC_HOOK_CODE, instruction)
        machine.emu_start(0x69ED9E, 0x69EEFD, count=10000)
        data, count = (self.word(machine, 0x308024 + offset) for offset in (0, 4))
        self.assertEqual(count, len(indices))
        return [providers.index(self.word(machine, data + index * 4)) + 1 for index in range(count)]

    def test_original_zero_option_index_preserves_config_order(self):
        self.assertEqual(self.native_order(list(range(1, 15))), [1, 14, 2, 13, 3, 12, 4, 11, 5, 10, 6, 9, 7, 8])
        self.assertEqual(self.native_order([0] * 14), list(range(1, 15)))
        for language in ("AndroidPortOptions.ini", "AndroidPortOptions-RUS.ini"):
            text = (ROOT / "android/uscript/AndroidPort" / language).read_text(encoding="utf-8")
            self.assertEqual(re.findall(r"^OptionIndex=(\d+)$", text, re.M), ["0"] * 15)
            self.assertEqual(re.findall(r"DataStoreMarkup=<AndroidPortValues:(\w+)>", text),
                ["touchSensitivity", "touchAltSensitivity", "touchScale", "uiScale", "touchOpacity",
                 "touchLeftHanded", "touchInvertY", "touchWasd", "touchGyroSensitivity", "renderScale"])
            self.assertEqual(re.findall(r"DataStoreMarkup=<AndroidPort:(\w+)>", text),
                ["touchVehicleProfile", "touchGyro", "language", "fpsLimit", "renderer"])
            if language == "AndroidPortOptions.ini":
                labels = next(line for line in text.splitlines() if 'Tag="language",Labels=' in line)
                self.assertTrue(labels.isascii(), "INT stock fonts require ASCII language labels")


if __name__ == "__main__":
    unittest.main()
