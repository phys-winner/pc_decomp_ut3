"""Compile the statically lifted UT3 code and Win32 host to an ARM64 executable."""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import x87_slots

# Lifted modules whose functions keep their x87 stack in C locals (tools/x87_slots.py): the game's own
# scalar float code, PhysXCore (its CPU solver carries the PhysX Mod Pack maps), the PhysX cooking and
# character libraries, and Bink, Vorbis and the resampler. A function with another x87 interface keeps
# the global window; so do the CRT's math helpers (msvcr80), whose interfaces are x87 values.
X87_SLOT_MODULES = ("ut3", "physxcore", "nxcooking", "nxcharacter", "binkw32", "vorbis", "libresample")

UE3_NATIVE_TARGETS = [
    "sub_00486030", "sub_0117C3B0",  # UE3 matrix products and sphere culling
    "sub_0117D890",  # Light/primitive sphere rejection; other cases keep the lifted predicate
    "sub_006044F0", "sub_00B65BD0", "sub_00BF8120",  # Actor transforms and tick containers
    "sub_01178E50", "sub_01178EA0",  # Per-view primitive lookup
    "sub_00F058D0", "sub_0088B010",  # Draw traversal and actor iterator
    "sub_00F05700", "sub_00F05AA0", "sub_00EFE060", "sub_00EFE230",  # The same draw traversal, other draw lists
    "sub_00F05530",  # The 16-byte draw-list traversal with the sparse array at +0x28
    "sub_00F01CF0",  # Connected primitive and light visibility traversal
    "sub_005DAF50",  # Sparse allocation-bit iterator initialization
    "sub_00BF81A0", "sub_00BF82A0", "sub_00BF82F0", "sub_00BF8340",  # Visibility bit-array storage
    "sub_00EFF830",  # Per-view light-array reset
    "sub_0117AEC0", "sub_01178C60",  # Temporal occlusion decision and bounds preparation
    "sub_010275E0",  # AI navigation graph search
    "sub_006A97C0", "sub_00EA3C60", "sub_01079C10", "sub_01079F80",  # ReachSpec support/cost and edge lookup
    "sub_00FC8DE0", "sub_00FCBCB0",  # AI perception eligibility and controller traversal
    "sub_01156630",  # Segment/AABB query used by perception and movement traces
    "sub_004B49A0",  # TMap<FName, V>::Find (core_native.c; 00B65BD0 is its TMap<int, V> sibling)
    "sub_004B9D60", "sub_00F78830", "sub_00E2DC90",  # TMap finds with object and two-word keys (core_native.c)
    "sub_00BC1310", "sub_00BC12C0",  # The script VM's instance and local variable opcodes (core_native.c)
    "sub_00EC2580", "sub_00EC2610", "sub_00EC26A0", "sub_00EC2730",  # Shader parameter descriptor bridges
    "sub_0116C250",  # Segment/box slab test of collision checks (collision_native.c)
    "sub_00BF8DA0",  # Baked distribution lookup for particles (particle_native.c)
    "sub_01590CF0",  # D3D9 RHI query wait: poll before reading the clock (query_native.c)
]
WRAPS = [
    "sub_00F75DB0", "sub_00486270", "sub_00C85710", "sub_00C12290", "sub_00B6AF10", "sub_010FC820",
    "sub_1E23D3A0", "sub_1E23A700", "sub_1E23D300", "sub_1E213B70",  # PhysX collision/pair kernels
] + UE3_NATIVE_TARGETS
# These native entries preserve the lifted summary's caller-visible contract (0x17, flags dead).
NATIVE_CALL_CONTRACT = {"sub_00EC2580", "sub_00EC2610", "sub_00EC26A0", "sub_00EC2730", "sub_0117D890"}

