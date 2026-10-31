"""Exercise the real packager boundary with tiny pinned ELF fixtures."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import build_android


class NativeBundleTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.addCleanup(patch.stopall)
        patch.object(build_android, "ROOT", self.root).start()
        elf = bytearray(20)
        elf[:6] = b"\x7fELF\x02\x01"
        elf[16:20] = b"\x03\x00\xb7\x00"
        self.elf = bytes(elf)
        self.write("dependencies-audio.json", b"{}")
        self.write("tools/build_native_audio.py", b"audio tool")
        self.write("tools/build_dxvk.py", b"dxvk tool")
        self.write("android/dxvk/wsi_android.cpp", b"android window")
        self.write("android/dxvk/bridge.cpp", b"typed native adapter")
        self.write("tools/generate_dxvk_bridge.py", b"typed slot generator")
        self.write("build/dxvk/guest_methods.inc", b"guest thunks")
        self.audio = {
            "manifest_sha256": self.digest("dependencies-audio.json"),
            "tool_sha256": self.digest("tools/build_native_audio.py"),
            "runtime": {}, "licenses": {}
        }
        self.dxvk = {
            "build_tool_sha256": self.digest("tools/build_dxvk.py"),
            "wsi_sha256": self.digest("android/dxvk/wsi_android.cpp"),
            "outputs": []
        }
        for name in ("libopenal.so", "libc++_shared.so"):
            path = "build/native-audio/runtime/" + name
            self.write(path, self.elf)
            self.audio["runtime"][name] = self.digest(path)
        for name in ("libdxvk_d3d9.so", "libc++_shared.so", "libut3_dxvk.a"):
            path = "build/dxvk/" + name
            self.write(path, self.elf if name.endswith(".so") else b"!<arch>\n")
            self.dxvk["outputs"].append({"path": path, "sha256": self.digest(path)})
        self.write("build/native-audio/licenses/OpenAL-COPYING.txt", b"audio license")
        self.audio["licenses"]["OpenAL-COPYING.txt"] = self.digest("build/native-audio/licenses/OpenAL-COPYING.txt")
        self.write("reference/dxvk/LICENSE", b"dxvk license")
        self.write("reference/dxvk/include/native/directx/COPYING.MinGW-w64.txt", b"headers license")
        self.write("tools/build_turnip.py", b"turnip tool")
        self.write("dependencies-turnip.json", b"turnip inputs")
        self.write("android/dxvk/turnip.inc", b"custom vulkan loader")
        self.write("android/dxvk/query_wait.inc", b"pending query wait policy")
        self.turnip = {"build_tool_sha256": self.digest("tools/build_turnip.py"),
                       "input_pins_sha256": self.digest("dependencies-turnip.json"),
                       "outputs": [], "licenses": []}
        for name in ("libadrenotools.so", "libhook_impl.so", "libmain_hook.so", "libfile_redirect_hook.so", "libgsl_alloc_hook.so", "libut3_turnip.so"):
            path = "build/turnip-runtime/runtime/" + name
            self.write(path, self.elf)
            self.turnip["outputs"].append({"path": path, "sha256": self.digest(path)})
        for name in ("adrenotools.txt", "linkernsbypass.txt", "Turnip.txt"):
            path = "build/turnip-runtime/runtime/licenses/" + name
            self.write(path, b"custom driver license")
            self.turnip["licenses"].append({"path": path, "sha256": self.digest(path)})
        self.receipts()

    def write(self, name, data):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    def digest(self, name):
        return hashlib.sha256((self.root / name).read_bytes()).hexdigest()

    def receipts(self):
        for name, value in (("native-audio", self.audio), ("dxvk", self.dxvk), ("turnip-runtime", self.turnip)):
            self.write(f"build/{name}/build-receipt.json", json.dumps(value).encode())

    def test_complete_bundle_deduplicates_identical_cpp_runtime(self):
        runtime, pins, licenses = build_android.native_dependencies()
        self.assertEqual(set(runtime), {"libopenal.so", "libc++_shared.so", "libdxvk_d3d9.so", "libadrenotools.so", "libhook_impl.so", "libmain_hook.so", "libfile_redirect_hook.so", "libgsl_alloc_hook.so", "libut3_turnip.so"})
        self.assertIn(self.root / "build/dxvk/libut3_dxvk.a", pins)
        self.assertEqual(len(licenses), 6)

    def test_modified_turnip_driver_is_rejected(self):
        self.write("build/turnip-runtime/runtime/libut3_turnip.so", self.elf + b"changed")
        with self.assertRaisesRegex(ValueError, "hash mismatch"):
            build_android.native_dependencies()

    def test_modified_runtime_is_rejected(self):
        self.write("build/native-audio/runtime/libopenal.so", self.elf + b"changed")
        with self.assertRaisesRegex(ValueError, "hash mismatch"):
            build_android.native_dependencies()

    def test_conflicting_cpp_runtimes_are_rejected_even_with_valid_individual_pins(self):
        path = "build/dxvk/libc++_shared.so"
        self.write(path, self.elf + b"different valid DSO")
        self.dxvk["outputs"][1]["sha256"] = self.digest(path)
        self.receipts()
        with self.assertRaisesRegex(ValueError, "Conflicting native runtime"):
            build_android.native_dependencies()

    def test_wrong_architecture_is_rejected(self):
        path = "build/dxvk/libdxvk_d3d9.so"
        self.write(path, self.elf[:18] + b"\x3e\x00")
        self.dxvk["outputs"][0]["sha256"] = self.digest(path)
        self.receipts()
        with self.assertRaisesRegex(ValueError, "AArch64 ELF64"):
            build_android.native_dependencies()

    def test_missing_com_adapter_is_rejected(self):
        self.dxvk["outputs"].pop()
        self.receipts()
        with self.assertRaisesRegex(ValueError, "lacks"):
            build_android.native_dependencies()


if __name__ == "__main__":
    unittest.main()
