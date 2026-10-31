"""Check the original-image boundary and the guest module address layout."""
import hashlib
import json
from pathlib import Path
import sys
import shutil
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from lift_ut3 import prepare, metadata_roots, code_address
import lift_ut3
import pefile


class LiftInputsTest(unittest.TestCase):
    def test_changed_input_is_rejected_before_pe_parsing(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "image.exe"
            source.write_bytes(b"changed")
            spec = {"source": str(source), "file": "image.exe",
                    "sha256": hashlib.sha256(b"original").hexdigest(), "base": "0x400000"}
            with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
                prepare(spec, Path(directory))

    def test_module_ranges_do_not_overlap(self):
        inputs = json.loads((ROOT / "config/inputs.json").read_text())
        modules = json.loads((ROOT / "config/modules.json").read_text())["modules"]
        pinned = {m["sha256"]: m for m in inputs["modules"]}
        external = json.loads((ROOT / "research/external-runtime-inputs.json").read_text())
        pinned.update({m["sha256"]: m for m in external["modules"]})
        spans = sorted((int(m["base"], 0), int(m["base"], 0) + pinned[m["sha256"]]["image_size"])
                       for m in modules)
        self.assertEqual(len({m["file"].lower() for m in modules}), len(modules))
        self.assertEqual(sum(bool(m["exe"]) for m in modules), 1)
        for start, end in spans:
            self.assertEqual(start & 0xFFFF, 0)
            self.assertLess(start, end)
            self.assertLessEqual(end, 0x7FF00000)
        for (_, end), (start, _) in zip(spans, spans[1:]):
            self.assertLessEqual(end, start)

    def test_actual_roots_are_executable(self):
        manifest = json.loads((ROOT / "config/inputs.json").read_text())
        source = Path(manifest["source_root"]) / "Binaries/UT3.exe"
        if not source.exists():
            self.skipTest("Original UT3 input is unavailable")
        pe = pefile.PE(str(source), fast_load=True)
        pe.parse_data_directories(directories=[0, 5, 9, 10])
        roots = metadata_roots(pe)
        self.assertEqual(len(roots), 105907)
        self.assertTrue(all(code_address(pe, a) for a in roots))
        # UTF-16 'in' looks like a code VA, but no pointer relocation names it.
        self.assertNotIn(0x006E0069, roots)
        pe.close()

    def test_targeted_regeneration_preserves_other_bodies_and_dispatch(self):
        module = ROOT / "generated/ogg"
        if not (module / "report.json").exists():
            self.skipTest("Generated ogg module is unavailable")
        specs = json.loads((ROOT / "config/modules.json").read_text())["modules"]
        spec = dict(next(m for m in specs if m["name"] == "ogg"))
        source_root = Path(json.loads((ROOT / "config/inputs.json").read_text())["source_root"])
        spec["source"] = str(source_root / spec["path"])
        report = json.loads((module / "report.json").read_text())
        address = int(report["dispatch_entries"][0], 0)
        source = module / report["sources"][0]
        original = source.read_text()
        next_symbol = "void sub_" + format(int(report["dispatch_entries"][1], 0), "08X")
        unchanged_tail = original[original.index(next_symbol):]
        with tempfile.TemporaryDirectory() as directory:
            stage = Path(directory)
            target = stage / "generated/ogg"
            target.mkdir(parents=True)
            shutil.copy2(module / "report.json", target / "report.json")
            shutil.copy2(source, target / source.name)
            old_root = lift_ut3.ROOT
            try:
                lift_ut3.ROOT = stage
                lift_ut3.regenerate_function(spec, address, source_root)
            finally:
                lift_ut3.ROOT = old_root
            result = json.loads((target / "report.json").read_text())
            self.assertEqual(result["dispatch_entries"], report["dispatch_entries"])
            self.assertEqual(result["sources"], report["sources"])
            updated = (target / source.name).read_text()
            self.assertEqual(updated[updated.index(next_symbol):], unchanged_tail)
            self.assertEqual(updated.count(f"void sub_{address:08X}(void) {{"), 1)
            self.assertEqual(result["image_sha256"], report["image_sha256"])
            self.assertEqual(result["targeted_regenerations"][hex(address)]["source_sha256"],
                             hashlib.sha256((target / source.name).read_bytes()).hexdigest())

    def test_declared_jump_table_hash_is_enforced_before_output(self):
        specs = json.loads((ROOT / "config/modules.json").read_text())["modules"]
        spec = next(m for m in specs if m["name"] == "msvcr80")
        if not spec.get("jump_tables"):
            self.skipTest("No declared CRT jump tables")
        spec["jump_tables"][0]["sha256"] = "0" * 64
        with tempfile.TemporaryDirectory() as directory:
            old_root = lift_ut3.ROOT
            try:
                lift_ut3.ROOT = Path(directory)
                with self.assertRaisesRegex(ValueError, "jump table SHA-256 mismatch"):
                    prepare(spec, Path(directory))
                self.assertFalse((Path(directory) / "generated").exists())
            finally:
                lift_ut3.ROOT = old_root


if __name__ == "__main__":
    unittest.main()