ROOT = Path(__file__).resolve().parents[1]
# The inputs of the three optimisations of the lifted code that are on by default (see apply_protocol_defaults):
# a tools/call_protocol.py summary and a tools/x87_locals.py plan (both made on first use, about 15 minutes, from
# the same PE images and generated sources the build compiles) and the list of the bodies a WAR-Torlan match runs.
DEFAULT_CALL_PROTOCOL = ROOT / "build/call-protocol-v2/summary.json"
DEFAULT_X87_LOCALS = ROOT / "build/call-protocol-v2/x87-v3.json"
DEFAULT_WARM_FUNCTIONS = ROOT / "config/warm-functions.txt"
# Host translation units built with the generated code's separated guest-state profile
# (android/runtime/guest_state_prefix.h): they work on the global guest registers and guest memory only
# through the runtime macros and define no identifiers named like the register aliases.
SEPARATED_HOST_SOURCES = (
    "render_visibility_native.c", "game_native.c", "render_native.c", "render_hash_native.c",
    "render_draw_native.c", "ai_native.c", "ai_reach_native.c", "ai_trace_native.c", "core_native.c",
    "collision_native.c", "crt_native.c", "particle_native.c",
)
SDK = Path(os.environ.get("ANDROID_SDK_ROOT", Path.home() / "AppData/Local/Android/Sdk"))
MOJO_SOURCES = ["mojoshader.c", "mojoshader_common.c", "mojoshader_assembler.c",
                "mojoshader_preprocessor.c", "mojoshader_lexer.c",
                "profiles/mojoshader_profile_common.c", "profiles/mojoshader_profile_glsl.c",
                "profiles/mojoshader_profile_d3d.c", "profiles/mojoshader_profile_bytecode.c"]


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


DISPATCH_ADD = re.compile(r'void_dispatch_add\(\s*([A-Za-z_][A-Za-z0-9_]*|0x[0-9A-Fa-f]+)u?\s*,')
NAMED_VA = r'(?:#define\s+{0}\s+|\b{0}\s*=\s*)(0x[0-9A-Fa-f]+)u?\b'


def host_overrides(host_sources):
    """Guest VAs the host replaces through void_dispatch_add, as lifted names.

    Callers of these bodies keep the full call protocol: the host replacement may read any register."""
    names = set()
    for source in host_sources:
        text = source.read_text(encoding="utf-8", errors="replace")
        for token in DISPATCH_ADD.findall(text):
            if token.startswith("0x"):
                names.add("sub_%08X" % int(token, 16))
                continue
            if token in ("va", "old"):
                continue   # core.c's own registration helpers
            match = re.search(NAMED_VA.format(re.escape(token)), text)
            if not match:
                raise ValueError(f"{source.name}: cannot resolve the address of void_dispatch_add({token}, ...)")
            names.add("sub_%08X" % int(match[1], 16))
    return names


def native_same_unit_calls(text):
    # --wrap only redirects undefined symbols; a call beside its definition can be bound or inlined first.
    defined = set(re.findall(r'^void (sub_[0-9A-F]{8})\(void\) \{', text, re.M))
    local = [name for name in UE3_NATIVE_TARGETS if name in defined]
    if not local:
        return text, 0
    called = set()
    def redirect(match):
        called.add(match[1])
        return 'RECOMP_CALL(__wrap_' + match[1] + ')'
    rewritten, count = re.subn(r'\bRECOMP_CALL\((' + '|'.join(local) + r')\)', redirect, text)
    if not count:
        return text, 0
    declarations = ''.join('extern void __wrap_' + name + '(void);\n' for name in sorted(called))
    return declarations + rewritten.replace('#include "functions.h"', '#include "ut3/functions.h"', 1), count


def validate_native_call_contract(protocol):
    for name in NATIVE_CALL_CONTRACT:
        if protocol.call_masks(name) != (0x17, 0x17, 0) or name in protocol.flags_out:
            raise ValueError("Native call contract differs from the proven summary: " + name)


