"""DEVICE-run the raw Vulkan occlusion query oracle (tests/test_vulkan_query_pools_gpu.cpp) on UT3_TEST_ANDROID.

Built with the NDK (glslc for its two shaders, clang++ with 16 KB page alignment, which Android 15+ phones such
as the Pixel 10a need) and run from /data/local/tmp. It checks the system driver's occlusion queries that
android/dxvk/query_pools.inc relies on: every slot of pools up to 4096 entries (the PowerVR pool) returns the real
sample count, a depth-hidden draw returns 0. The render-split cost of query pool changes inside a render is
printed (the measurement behind query_pools.inc), not asserted: it is the driver's property, not the port's.

Skipped without UT3_TEST_ANDROID. No window is used: a locked phone runs it as well."""
import os
from pathlib import Path
import re
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/tests/vulkan-query-pools"
NDK = Path(os.environ.get("ANDROID_NDK_HOME", Path.home() / "AppData/Local/Android/Sdk/ndk/27.2.12479018"))
ADB = Path(os.environ.get("ANDROID_SDK_ROOT", Path.home() / "AppData/Local/Android/Sdk")) / "platform-tools/adb.exe"
REMOTE = "/data/local/tmp/ut3-query-pools"

SHADERS = {
    "vert": """#version 450
layout(push_constant) uniform PC { vec4 rect; vec4 color; float z; } pc;
void main() {
  vec2 p[3] = vec2[](vec2(pc.rect.x, pc.rect.y), vec2(pc.rect.z, pc.rect.y), vec2(pc.rect.x, pc.rect.w));
  gl_Position = vec4(p[gl_VertexIndex], pc.z, 1.0);
}
""",
    "frag": """#version 450
layout(push_constant) uniform PC { vec4 rect; vec4 color; float z; } pc;
layout(location = 0) out vec4 outColor;
void main() { outColor = pc.color; }
""",
}


def build():
    OUT.mkdir(parents=True, exist_ok=True)
    lines = []
    for stage, source in SHADERS.items():
        (OUT / f"shader.{stage}").write_text(source, encoding="utf-8")
        subprocess.run([str(NDK / "shader-tools/windows-x86_64/glslc.exe"), f"-fshader-stage={stage}", "--target-env=vulkan1.1",
                        "-O", str(OUT / f"shader.{stage}"), "-o", str(OUT / f"shader.{stage}.spv")], check=True)
        data = (OUT / f"shader.{stage}.spv").read_bytes()
        words = ",".join(f"0x{int.from_bytes(data[i:i + 4], 'little'):08x}u" for i in range(0, len(data), 4))
        lines.append(f"static const uint32_t probe_{stage}[] = {{{words}}};")
    (OUT / "query_pools_spirv.inc").write_text("\n".join(lines) + "\n", encoding="utf-8")
    binary = OUT / "test_vulkan_query_pools_gpu"
    subprocess.run([str(NDK / "toolchains/llvm/prebuilt/windows-x86_64/bin/clang++.exe"), "--target=aarch64-linux-android29",
                    "-std=c++17", "-O2", "-Wall", "-static-libstdc++", "-Wl,-z,max-page-size=16384",
                    str(ROOT / "tests/test_vulkan_query_pools_gpu.cpp"), "-I", str(OUT), "-lvulkan", "-o", str(binary)], check=True)
    return binary


@unittest.skipUnless(os.environ.get("UT3_TEST_ANDROID"), "set UT3_TEST_ANDROID to a phone's adb serial")
class VulkanQueryPoolsGpuTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        binary = build()
        cls.adb = [str(ADB), "-s", os.environ["UT3_TEST_ANDROID"]]
        subprocess.run([*cls.adb, "shell", "mkdir", "-p", REMOTE], check=True)
        subprocess.run([*cls.adb, "push", str(binary), REMOTE + "/"], check=True, capture_output=True)
        subprocess.run([*cls.adb, "shell", "chmod", "755", REMOTE + "/test_vulkan_query_pools_gpu"], check=True)

    @classmethod
    def tearDownClass(cls):
        subprocess.run([*cls.adb, "shell", "rm", "-rf", REMOTE], check=False)

    def run_mode(self, *args):
        result = subprocess.run([*self.adb, "shell", REMOTE + "/test_vulkan_query_pools_gpu", *args],
                                capture_output=True, text=True, timeout=900)
        print(result.stdout)
        return result.stdout

    def test_pools_used_by_dxvk_are_exact(self):
        text = self.run_mode("correct")
        for size in (64, 128, 256, 512, 1024, 2048, 4096):
            self.assertRegex(text, rf"PASS pool +{size}:", f"pool {size}")
        text = self.run_mode("full")
        for size in (1024, 2048, 4096):
            self.assertEqual(len(re.findall(rf"PASS full pool +{size} frame", text)), 3, f"full pool {size}")

    def test_pool_change_cost_is_reported(self):
        text = self.run_mode("perf", "256", "20")
        gpu = {m[1].strip(): float(m[2]) for m in re.finditer(r"^(.+?) +draws \d+: wall median [0-9.]+ ms, gpu median ([0-9.]+) ms", text, re.M)}
        self.assertIn("one 1024 pool", gpu)
        self.assertIn("64-pools shuffled", gpu)
        print(f"GPU per frame: one pool {gpu['one 1024 pool']:.2f} ms, shuffled 64-entry pools {gpu['64-pools shuffled']:.2f} ms")


if __name__ == "__main__":
    unittest.main()
