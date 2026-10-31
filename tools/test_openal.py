"""Build an isolated production guest OpenAL fixture; --run uses a private adb directory."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/openal-test"
MOJO = ["mojoshader.c", "mojoshader_common.c", "mojoshader_assembler.c",
        "mojoshader_preprocessor.c", "mojoshader_lexer.c", "profiles/mojoshader_profile_common.c",
        "profiles/mojoshader_profile_glsl.c", "profiles/mojoshader_profile_d3d.c",
        "profiles/mojoshader_profile_bytecode.c"]


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", action="store_true")
    parser.add_argument("--serial", default=os.environ.get("UT3_TEST_ANDROID"), required=not os.environ.get("UT3_TEST_ANDROID"))
    args = parser.parse_args()
    sdk = Path(os.environ.get("ANDROID_SDK_ROOT", Path.home() / "AppData/Local/Android/Sdk"))
    ndk = sdk / "ndk/27.2.12479018"
    clang = ndk / "toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe"
    if not (ROOT / "android/host/openal.c").is_file():
        parser.error("Production OpenAL bridge is not ready")
    OUT.mkdir(parents=True, exist_ok=True)
    rows = json.loads((ROOT / "research/openal-dynamic-exports.json").read_text())["apis"]
    names = [row["name"] for row in rows if row["group"] in ("AL", "ALC_EXT_EFX")]
    assert len(names) == 118 and len(set(names)) == 118
    (OUT / "test_openal_apis.h").write_text("static const char *openal_api_names[]={\n" +
        "\n".join(json.dumps(name) + "," for name in names) + "\n};\n")
    (OUT / "dispatch.c").write_text('#include "recomp_types.h"\nstatic void entry(void){g_eax=0;}\n'
        'const recomp_dispatch_entry_t recomp_dispatch_table[]={{0x0128959au,entry}};\n'
        'const uint32_t recomp_dispatch_count=1;\n')
    common = ["--target=aarch64-linux-android29", "-O1", "-std=gnu11", "-fno-strict-aliasing",
              "-fwrapv", "-ffp-contract=off", "-DRECOMP_X87_WINDOW", "-DRECOMP_TRACE", "-DRECOMP_CALL_DIAG",
              "-Iandroid/runtime", "-Iandroid/host", "-Igenerated", "-Ireference/mojoshader",
              "-Ireference/openal-soft/source/include", "-I" + str(OUT)]
    host = [p for p in sorted((ROOT / "android/host").glob("*.c"))
            if p.name not in {"main.c", "android_main.c", "openal.c", "pe.c"}]
    sources = [ROOT / "tests/test_openal.c", OUT / "dispatch.c", *host,
               *(ROOT / "reference/mojoshader" / name for name in MOJO)]
    pins = {str(p.relative_to(ROOT)): sha(p) for p in [*sources, ROOT / "android/host/openal.c", ROOT / "android/host/pe.c"]}

    def compile_one(source):
        obj = OUT / "obj" / source.relative_to(ROOT).with_suffix(".o")
        obj.parent.mkdir(parents=True, exist_ok=True)
        flags = common
        if source.is_relative_to(ROOT / "reference/mojoshader"):
            flags = common + ["-w", "-DMOJOSHADER_FLIP_RENDERTARGET", "-DMOJOSHADER_DEPTH_CLIPPING",
                "-DSUPPORT_PROFILE_HLSL=0", "-DSUPPORT_PROFILE_ARB1=0", "-DSUPPORT_PROFILE_ARB1_NV=0",
                "-DSUPPORT_PROFILE_METAL=0", "-DSUPPORT_PROFILE_SPIRV=0", "-DSUPPORT_PROFILE_GLSPIRV=0",
                "-DSUPPORT_PROFILE_GLSL120=0"]
        result = subprocess.run([str(clang), *flags, "-c", str(source), "-o", str(obj)], cwd=ROOT,
                                capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(str(source) + "\n" + result.stdout + result.stderr)
        return obj

    with ThreadPoolExecutor(max_workers=4) as pool:
        objects = list(pool.map(compile_one, sources))
    if any(sha(ROOT / name) != digest for name, digest in pins.items()):
        raise ValueError("Production source changed during isolated fixture build")
    rsp = OUT / "link.rsp"
    rsp.write_text("\n".join('"' + p.as_posix() + '"' for p in objects))
    binary = OUT / "test-openal"
    subprocess.run([str(clang), "--target=aarch64-linux-android29", "-Wl,-z,max-page-size=16384",
                    "@" + str(rsp), "-o", str(binary), "-lm", "-ldl", "-llog", "-landroid",
                    str(ROOT / "build/dxvk/libut3_dxvk.a"),
                    "-L" + str(ROOT / "build/native-audio/runtime"), "-lc++_shared",
                    "-lEGL", "-lGLESv3", "-laaudio", "-lz"], cwd=ROOT, check=True)
    native = json.loads((ROOT / "build/native-audio/build-receipt.json").read_text())
    runtime = []
    for name, expected in native["runtime"].items():
        path = ROOT / "build/native-audio/runtime" / name
        if sha(path) != expected:
            raise ValueError("Native fixture runtime hash changed: " + name)
        runtime.append(path)
    report = {"binary": str(binary), "binary_sha256": sha(binary), "sources": pins,
              "native_runtime": native["runtime"], "device_executed": False}
    if args.run:
        adb = [str(sdk / "platform-tools/adb.exe"), "-s", args.serial]
        remote = "/data/local/tmp/ut3-audio-bridge"
        subprocess.run([*adb, "shell", "mkdir", "-p", remote], check=True)
        for path in [binary, *runtime]:
            subprocess.run([*adb, "push", str(path), remote + "/" + path.name], check=True)
        subprocess.run([*adb, "shell", "chmod", "755", remote + "/test-openal"], check=True)
        executed = subprocess.run([*adb, "shell", "cd " + remote + " && LD_LIBRARY_PATH=" + remote +
            " ALSOFT_DRIVERS=oboe ALSOFT_LOGLEVEL=3 ./test-openal"], capture_output=True, text=True,
            encoding="utf-8", errors="replace", timeout=20)
        log = executed.stdout + executed.stderr
        (ROOT / "research/device-openal-bridge.log").write_text(log)
        report.update(device_executed=True, serial=args.serial, returncode=executed.returncode,
                      remote_directory=remote, process="test-openal", log="research/device-openal-bridge.log")
        for line in executed.stdout.splitlines():
            if line.startswith("OPENAL_TEST_JSON "):
                report["metrics"] = json.loads(line[len("OPENAL_TEST_JSON "):])
            elif line.startswith("OPENAL_METRICS_JSON "):
                report["physical_metrics"] = json.loads(line[len("OPENAL_METRICS_JSON "):])
            elif line.startswith("OPENAL_ERROR_EDGES_JSON "):
                report["error_edges"] = json.loads(line[len("OPENAL_ERROR_EDGES_JSON "):])
        (ROOT / "research/openal-bridge-test.json").write_text(json.dumps(report, indent=2) + "\n")
        print(log)
        if executed.returncode or not report.get("metrics", {}).get("success"):
            raise SystemExit("Guest OpenAL/AAudio fixture failed; production sources unchanged")
    (OUT / "build-receipt.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({k: v for k, v in report.items() if k != "sources"}, indent=2))


if __name__ == "__main__":
    main()