def native_dependencies(prepare=False):
    if prepare:
        for tool in ("build_native_audio.py", "build_dxvk.py", "build_turnip.py", "build_texconv.py",
                     "build_vkd3d.py"):
            subprocess.run([sys.executable, str(ROOT / "tools" / tool)], cwd=ROOT, check=True)
    pinned, runtime = {}, {}

    def pin(path, expected):
        if not path.is_file() or sha(path) != expected:
            raise ValueError(f"Native dependency hash mismatch or missing file: {path}")
        pinned[path] = expected

    def library(path, expected):
        pin(path, expected)
        data = path.read_bytes()[:20]
        if data[:6] != b"\x7fELF\x02\x01" or int.from_bytes(data[18:20], "little") != 183:
            raise ValueError(f"Native runtime must be little-endian AArch64 ELF64: {path}")
        if path.name in runtime and sha(runtime[path.name]) != expected:
            raise ValueError(f"Conflicting native runtime libraries: {path.name}")
        runtime[path.name] = path

    audio_manifest = ROOT / "dependencies-audio.json"
    audio = json.loads((ROOT / "build/native-audio/build-receipt.json").read_text())
    pin(audio_manifest, audio["manifest_sha256"])
    pin(ROOT / "tools/build_native_audio.py", audio["tool_sha256"])
    for name, expected in audio["runtime"].items():
        library(ROOT / "build/native-audio/runtime" / name, expected)
    licenses = []
    for name, expected in audio["licenses"].items():
        path = ROOT / "build/native-audio/licenses" / name
        pin(path, expected)
        licenses.append(path)
    dxvk = json.loads((ROOT / "build/dxvk/build-receipt.json").read_text())
    pin(ROOT / "tools/build_dxvk.py", dxvk["build_tool_sha256"])
    pin(ROOT / "android/dxvk/wsi_android.cpp", dxvk["wsi_sha256"])
    for name in ("android/dxvk/bridge.cpp", "tools/generate_dxvk_bridge.py", "build/dxvk/guest_methods.inc"):
        path = ROOT / name
        pinned[path] = sha(path)
    for item in dxvk["outputs"]:
        path = ROOT / item["path"]
        if path.suffix == ".so":
            library(path, item["sha256"])
        else:
            pin(path, item["sha256"])
    archive = ROOT / "build/dxvk/libut3_dxvk.a"
    if archive not in pinned or set(runtime) != {"libopenal.so", "libc++_shared.so", "libdxvk_d3d9.so"}:
        raise ValueError("Native bundle lacks the audio runtime, DXVK D3D9 or its COM adapter")
    turnip = json.loads((ROOT / "build/turnip-runtime/build-receipt.json").read_text())
    pin(ROOT / "tools/build_turnip.py", turnip["build_tool_sha256"])
    pin(ROOT / "dependencies-turnip.json", turnip["input_pins_sha256"])
    pinned[ROOT / "android/dxvk/turnip.inc"] = sha(ROOT / "android/dxvk/turnip.inc")
    pinned[ROOT / "android/dxvk/query_wait.inc"] = sha(ROOT / "android/dxvk/query_wait.inc")
    for item in turnip["outputs"]:
        library(ROOT / item["path"], item["sha256"])
    for item in turnip["licenses"]:
        path = ROOT / item["path"]
        pin(path, item["sha256"])
        licenses.append(path)
    for name in ("LICENSE", "include/native/directx/COPYING.MinGW-w64.txt"):
        path = ROOT / "reference/dxvk" / name
        pinned[path] = sha(path)
        licenses.append(path)
    texture = json.loads((ROOT / "build/texconv/receipt.json").read_text())
    pin(ROOT / "dependencies-textures.json", texture["manifest_sha256"])
    pin(ROOT / "tools/build_texconv.py", texture["tool_sha256"])
    for name, expected in texture["sources"].items():
        pin(ROOT / name, expected)
    library(ROOT / "build/texconv/libtexconv.so", texture["sha256"])
    for directory in ("reference/astcenc", "reference/etcpak"):
        for path in (ROOT / directory).glob("LICENSE*"):
            pinned[path] = sha(path)
            licenses.append(path)
    # vkd3d-shader: the device's HLSL compiler (android/host/hlsl_vkd3d.c), linked into libut3.so.
    vkd3d = json.loads((ROOT / "build/vkd3d/receipt.json").read_text(encoding="utf-8"))
    pin(ROOT / "tools/build_vkd3d.py", vkd3d["tool_sha256"])
    pin(ROOT / "dependencies-vkd3d.json", vkd3d["manifest_sha256"])
    pin(ROOT / vkd3d["library"], vkd3d["sha256"])
    for name, digest in vkd3d["headers"].items():
        pin(ROOT / name, digest)
    for name in vkd3d["licenses"]:
        pinned[ROOT / name] = sha(ROOT / name)
        licenses.append(ROOT / name)
    return runtime, pinned, licenses


