"""HOST-run DXVK's SCALED vertex fallback SPIR-V patch (android/dxvk/scaled_vertex_spirv.inc) and spirv-val it.

DXVK calls convertIntegerInput once per converted vertex input on the same module: every unsigned
location, then every signed one (tools/build_dxvk.py). Types a call declares go right before the
first function, after the module's variables, so a later call must find them again: SPIR-V forbids
a second declaration of a non-aggregate type, and two UBYTE4 (or two SHORT) inputs once declared a
second OpTypeInt/OpTypeVector. Each module below is assembled at SPIR-V 1.3 (DXVK's dxso) and 1.6,
patched by tests/test_dxvk_scaled_spirv.cpp (DXVK's own SpirvCodeBuffer and eliminateInput from
build/dxvk-source, the overlay under test) and validated by the NDK's spirv-val for Vulkan 1.3.
Needs build/dxvk-source (tools/build_dxvk.py --rebuild stages it); skipped without it. No device."""
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import unittest

from tests.host_environment import environment

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/tests/dxvk-scaled-spirv"
DXVK = ROOT / "build/dxvk-source"
SDK = Path(os.environ.get("ANDROID_SDK_ROOT", Path.home() / "AppData/Local/Android/Sdk"))
SHADER_TOOLS = SDK / "ndk/27.2.12479018/shader-tools/windows-x86_64"

HEADER = """               OpCapability Shader
               OpMemoryModel Logical GLSL450
"""

# Two UBYTE4 and two SHORT2 inputs, no integer type declared: the review's case.
TWO_OF_EACH = HEADER + """               OpEntryPoint Vertex %main "main" %in0 %in1 %in2 %in3 %pos
               OpDecorate %in0 Location 0
               OpDecorate %in1 Location 1
               OpDecorate %in2 Location 2
               OpDecorate %in3 Location 3
               OpDecorate %pos BuiltIn Position
       %void = OpTypeVoid
     %fnvoid = OpTypeFunction %void
      %float = OpTypeFloat 32
    %v4float = OpTypeVector %float 4
    %v2float = OpTypeVector %float 2
  %ptr_in_v4 = OpTypePointer Input %v4float
  %ptr_in_v2 = OpTypePointer Input %v2float
 %ptr_out_v4 = OpTypePointer Output %v4float
        %in0 = OpVariable %ptr_in_v4 Input
        %in1 = OpVariable %ptr_in_v4 Input
        %in2 = OpVariable %ptr_in_v2 Input
        %in3 = OpVariable %ptr_in_v2 Input
        %pos = OpVariable %ptr_out_v4 Output
       %main = OpFunction %void None %fnvoid
      %entry = OpLabel
         %a0 = OpLoad %v4float %in0
         %a1 = OpLoad %v4float %in1
         %a2 = OpLoad %v2float %in2
         %a3 = OpLoad %v2float %in3
        %s01 = OpFAdd %v4float %a0 %a1
        %s23 = OpFAdd %v2float %a2 %a3
          %x = OpCompositeExtract %float %s23 0
          %y = OpCompositeExtract %float %s23 1
          %v = OpCompositeConstruct %v4float %x %y %x %y
        %sum = OpFAdd %v4float %s01 %v
               OpStore %pos %sum
               OpReturn
               OpFunctionEnd
"""

# UBYTE4, SHORT2 and a scalar from one unsigned format: one conversion of each kind per type.
MIXED = HEADER + """               OpEntryPoint Vertex %main "main" %in0 %in2 %in4 %pos
               OpDecorate %in0 Location 0
               OpDecorate %in2 Location 2
               OpDecorate %in4 Location 4
               OpDecorate %pos BuiltIn Position
       %void = OpTypeVoid
     %fnvoid = OpTypeFunction %void
      %float = OpTypeFloat 32
    %v4float = OpTypeVector %float 4
    %v2float = OpTypeVector %float 2
  %ptr_in_v4 = OpTypePointer Input %v4float
  %ptr_in_v2 = OpTypePointer Input %v2float
   %ptr_in_f = OpTypePointer Input %float
 %ptr_out_v4 = OpTypePointer Output %v4float
        %in0 = OpVariable %ptr_in_v4 Input
        %in2 = OpVariable %ptr_in_v2 Input
        %in4 = OpVariable %ptr_in_f Input
        %pos = OpVariable %ptr_out_v4 Output
       %main = OpFunction %void None %fnvoid
      %entry = OpLabel
         %a0 = OpLoad %v4float %in0
         %a2 = OpLoad %v2float %in2
         %a4 = OpLoad %float %in4
          %x = OpCompositeExtract %float %a2 0
          %v = OpCompositeConstruct %v4float %x %a4 %x %a4
        %sum = OpFAdd %v4float %a0 %v
               OpStore %pos %sum
               OpReturn
               OpFunctionEnd
"""

