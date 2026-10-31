"""Build the cached, hash-pinned Adrenotools runtime. Never downloads inputs."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/turnip-runtime"
LIBRARIES = ("libadrenotools.so", "libhook_impl.so", "libmain_hook.so",
             "libfile_redirect_hook.so", "libgsl_alloc_hook.so", "libut3_turnip.so")


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    pins = json.loads((ROOT / "dependencies-turnip.json").read_text())
    for name, expected in pins["source_files"].items():
        if sha(ROOT / name) != expected:
            raise ValueError("Turnip helper source hash mismatch: " + name)
    driver = ROOT / "reference/turnip-android/vulkan.ad07xx.so"
    if sha(driver) != pins["driver_sha256"]:
        raise ValueError("Cached Turnip driver hash mismatch")
    sdk = Path(os.environ.get("ANDROID_SDK_ROOT", Path.home() / "AppData/Local/Android/Sdk"))
    ndk = sdk / "ndk/27.2.12479018"
    cmake = sdk / "cmake/3.22.1/bin/cmake.exe"
    native, runtime = OUT / "native", OUT / "runtime"
    # No build machine paths in the libraries' __FILE__ strings.
    prefix_maps = f"-ffile-prefix-map={ROOT.as_posix()}=. -ffile-prefix-map={sdk.as_posix()}=sdk"
    subprocess.run([str(x) for x in [cmake, "-S", ROOT / "reference/adrenotools", "-B", native,
        "-DCMAKE_C_FLAGS=" + prefix_maps, "-DCMAKE_CXX_FLAGS=" + prefix_maps,
        "-G", "Ninja", "-DCMAKE_MAKE_PROGRAM=" + str(cmake.parent / "ninja.exe"),
        "-DCMAKE_TOOLCHAIN_FILE=" + str(ndk / "build/cmake/android.toolchain.cmake"),
        "-DANDROID_ABI=arm64-v8a", "-DANDROID_PLATFORM=android-30", "-DANDROID_STL=c++_shared",
        "-DBUILD_SHARED_LIBS=ON", "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_SHARED_LINKER_FLAGS=-Wl,-z,max-page-size=16384"]], check=True)
    subprocess.run([str(cmake), "--build", str(native), "--parallel", "4"], check=True)
    runtime.mkdir(parents=True, exist_ok=True)
    sources = {"libadrenotools.so": native / "libadrenotools.so", "libut3_turnip.so": driver}
    sources.update({name: native / "src/hook" / name for name in LIBRARIES[1:-1]})
    for name, source in sources.items():
        data = source.read_bytes()
        if data[:5] != b"\x7fELF\x02" or struct.unpack_from("<HH", data, 16) != (3, 183):
            raise ValueError("Custom GPU library is not an AArch64 ELF DSO: " + name)
        for index in range(struct.unpack_from("<H", data, 56)[0]):
            offset = struct.unpack_from("<Q", data, 32)[0] + index * struct.unpack_from("<H", data, 54)[0]
            if struct.unpack_from("<I", data, offset)[0] == 1 and struct.unpack_from("<Q", data, offset + 48)[0] < 16384:
                raise ValueError("Custom GPU PT_LOAD alignment is below 16KiB: " + name)
        shutil.copyfile(source, runtime / name)
    licenses = runtime / "licenses"
    licenses.mkdir(exist_ok=True)
    for name, source in {
        "adrenotools.BSD-2-Clause.txt": ROOT / "reference/adrenotools/LICENSE",
        "linkernsbypass.BSD-2-Clause.txt": ROOT / "reference/adrenotools/lib/linkernsbypass/LICENSE",
        "Turnip-LICENSE-NOTE.txt": ROOT / "reference/turnip-android/LICENSE-NOTE.txt",
    }.items():
        shutil.copyfile(source, licenses / name)
    receipt = {"source": pins["source"], "adrenotools_revision": pins["adrenotools_revision"],
        "linkernsbypass_revision": pins["linkernsbypass_revision"], "build_tool_sha256": sha(Path(__file__)),
        "input_pins_sha256": sha(ROOT / "dependencies-turnip.json"), "driver_sha256": pins["driver_sha256"],
        "outputs": [{"path": (runtime / name).relative_to(ROOT).as_posix(), "bytes": (runtime / name).stat().st_size,
                     "sha256": sha(runtime / name)} for name in LIBRARIES],
        "licenses": [{"path": p.relative_to(ROOT).as_posix(), "sha256": sha(p)} for p in sorted(licenses.iterdir())],
        "gameplay_verified": False, "system_driver_changed": False,
        "driver_license_scope": pins["driver_license_scope"]}
    (OUT / "build-receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps(receipt, indent=2))


if __name__ == "__main__":
    main()