def apply_protocol_defaults(parser, args):
    """The masked call protocol, the warm bodies' site caches and the x87 locals are on unless opted out.

    Only a plain build of the game: a --generated tree other than the game's has no analysis to use, and the
    investigation builds keep the full protocol, because their call and function rings are filled by
    RECOMP_CALL/RECOMP_ICALL and not by the masked RECOMP_CALL_P forms (recomp_types.h). Those builds still
    take the three options when they are named explicitly.
    """
    plain = args.generated.resolve() == (ROOT / "generated").resolve() and not (args.diagnostics or args.function_coverage)
    defaults = [("call_protocol", DEFAULT_CALL_PROTOCOL), ("warm_functions", DEFAULT_WARM_FUNCTIONS), ("x87_locals", DEFAULT_X87_LOCALS)]
    for name, default in defaults:
        if getattr(args, name) is not None and getattr(args, "no_" + name):
            parser.error(f"--{name.replace('_', '-')} and --no-{name.replace('_', '-')} exclude each other")
    # What the masked protocol needs follows it: --no-call-protocol alone also turns off the other two.
    if plain and not args.no_call_protocol:
        for name, default in defaults:
            if getattr(args, name) is None and not getattr(args, "no_" + name):
                setattr(args, name, default)
    for name, _ in defaults:
        path = getattr(args, name)
        if path is not None and not path.is_file() and not make_protocol_input(path):
            parser.error(f"--{name.replace('_', '-')}: missing file {path}")


def make_protocol_input(path):
    """Creates a missing default analysis file: the same tool and inputs the build would use by hand."""
    tools = {DEFAULT_CALL_PROTOCOL: "call_protocol.py", DEFAULT_X87_LOCALS: "x87_locals.py"}
    tool = tools.get(path)
    if tool is None:
        return False
    print(f"{path.relative_to(ROOT)} is missing: running tools/{tool} (about 15 minutes)", flush=True)
    subprocess.run([sys.executable, str(ROOT / "tools" / tool), "--out", str(path)], cwd=ROOT, check=True)
    return path.is_file()