# Two SHORT4 inputs, signed only.
TWO_SHORT4 = HEADER + """               OpEntryPoint Vertex %main "main" %in0 %in1 %pos
               OpDecorate %in0 Location 0
               OpDecorate %in1 Location 1
               OpDecorate %pos BuiltIn Position
       %void = OpTypeVoid
     %fnvoid = OpTypeFunction %void
      %float = OpTypeFloat 32
    %v4float = OpTypeVector %float 4
  %ptr_in_v4 = OpTypePointer Input %v4float
 %ptr_out_v4 = OpTypePointer Output %v4float
        %in0 = OpVariable %ptr_in_v4 Input
        %in1 = OpVariable %ptr_in_v4 Input
        %pos = OpVariable %ptr_out_v4 Output
       %main = OpFunction %void None %fnvoid
      %entry = OpLabel
       %tmp0 = OpVariable %ptr_fn_v4 Function
         %a0 = OpLoad %v4float %in0
         %a1 = OpLoad %v4float %in1
        %sum = OpFAdd %v4float %a0 %a1
               OpStore %tmp0 %sum
         %t0 = OpLoad %v4float %tmp0
               OpStore %pos %t0
               OpReturn
               OpFunctionEnd
""".replace(" %ptr_out_v4 = OpTypePointer Output %v4float\n",
            " %ptr_out_v4 = OpTypePointer Output %v4float\n  %ptr_fn_v4 = OpTypePointer Function %v4float\n")

# The module's own uint types come after its input variables (as dxso declares types on first use),
# and an access chain reads one component of the converted input.
INT_AFTER_INPUTS = HEADER + """               OpEntryPoint Vertex %main "main" %in0 %pos
               OpDecorate %in0 Location 0
               OpDecorate %pos BuiltIn Position
       %void = OpTypeVoid
     %fnvoid = OpTypeFunction %void
      %float = OpTypeFloat 32
    %v4float = OpTypeVector %float 4
  %ptr_in_v4 = OpTypePointer Input %v4float
 %ptr_out_v4 = OpTypePointer Output %v4float
        %in0 = OpVariable %ptr_in_v4 Input
        %pos = OpVariable %ptr_out_v4 Output
       %uint = OpTypeInt 32 0
     %v4uint = OpTypeVector %uint 4
     %uint_1 = OpConstant %uint 1
   %ptr_in_f = OpTypePointer Input %float
       %main = OpFunction %void None %fnvoid
      %entry = OpLabel
         %pc = OpAccessChain %ptr_in_f %in0 %uint_1
          %c = OpLoad %float %pc
         %a0 = OpLoad %v4float %in0
          %v = OpVectorTimesScalar %v4float %a0 %c
               OpStore %pos %v
               OpReturn
               OpFunctionEnd
"""

# uint, uvec4 and its Input pointer exist before the inputs (an integer input at location 2):
# nothing new may be declared for them, and the integer input itself is left alone.
INT_BEFORE_INPUTS = HEADER + """               OpEntryPoint Vertex %main "main" %in0 %in1 %raw %pos
               OpDecorate %in0 Location 0
               OpDecorate %in1 Location 1
               OpDecorate %raw Location 2
               OpDecorate %pos BuiltIn Position
       %void = OpTypeVoid
     %fnvoid = OpTypeFunction %void
      %float = OpTypeFloat 32
    %v4float = OpTypeVector %float 4
       %uint = OpTypeInt 32 0
     %v4uint = OpTypeVector %uint 4
 %ptr_in_v4u = OpTypePointer Input %v4uint
  %ptr_in_v4 = OpTypePointer Input %v4float
 %ptr_out_v4 = OpTypePointer Output %v4float
        %in0 = OpVariable %ptr_in_v4 Input
        %in1 = OpVariable %ptr_in_v4 Input
        %raw = OpVariable %ptr_in_v4u Input
        %pos = OpVariable %ptr_out_v4 Output
       %main = OpFunction %void None %fnvoid
      %entry = OpLabel
         %a0 = OpLoad %v4float %in0
         %a1 = OpLoad %v4float %in1
          %r = OpLoad %v4uint %raw
         %rf = OpConvertUToF %v4float %r
        %s01 = OpFAdd %v4float %a0 %a1
        %sum = OpFAdd %v4float %s01 %rf
               OpStore %pos %sum
               OpReturn
               OpFunctionEnd
"""

# name, module, conversions in DXVK's order (unsigned locations ascending, then signed), and the
# number of ConvertUToF / ConvertSToF the patch must add (location 2 of INT_BEFORE_INPUTS is no float).
CASES = [
    ("two_ubyte4_two_short2", TWO_OF_EACH, ["u0", "u1", "s2", "s3"], 2, 2),
    ("mixed_signedness", MIXED, ["u0", "u4", "s2"], 2, 1),
    ("two_short4", TWO_SHORT4, ["s0", "s1"], 0, 2),
    ("int_types_after_inputs", INT_AFTER_INPUTS, ["u0"], 1, 0),
    ("int_types_before_inputs", INT_BEFORE_INPUTS, ["u0", "u1", "u2"], 2, 0),
]


def function(text, name):
    start = text.rfind("\n", 0, text.index(name)) + 1
    depth, end = 1, text.index("{", start) + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end] + "\n"


