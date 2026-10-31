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

ROOT = Path(__file__).resolve().parents[1]
SDK = Path(os.environ.get("ANDROID_SDK_ROOT", Path.home() / "AppData/Local/Android/Sdk"))
MOJO_SOURCES = ["mojoshader.c", "mojoshader_common.c", "mojoshader_assembler.c",
                "mojoshader_preprocessor.c", "mojoshader_lexer.c",
                "profiles/mojoshader_profile_common.c", "profiles/mojoshader_profile_glsl.c",
                "profiles/mojoshader_profile_d3d.c", "profiles/mojoshader_profile_bytecode.c"]


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def native_dependencies(prepare=False):
    if prepare:
        for tool in ("build_native_audio.py", "build_dxvk.py", "build_turnip.py"):
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
    return runtime, pinned, licenses


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
    args = parser.parse_args()
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
    compiler = subprocess.check_output([str(clang), "--version"], text=True)
    common = ["--target=aarch64-linux-android29", "-O2", "-fno-strict-aliasing", "-fwrapv",
              "-fvisibility=hidden", "-fPIC", "-ffp-contract=off", "-fno-math-errno", "-g0",
              "-DRECOMP_X87_WINDOW", "-Iandroid/runtime", "-Iandroid/host",
              "-I" + str(generated), "-Ireference/mojoshader"]
    if args.diagnostics:
        common += ["-DRECOMP_TRACE", "-DRECOMP_CALL_DIAG"]
    if args.shared:
        common.append("-DUT3_ANDROID_APP")
    headers = sorted((ROOT / "android").rglob("*.h")) + sorted(generated.rglob("*.h"))
    headers += [generated / "modules.inc", ROOT / "reference/dlmalloc/malloc.c"]
    headers += sorted((ROOT / "reference/mojoshader").rglob("*.h"))
    inputs = hashlib.sha256((str(clang) + compiler).encode())
    for path in headers:
        inputs.update(str(path).encode())
        inputs.update(path.read_bytes())
    base_digest = inputs.digest()
    tasks = []
    generated_flags = common + ["-w", "-DRECOMP_GENERATED_CODE", "-DRECOMP_GUEST_STATE_SEPARATE",
                                "-mllvm", "-basic-aa-separate-storage", "-include",
                                str(ROOT / "android/runtime/guest_state_prefix.h")]
    for source in lifted:
        tasks.append((source, common + ["-w", "-DRECOMP_GENERATED_CODE"]
                      if source == generated / "dispatch.c" else generated_flags))
    for source in sorted((ROOT / "android/host").glob("*.c")):
        flags = common + ["-std=gnu11", "-Wall", "-Wno-unused-function", "-Wno-unused-variable"]
        if source.name == "openal.c":
            flags.append("-Ireference/openal-soft/source/include")
        tasks.append((source, flags))
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
        if source.name == "d3d9_dxvk.c":
            extra = (ROOT / "build/dxvk/guest_methods.inc").read_bytes() + (ROOT / "android/dxvk/query_wait.inc").read_bytes()
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
                    "-Wl,--wrap=sub_00F75DB0",
                    "-Wl,--wrap=sub_00486270",
                    "-Wl,--wrap=sub_00C85710",
                    "-Wl,--wrap=sub_00C12290",
                    *(["-shared", "-Wl,-z,defs"] if args.shared else []),
                    "@" + str(response), "-o", str(out), "-lm", "-ldl", "-llog", "-landroid",
                    str(ROOT / "build/dxvk/libut3_dxvk.a"),
                    "-L" + str(runtime["libc++_shared.so"].parent), "-lc++_shared",
                    "-lEGL", "-lGLESv3", "-laaudio", "-lz"], cwd=ROOT, check=True)
    receipt = {"output": str(out), "sha256": sha(out), "compiler": compiler,
               "target": "aarch64-linux-android29", "cflags": common, "generated_cflags": generated_flags,
               "sources": len(tasks), "cached": cached,
               "generated": str(generated), "shared": args.shared,
               "native_dependencies": {path.relative_to(ROOT).as_posix(): digest for path, digest in native_pinned.items()},
               "whole_game": "NOT YET VERIFIED"}
    out.with_suffix(".json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps(receipt, indent=2))


if __name__ == "__main__":
    main()