def generated_sources(folder):
    if folder == ROOT / "build/host-smoke":
        return [folder / "dispatch.c"], {}
    specs = json.loads((ROOT / "config/modules.json").read_text())["modules"]
    included = re.findall(r'^\s*\{\s*"([^"]+)"', (folder / "modules.inc").read_text(), re.M)
    by_file = {row["file"]: row for row in specs}
    if not included or len(set(included)) != len(included) or set(included) - by_file.keys():
        raise ValueError("Generated module list is empty, duplicated or unknown")
    if not any(by_file[name].get("exe") for name in included):
        raise ValueError("Generated module list has no executable")
    sources, pinned = [folder / "dispatch.c"], {}
    for name in included:
        spec = by_file[name]
        directory = folder / spec["name"]
        report_path = directory / "report.json"
        report = json.loads(report_path.read_text())
        if report.get("status") != "GENERATED":
            raise ValueError(f"Static generation is incomplete: {name}")
        image = folder / "input" / spec["file"]
        if sha(image) != report.get("image_sha256") or report.get("original_sha256") != spec["sha256"]:
            raise ValueError(f"Generated input hash differs from its report: {name}")
        pinned[image] = sha(image)
        pinned[report_path] = sha(report_path)
        for relative in report["sources"]:
            source = (directory / relative).resolve()
            if source.parent != directory or source.suffix != ".c" or not source.is_file():
                raise ValueError(f"Invalid generated source: {relative}")
            sources.append(source)
    pinned[folder / "modules.inc"] = sha(folder / "modules.inc")
    pinned[folder / "dispatch.c"] = sha(folder / "dispatch.c")
    return sources, pinned


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--generated", type=Path, default=ROOT / "generated")
    parser.add_argument("--output", type=Path, default=ROOT / "build/android/ut3-arm64")
    parser.add_argument("--jobs", type=int, default=min(8, os.cpu_count() or 1))
    parser.add_argument("--shared", action="store_true", help="Build the APK's libut3.so")
    parser.add_argument("--diagnostics", action="store_true", help="Record guest function/call rings for crash investigations")
    parser.add_argument("--function-coverage", action="store_true", help="Record exact guest function-entry sets in a diagnostic build")
    parser.add_argument("--rt-check", action="store_true",
                        help="Check that every GL call of the D3D9 files runs on the render thread (d3d9_thread.inc)")
    parser.add_argument("--call-protocol", type=Path, metavar="SUMMARY",
                        help="Local guest registers with the masked call protocol of this tools/call_protocol.py summary; "
                             "on by default (build/call-protocol-v2/summary.json), see --no-call-protocol")
    parser.add_argument("--no-call-protocol", action="store_true",
                        help="Keep the full call protocol (and with it no --warm-functions or --x87-locals)")
    parser.add_argument("--warm-functions", type=Path, metavar="LIST",
                        help="With the call protocol: per-site indirect-call caches in the bodies named here (one per "
                             "line); on by default (config/warm-functions.txt)")
    parser.add_argument("--no-warm-functions", action="store_true", help="The shared resolve cache for every body")
    parser.add_argument("--protocol-check", action="store_true",
                        help="With the call protocol: log every callee that changes registers its summary preserves")
    parser.add_argument("--x87-locals", type=Path, metavar="PLAN",
                        help="With the call protocol: the x87 stack in C locals for every body this tools/x87_locals.py "
                             "plan places, where tools/x87_slots.py kept the window; on by default "
                             "(build/call-protocol-v2/x87-v3.json)")
    parser.add_argument("--no-x87-locals", action="store_true", help="tools/x87_slots.py's result only")
    args = parser.parse_args()
    apply_protocol_defaults(parser, args)
    if args.protocol_check and not args.call_protocol:
        parser.error("--protocol-check needs --call-protocol")
    if args.x87_locals and not args.call_protocol:
        parser.error("--x87-locals needs --call-protocol")
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    runtime, native_pinned, _ = native_dependencies(prepare=True)
    ndk = Path(os.environ.get("ANDROID_NDK_HOME", SDK / "ndk/27.2.12479018"))
    clang = ndk / "toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe"
    if not clang.is_file():
        parser.error(f"NDK compiler not found: {clang}")
    generated = args.generated.resolve()
    if not (generated / "modules.inc").is_file():
        parser.error(f"missing module list {generated / 'modules.inc'}")
    lifted, pinned = generated_sources(generated)
    pinned.update(native_pinned)
    out = args.output.resolve()
    if args.shared and out == ROOT / "build/android/ut3-arm64":
        out = ROOT / "build/android/libut3.so"
    out.parent.mkdir(parents=True, exist_ok=True)
    host_sources = sorted((ROOT / "android/host").glob("*.c"))
    coverage = None
    if args.function_coverage:
        from function_coverage import CoveragePlan
        coverage = CoveragePlan.create(generated, lifted, host_sources, WRAPS, out.parent / "function_coverage")
    compiler = subprocess.check_output([str(clang), "--version"], text=True)
    common = ["--target=aarch64-linux-android29", "-O2", "-fno-strict-aliasing", "-fwrapv",
              "-fvisibility=hidden", "-fPIC", "-ffp-contract=off", "-fno-math-errno", "-g0",
              "-DRECOMP_X87_WINDOW", "-Iandroid/runtime", "-Iandroid/host",
              "-I" + str(generated), "-Ireference/mojoshader"]
    if args.diagnostics:
        common += ["-DRECOMP_TRACE", "-DRECOMP_CALL_DIAG"]
    if coverage:
        common += ["-DVOID_FUNCTION_COVERAGE", "-I" + str(coverage.output), "-I" + str(ROOT / "tools")]
    if args.shared:
        common.append("-DUT3_ANDROID_APP")
    headers = sorted((ROOT / "android").rglob("*.h")) + sorted(generated.rglob("*.h"))
    headers += [generated / "modules.inc", ROOT / "reference/dlmalloc/malloc.c"]
    headers += sorted((ROOT / "reference/mojoshader").rglob("*.h"))
    if coverage:
        headers += [coverage.header, ROOT / "tools/function_coverage_runtime.h"]
    inputs = hashlib.sha256((str(clang) + compiler).encode())
    for path in headers:
        inputs.update(str(path).encode())
        inputs.update(path.read_bytes())
    base_digest = inputs.digest()
    tasks = []
    generated_flags = common + ["-w", "-DRECOMP_GENERATED_CODE", "-DRECOMP_GUEST_STATE_SEPARATE",
                                "-mllvm", "-basic-aa-separate-storage", "-include",
                                str(ROOT / "android/runtime/guest_state_prefix.h")]
    protocol = None
    protocol_counts = [0, 0]
    if args.call_protocol:
        from protocol_transform import Protocol, apply_protocol
        summary = args.call_protocol.resolve()
        protocol_full = (set(WRAPS) | host_overrides(host_sources)) - NATIVE_CALL_CONTRACT
        warm = set()
        if args.warm_functions:
            warm = {line.strip() for line in args.warm_functions.read_text(encoding="utf-8").splitlines()
                    if line.strip().startswith("sub_")}
            pinned[args.warm_functions.resolve()] = sha(args.warm_functions.resolve())
        protocol = Protocol(summary, full=protocol_full, warm=warm, masked_native=NATIVE_CALL_CONTRACT)
        validate_native_call_contract(protocol)
        pinned[summary] = sha(summary)
        pinned[ROOT / "tools/protocol_transform.py"] = sha(ROOT / "tools/protocol_transform.py")
        generated_flags += ["-DRECOMP_LOCAL_REGS"] + (["-DRECOMP_PROTOCOL_CHECK"] if args.protocol_check else [])
    x87_plan = None
    if args.x87_locals:
        import x87_locals
        x87_plan = x87_locals.Plan(args.x87_locals.resolve())
        pinned[args.x87_locals.resolve()] = sha(args.x87_locals.resolve())
        pinned[ROOT / "tools/x87_locals.py"] = sha(ROOT / "tools/x87_locals.py")
    # The rewritten sources are made from the game's lifted code: generated into the build tree only.
    # x87_slots/manifest.json remembers, per lifted source and x87_slots.py version, what the rewrite
    # gave, so an unchanged source is not analysed again.
    x87_counts = [0, 0]
    x87_dir = out.parent / "x87_slots"
    x87_manifest_path = x87_dir / "manifest.json"
    x87_tool = sha(ROOT / "tools/x87_slots.py")
    try:
        x87_manifest = json.loads(x87_manifest_path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        x87_manifest = {}
    x87_next = {}
    native_calls = 0
    protocol_modules = {row["name"] for row in json.loads((ROOT / "config/modules.json").read_text())["modules"]}
    for source in lifted:
        module = source.parent.name
        if source.parent.name in X87_SLOT_MODULES and source.parent.parent == generated:
            key = source.relative_to(generated).as_posix()
            target = x87_dir / source.parent.name / source.name
            source_sha = sha(source)
            known = x87_manifest.get(key)
            if (known and known["source"] == source_sha and known["tool"] == x87_tool and
                    (not known["rewritten"] or (target.is_file() and sha(target) == known["target"]))):
                rewritten, kept = known["rewritten"], known["kept"]
            else:
                text, rewritten, kept = x87_slots.transform_file(source.read_text(encoding="utf-8"), source.parent.name)
                if rewritten:
                    target.parent.mkdir(parents=True, exist_ok=True)
                    if not target.is_file() or target.read_text(encoding="utf-8") != text:
                        target.write_text(text, encoding="utf-8", newline="\n")
            x87_next[key] = {"source": source_sha, "tool": x87_tool, "rewritten": rewritten, "kept": kept,
                             "target": sha(target) if rewritten else None}
            x87_counts[0] += rewritten
            x87_counts[1] += kept
            # A file with nothing rewritten keeps its own path, so its cached object stays valid.
            if rewritten:
                source = target
        if module == "ut3":
            text, count = native_same_unit_calls(source.read_text(encoding="utf-8"))
            if count:
                target = out.parent / "native_calls" / module / source.name
                target.parent.mkdir(parents=True, exist_ok=True)
                if not target.is_file() or target.read_text(encoding="utf-8") != text:
                    target.write_text(text, encoding="utf-8", newline="\n")
                source = target
                native_calls += count
        if protocol and source != generated / "dispatch.c" and module in protocol_modules:
            original = source.read_bytes()
            text = apply_protocol(original, protocol, module)
            if x87_plan is not None:
                # After the protocol, as in Heroes V: the rewrite spills x87 values around RECOMP_CALL_P.
                text = x87_locals.apply_x87(text, x87_plan)
            if text != original:
                # The copy lives outside the module folder: name the module's header explicitly.
                text = text.replace(b'#include "functions.h"', b'#include "' + module.encode() + b'/functions.h"', 1)
                target = out.parent / "protocol" / module / source.name
                target.parent.mkdir(parents=True, exist_ok=True)
                if not target.is_file() or target.read_bytes() != text:
                    target.write_bytes(text)
                source = target
                protocol_counts[0] += 1
            else:
                protocol_counts[1] += 1
        if coverage:
            text, count = coverage.instrument_generated(source.read_text(encoding="utf-8"), module)
            if count:
                source = coverage.write_copy(text, "generated", module, source.name)
        tasks.append((source, common + ["-w", "-DRECOMP_GENERATED_CODE"]
                      if source == generated / "dispatch.c" else generated_flags))
    if x87_next != x87_manifest:
        x87_dir.mkdir(parents=True, exist_ok=True)
        x87_manifest_path.write_text(json.dumps(x87_next, indent=1, sort_keys=True) + "\n", encoding="utf-8")
    print(f"x87 stack in locals: {x87_counts[0]} functions, {x87_counts[1]} keep the window", flush=True)
    print(f"native calls within their original translation unit: {native_calls}", flush=True)
    if protocol:
        print(f"masked call protocol: {protocol_counts[0]} sources rewritten, {protocol_counts[1]} unchanged; "
              f"{len(protocol.full)} bodies keep the full protocol, {len(protocol.full_slots)} import slots", flush=True)
    host_copies = set()
    host_cflags_overrides = {}
    for source in host_sources:
        flags = common + ["-std=gnu11", "-Wall", "-Wno-unused-function", "-Wno-unused-variable"]
        if source.name in SEPARATED_HOST_SOURCES:
            # Native kernels share the lifted code's reserved guest window and global GPRs: tell the
            # optimiser that guest memory never overlaps them, as for the generated bodies.
            extra_flags = ["-DRECOMP_GENERATED_CODE", "-DRECOMP_GUEST_STATE_SEPARATE",
                           "-mllvm", "-basic-aa-separate-storage", "-include",
                           str(ROOT / "android/runtime/guest_state_prefix.h")]
            flags += extra_flags
            host_cflags_overrides[source.relative_to(ROOT).as_posix()] = extra_flags
        if source.name == "openal.c":
            flags.append("-Ireference/openal-soft/source/include")
        if source.name == "hlsl_vkd3d.c":
            flags.append("-Ireference/vkd3d/include")
        if args.rt_check and source.name.startswith("d3d9_"):
            flags.append("-DVOID_RT_CHECK")
        if coverage:
            text, count = coverage.instrument_host(source.read_text(encoding="utf-8"))
            if count:
                source = coverage.write_copy(text, "host", "host", source.name)
                host_copies.add(source)
        tasks.append((source, flags))
    if coverage:
        tasks.append((ROOT / "tools/function_coverage_runtime.c", common + ["-std=gnu11", "-Wall"]))
    mojo_flags = common + ["-w", "-DMOJOSHADER_FLIP_RENDERTARGET", "-DMOJOSHADER_DEPTH_CLIPPING",
        "-DSUPPORT_PROFILE_HLSL=0", "-DSUPPORT_PROFILE_ARB1=0", "-DSUPPORT_PROFILE_ARB1_NV=0",
        "-DSUPPORT_PROFILE_METAL=0", "-DSUPPORT_PROFILE_SPIRV=0", "-DSUPPORT_PROFILE_GLSPIRV=0",
        "-DSUPPORT_PROFILE_GLSL120=0"]
    for name in MOJO_SOURCES:
        tasks.append((ROOT / "reference/mojoshader" / name, mojo_flags))

    def compile_one(task):
        source, flags = task
        relative = source.relative_to(ROOT)
        obj = out.parent / "obj" / relative.with_suffix(".o")
        obj.parent.mkdir(parents=True, exist_ok=True)
        stamp = obj.with_suffix(".json")
        extra = b""
        if source.name == "openal.c":
            extra = b"".join(header.read_bytes() for header in
                             sorted((ROOT / "reference/openal-soft/source/include").rglob("*.h")))
        if source.name == "hlsl_vkd3d.c":
            extra = b"".join(header.read_bytes() for header in sorted((ROOT / "reference/vkd3d/include").glob("*.h")))
        if source.name == "d3d9_dxvk.c":
            extra = (ROOT / "build/dxvk/guest_methods.inc").read_bytes() + (ROOT / "android/dxvk/query_wait.inc").read_bytes()
        if source.parent == ROOT / "android/host" or source in host_copies:
            # Host .inc files are part of the objects that include them (headers are hashed for all).
            text = source.read_bytes()
            for name in sorted(set(re.findall(rb'#include "([A-Za-z0-9_]+\.inc)"', text))):
                included = ROOT / "android/host" / name.decode()
                if included.is_file():
                    extra += included.read_bytes()
                    if name == b"d3d9_thread.inc":
                        extra += (source.parent / "d3d9_thread_check.inc").read_bytes()
        digest = hashlib.sha256(base_digest + source.read_bytes() + json.dumps(flags).encode() + extra).hexdigest()
        if stamp.exists() and obj.exists():
            cached = json.loads(stamp.read_text())
            if cached.get("input") == digest and cached.get("object") == sha(obj):
                return obj, True
        result = subprocess.run([str(clang), *flags, "-c", str(source), "-o", str(obj)],
                                cwd=ROOT, text=True, capture_output=True)
        if result.returncode:
            raise RuntimeError(f"compile failed: {relative}\n{result.stdout}{result.stderr}")
        if result.stderr:
            print(result.stderr.rstrip(), flush=True)
        stamp.write_text(json.dumps({"input": digest, "object": sha(obj)}))
        return obj, False

    objects, cached = [], 0
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [pool.submit(compile_one, task) for task in tasks]
        for index, future in enumerate(as_completed(futures), 1):
            try:
                obj, reused = future.result()
            except BaseException:
                for pending in futures:
                    pending.cancel()
                assert all(pending.running() or pending.done() for pending in futures), "compile failure left queued tasks"
                raise
            objects.append(obj)
            cached += reused
            if index % 25 == 0 or index == len(tasks):
                print(f"compiled {index}/{len(tasks)} ({cached} cached)", flush=True)
    if any(sha(path) != digest for path, digest in pinned.items()):
        raise ValueError("Generated module report, dispatch or PE image changed during compilation")
    response = out.parent / "link.rsp"
    response.write_text("\n".join('"' + p.as_posix() + '"' for p in sorted(objects)))
    subprocess.run([str(clang), "--target=aarch64-linux-android29", "-Wl,-z,max-page-size=16384",
                    *["-Wl,--wrap=" + name for name in WRAPS],
                    *(["-shared", "-Wl,-z,defs"] if args.shared else []),
                    "@" + str(response), "-o", str(out), "-lm", "-ldl", "-llog", "-landroid",
                    str(ROOT / "build/dxvk/libut3_dxvk.a"),
                    str(ROOT / "build/vkd3d/libvkd3d_shader.a"), "-Wl,--exclude-libs,libvkd3d_shader.a",
                    "-L" + str(runtime["libc++_shared.so"].parent), "-lc++_shared",
                    "-lEGL", "-lGLESv3", "-laaudio", "-lz"], cwd=ROOT, check=True)
    receipt = {"output": str(out), "sha256": sha(out), "compiler": compiler,
               "target": "aarch64-linux-android29", "cflags": common, "generated_cflags": generated_flags,
               "host_cflags_overrides": host_cflags_overrides,
               "call_protocol": ({"summary": str(args.call_protocol.resolve()), "summary_sha256": sha(args.call_protocol.resolve()),
                                  "transform_sha256": sha(ROOT / "tools/protocol_transform.py"),
                                  "rewritten_sources": protocol_counts[0], "full_bodies": sorted(protocol.full),
                                  "warm_functions": (str(args.warm_functions.resolve()) if args.warm_functions else None),
                                  "warm_sha256": (sha(args.warm_functions.resolve()) if args.warm_functions else None),
                                  "warm_count": len(protocol.warm),
                                  "check": args.protocol_check} if protocol else None),
               "x87_locals": ({"plan": str(args.x87_locals.resolve()), "plan_sha256": sha(args.x87_locals.resolve()),
                               "tool_sha256": sha(ROOT / "tools/x87_locals.py"),
                               "bodies": dict(sorted(x87_plan.stats.items()))} if x87_plan is not None else None),
               "sources": len(tasks), "cached": cached,
               "generated": str(generated), "shared": args.shared,
               "native_dependencies": {path.relative_to(ROOT).as_posix(): digest for path, digest in native_pinned.items()},
               "whole_game": "NOT YET VERIFIED"}
    if coverage:
        receipt["function_coverage"] = {"manifest": str(coverage.manifest_path),
            "identity_sha256": coverage.manifest["identity_sha256"], "entries": len(coverage.manifest["entries"]),
            "tool_sha256": sha(ROOT / "tools/function_coverage.py"),
            "runtime_sha256": sha(ROOT / "tools/function_coverage_runtime.c"),
            "header_sha256": sha(ROOT / "tools/function_coverage_runtime.h")}
    out.with_suffix(".json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps(receipt, indent=2))


if __name__ == "__main__":
    main()