def build_harness(overlay_dir, out):
    """The harness with the overlay found in overlay_dir (android/dxvk, or a copy to compare with)."""
    out.mkdir(parents=True, exist_ok=True)
    shader = (DXVK / "src/dxvk/dxvk_shader.cpp").read_text(encoding="utf-8")
    eliminate = function(shader, "  void DxvkShader::eliminateInput(SpirvCodeBuffer& code, uint32_t location) {")
    pristine = ROOT / "reference/dxvk/src/dxvk/dxvk_shader.cpp"
    if pristine.exists():
        # The upstream function at the pinned revision, not something an overlay changed.
        pins = json.loads((ROOT / "dependencies-dxvk.json").read_text(encoding="utf-8"))["source_files"]
        assert hashlib.sha256(pristine.read_bytes()).hexdigest() == pins["src/dxvk/dxvk_shader.cpp"]
        assert function(pristine.read_text(encoding="utf-8"), "  void DxvkShader::eliminateInput(") == eliminate
    (out / "eliminate_input.inc").write_text(eliminate, encoding="utf-8")
    env, linker = environment("x64")
    # The installed VS2026 STL needs a newer Clang than the NDK's clang-cl: MSVC's own cl for C++.
    for command in (
        [linker.parent / "cl.exe", "/nologo", "/O2", "/std:c++17", "/EHsc", "/W3", "/D_CRT_SECURE_NO_WARNINGS",
         "/DNOMINMAX", "/I" + str(DXVK / "src"),
         "/I" + str(DXVK / "include/spirv/include"), "/I" + str(out), "/I" + str(overlay_dir),
         "/c", ROOT / "tests/test_dxvk_scaled_spirv.cpp", "/Fo" + str(out / "harness.obj")],
        [linker, "/nologo", out / "harness.obj", "/OUT:" + str(out / "harness.exe")]):
        result = subprocess.run([str(part) for part in command], cwd=out, env=env, capture_output=True, timeout=300)
        if result.returncode:
            raise AssertionError((result.stdout + result.stderr).decode(errors="replace"))
    return out / "harness.exe"


def tool(name, *args):
    return subprocess.run([str(SHADER_TOOLS / f"{name}.exe"), *map(str, args)], capture_output=True, text=True, timeout=60)


def run_case(harness, out, name, module, conversions, version):
    """(spirv-val result of the patched module, disassembly before, disassembly after)."""
    source, original, patched = (out / f"{name}-{version}{suffix}" for suffix in (".spvasm", ".spv", "-patched.spv"))
    source.write_text(module, encoding="utf-8")
    assembled = tool("spirv-as", "--target-env", version, "-o", original, source)
    assert assembled.returncode == 0, assembled.stderr
    valid = tool("spirv-val", "--target-env", "vulkan1.3", original)
    assert valid.returncode == 0, f"fixture {name} {version} itself is invalid: {valid.stderr}"
    result = subprocess.run([str(harness), str(original), str(patched), *conversions], capture_output=True, text=True,
                            timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr
    before = tool("spirv-dis", "--raw-id", "--no-header", original).stdout
    after = tool("spirv-dis", "--raw-id", "--no-header", patched).stdout
    return tool("spirv-val", "--target-env", "vulkan1.3", patched), before, after


@unittest.skipUnless((DXVK / "src/spirv/spirv_code_buffer.cpp").exists() and (SHADER_TOOLS / "spirv-val.exe").exists(),
                     "needs build/dxvk-source (tools/build_dxvk.py --rebuild) and the NDK shader tools")
class ScaledVertexSpirvTest(unittest.TestCase):
    def test_conversions_validate(self):
        harness = build_harness(ROOT / "android/dxvk", OUT)
        for name, module, conversions, unsigned, signed in CASES:
            for version in ("spv1.3", "spv1.6"):
                with self.subTest(case=name, version=version):
                    valid, before, after = run_case(harness, OUT, name, module, conversions, version)
                    self.assertEqual(valid.returncode, 0, valid.stdout + valid.stderr)
                    count = lambda text, op: len(re.findall(rf"= {op} ", text))
                    self.assertEqual(count(after, "OpConvertUToF") - count(before, "OpConvertUToF"), unsigned)
                    self.assertEqual(count(after, "OpConvertSToF") - count(before, "OpConvertSToF"), signed)
                    # One declaration per integer type, and per type one Input pointer that was not there before.
                    ints = Counter(re.findall(r"= OpTypeInt (32 [01])", after))
                    self.assertTrue(all(n == 1 for n in ints.values()), ints)
                    vectors = Counter(re.findall(r"= OpTypeVector (%\d+ \d)", after))
                    self.assertTrue(all(n == 1 for n in vectors.values()), vectors)
                    # Every converted location keeps exactly one decoration, on an Input variable.
                    for location in {int(c[1:]) for c in conversions}:
                        decorated = re.findall(rf"OpDecorate (%\d+) Location {location}\n", after)
                        self.assertEqual(len(decorated), 1, (location, decorated))
                        self.assertRegex(after, rf"{decorated[0]} = OpVariable %\d+ Input\n")


if __name__ == "__main__":
    unittest.main()
