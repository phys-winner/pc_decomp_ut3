"""DXVK Native D3D9 with the UT3 Android WSI, and the port's x86 ABI adapter (the bridge).

By default the pinned prebuilt library reference/dxvk-runtime/libdxvk_d3d9.so (what --rebuild makes from
upstream REVISION with the overlays below and android/dxvk/wsi_android.cpp) is staged and only the bridge
is compiled, with the NDK alone. --rebuild builds DXVK again from a checkout of that revision in
reference/dxvk, with meson and glslang from --host-tools (python/ with mesonbuild, glslang/bin)."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "reference/dxvk"
WORK = ROOT / "build/dxvk-source"
OUT = ROOT / "build/dxvk"
PREBUILT = ROOT / "reference/dxvk-runtime/libdxvk_d3d9.so"
REVISION = "0cf05780abd7250c2cd713b7749cf32180157cf5"
SDK = Path(os.environ.get("ANDROID_SDK_ROOT", Path.home() / "AppData/Local/Android/Sdk"))
NDK = SDK / "ndk/27.2.12479018"
LLVM = NDK / "toolchains/llvm/prebuilt/windows-x86_64/bin"
OVERLAY_TEXT = {}

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def write(path, value):
    data = value.encode() if isinstance(value, str) else value
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_bytes() != data:
        path.write_bytes(data)

def replace(path, before, after):
    value = OVERLAY_TEXT.get(path)
    if value is None:
        value = (SOURCE / path.relative_to(WORK)).read_text(encoding="utf-8")
    if value.count(before) != 1:
        raise ValueError(f"DXVK overlay context mismatch: {path}: {before[:80]}")
    OVERLAY_TEXT[path] = value.replace(before, after)

def run(args, **kwargs):
    print(subprocess.list2cmdline([str(x) for x in args]), flush=True)
    subprocess.run([str(x) for x in args], check=True, **kwargs)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--jobs", type=int, default=6)
    parser.add_argument("--rebuild", action="store_true", help="build DXVK with meson instead of staging the pinned library")
    parser.add_argument("--host-tools", type=Path, help="for --rebuild: a folder with python/ (mesonbuild) and glslang/bin")
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    pins = json.loads((ROOT / "dependencies-dxvk.json").read_text())
    if not args.rebuild:
        prebuilt = pins["prebuilt"]
        if sha(PREBUILT) != prebuilt["sha256"]:
            raise ValueError(f"Prebuilt DXVK hash mismatch: {PREBUILT}")
        if sha(ROOT / "android/dxvk/wsi_android.cpp") != prebuilt["wsi_sha256"]:
            raise ValueError("android/dxvk/wsi_android.cpp differs from the prebuilt DXVK's: build it with --rebuild")
        write(OUT / "libdxvk_d3d9.so", PREBUILT.read_bytes())
    else:
        if args.host_tools is None:
            parser.error("--rebuild needs --host-tools")
        pristine = {}
        for name, expected in pins["source_files"].items():
            path = SOURCE / name
            if sha(path) != expected:
                raise ValueError("DXVK source hash mismatch: " + name)
            pristine[WORK / name] = path.read_bytes()
        replace(WORK / "meson.build", "  if (not lib_sdl2.found() and not lib_glfw.found())",
                "  if platform == 'android'\n    compiler_args += ['-DDXVK_WSI_ANDROID']\n    link_args += ['-landroid', '-ldl', '-Wl,-z,max-page-size=16384']\n  endif\n  if platform != 'android' and (not lib_sdl2.found() and not lib_glfw.found())")
        replace(WORK / "src/wsi/meson.build", "  'wsi_platform.cpp',", "  'wsi_platform.cpp',\n  'wsi_android.cpp',")
        replace(WORK / "src/wsi/meson.build", "if platform != 'windows'", "if platform != 'windows' and platform != 'android'")
        write(WORK / "src/wsi/wsi_android.cpp", (ROOT / "android/dxvk/wsi_android.cpp").read_bytes())
        replace(WORK / "src/wsi/wsi_platform.h", "  void init();", "#if defined(DXVK_WSI_ANDROID)\n  extern WsiBootstrap AndroidWSI;\n#endif\n\n  void init();")
        replace(WORK / "src/wsi/wsi_platform.cpp", "  static const WsiBootstrap *wsiBootstrap[] = {", "  static const WsiBootstrap *wsiBootstrap[] = {\n#if defined(DXVK_WSI_ANDROID)\n    &AndroidWSI,\n#endif")
        replace(WORK / "src/d3d9/d3d9.sym", "    Direct3DCreate9;", "    Direct3DCreate9;\n    ut3_dxvk_set_callbacks_v2;\n    ut3_dxvk_set_vulkan_loader;")
        replace(WORK / "src/vulkan/vulkan_loader.cpp", "namespace dxvk::vk {",
                "#if defined(DXVK_WSI_ANDROID)\nextern \"C\" PFN_vkGetInstanceProcAddr ut3_dxvk_native_vulkan_loader();\n#endif\n\nnamespace dxvk::vk {")
        replace(WORK / "src/vulkan/vulkan_loader.cpp", "  LibraryLoader::LibraryLoader() {\n",
                "  LibraryLoader::LibraryLoader() {\n#if defined(DXVK_WSI_ANDROID)\n"
                "    if (auto loader = ut3_dxvk_native_vulkan_loader()) {\n"
                "      m_getInstanceProcAddr = loader;\n      return;\n    }\n#endif\n")
        # textureCompressionBC guarantees the entire BC family, including BC6/7.
        # Android GPUs may support D3D9's BC1-5 individually without that aggregate
        # feature. Keep native CheckDeviceFormat's Vulkan format checks unchanged.
        replace(WORK / "src/d3d9/d3d9_device.cpp",
                "    enabled.core.features.textureCompressionBC = VK_TRUE;",
                "#if defined(DXVK_WSI_ANDROID)\n"
                "    enabled.core.features.textureCompressionBC = supported.core.features.textureCompressionBC;\n"
                "#else\n    enabled.core.features.textureCompressionBC = VK_TRUE;\n#endif")
        # PowerVR D-Series 1.693.3716 reports zero samples from the 16384-entry
        # occlusion pool despite a readback-proven visible triangle. A 64-entry
        # pool returns the exact 6328 samples; the allocator grows more pools.
        replace(WORK / "src/dxvk/dxvk_gpu_query.cpp",
                "  DxvkGpuQueryPool::DxvkGpuQueryPool(DxvkDevice* device)\n  : m_occlusion(device, VK_QUERY_TYPE_OCCLUSION,                     16384),",
                "  static uint32_t occlusionQueryPoolSize(DxvkDevice* device) {\n"
                "#if defined(DXVK_WSI_ANDROID)\n"
                "    // PowerVR's large occlusion pools lose real sample counts.\n"
                "    // Keep precise queries and grow additional small pools.\n"
                "    if (device->adapter()->matchesDriver(VK_DRIVER_ID_IMAGINATION_PROPRIETARY))\n"
                "      return 64;\n"
                "#endif\n    return 16384;\n  }\n\n"
                "  DxvkGpuQueryPool::DxvkGpuQueryPool(DxvkDevice* device)\n"
                "  : m_occlusion(device, VK_QUERY_TYPE_OCCLUSION,                     occlusionQueryPoolSize(device)),")
        replace(WORK / "src/util/thread.h", "namespace dxvk {", "#if defined(__ANDROID__)\nextern \"C\" void ut3_dxvk_native_worker_enter();\n#endif\n\nnamespace dxvk {")
        replace(WORK / "src/util/thread.h", "  class thread : public std::thread {\n  public:\n    using std::thread::thread;", """  class thread : public std::thread {
  public:
#if defined(__ANDROID__)
    thread() = default;
    thread(thread&&) noexcept = default;
    thread& operator = (thread&&) noexcept = default;
    template<typename Function>
    explicit thread(Function&& function)
    : std::thread([proc = std::forward<Function>(function)]() mutable {
        ut3_dxvk_native_worker_enter();
        proc();
      }) { }
#else
    using std::thread::thread;
#endif""")
        replace(WORK / "src/dxvk/dxvk_presenter.cpp", "namespace dxvk {", "#if defined(__ANDROID__)\nextern \"C\" void ut3_dxvk_native_present_notify(const void*, uint64_t, int32_t);\n#endif\nnamespace dxvk {")
        replace(WORK / "src/dxvk/dxvk_presenter.cpp", "  Presenter::~Presenter() {\n", "  Presenter::~Presenter() {\n#if defined(__ANDROID__)\n    ut3_dxvk_native_present_notify(this, 0, VK_NOT_READY);\n#endif\n")
        replace(WORK / "src/dxvk/dxvk_presenter.cpp", "    // Try to acquire next image already", "#if defined(__ANDROID__)\n    if (frameId) ut3_dxvk_native_present_notify(this, frameId, status);\n#endif\n\n    // Try to acquire next image already")
        replace(WORK / "src/d3d9/d3d9_swapchain.cpp", "    if (status != VK_SUCCESS)\n      RecreateSwapChain();\n  }\n\n  void D3D9SwapChainEx::RecreateSwapChain()", "#if defined(DXVK_WSI_ANDROID)\n    if (status != VK_SUCCESS && status != VK_SUBOPTIMAL_KHR)\n#else\n    if (status != VK_SUCCESS)\n#endif\n      RecreateSwapChain();\n  }\n\n  void D3D9SwapChainEx::RecreateSwapChain()")
        # Compare only final composed bytes; restoring a pristine shared header
        # before patching would unnecessarily invalidate every native DXVK TU.
        for path, data in pristine.items():
            write(path, OVERLAY_TEXT.get(path, data))
        cross = OUT / "aarch64-android.ini"
        write(cross, f"""[binaries]
c = ['{LLVM.as_posix()}/clang.exe', '--target=aarch64-linux-android29']
cpp = ['{LLVM.as_posix()}/clang++.exe', '--target=aarch64-linux-android29']
ar = '{LLVM.as_posix()}/llvm-ar.exe'
strip = '{LLVM.as_posix()}/llvm-strip.exe'
[host_machine]
system = 'android'
cpu_family = 'aarch64'
cpu = 'armv8-a'
endian = 'little'
[built-in options]
c_args = ['-fPIC']
cpp_args = ['-fPIC']
""")
        # The host tools are used in place; no project or system installation is changed.
        host_tools = args.host_tools.resolve()
        env = os.environ.copy()
        env["PYTHONPATH"] = str(host_tools / "python")
        ninja = SDK / "cmake/3.22.1/bin/ninja.exe"
        env["PATH"] = os.pathsep.join([str(host_tools / "glslang/bin"), str(ninja.parent), "C:/Program Files/Git/usr/bin", env["PATH"]])
        if not (OUT / "build.ninja").exists():
            run([sys.executable, "-m", "mesonbuild.mesonmain", "setup", OUT, WORK, "--cross-file", cross, "--buildtype", "release",
                 "-Denable_dxgi=false", "-Denable_d3d11=false", "-Denable_d3d10=false", "-Denable_d3d9=true", "-Denable_d3d8=false"], env=env)
        run([ninja, "-C", OUT, "-j", args.jobs], env=env)
        write(OUT / "libdxvk_d3d9.so", (OUT / "src/d3d9/libdxvk_d3d9.so").read_bytes())
        if sha(OUT / "libdxvk_d3d9.so") != pins["prebuilt"]["sha256"]:
            print(f"The rebuilt library differs from {PREBUILT.relative_to(ROOT)}: copy it there and update its pin to keep it", flush=True)
    # Bridge compilation is independent of the large static game compilation.
    bridge = ROOT / "android/dxvk/bridge.cpp"
    if bridge.exists():
        import generate_dxvk_bridge
        generate_dxvk_bridge.main()
        includes = [SOURCE / "include/native/windows", SOURCE / "include/native/directx", SOURCE / "include/vulkan/include", OUT]
        command = [LLVM / "clang++.exe", "--target=aarch64-linux-android29", "-std=c++17", "-O2", "-fPIC", "-Wall", "-Wextra", "-Wno-extern-c-compat", "-Wno-missing-field-initializers"]
        for path in includes:
            command.extend(["-I", path])
        run([*command, "-c", bridge, "-o", OUT / "bridge.o"])
        run([LLVM / "llvm-ar.exe", "rcs", OUT / "libut3_dxvk.a", OUT / "bridge.o"])
    write(OUT / "libc++_shared.so", (NDK / "toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so").read_bytes())
    outputs = [p for p in [OUT / "libdxvk_d3d9.so", OUT / "libut3_dxvk.a", OUT / "libc++_shared.so"] if p.exists()]
    receipt = {"upstream": "https://github.com/doitsujin/dxvk", "revision": REVISION, "target": "aarch64-linux-android29", "ndk": "27.2.12479018",
               "build_tool_sha256": sha(Path(__file__)), "compiler_sha256": sha(LLVM / "clang++.exe"), "wsi_sha256": sha(ROOT / "android/dxvk/wsi_android.cpp"),
               "worker_entry_callback_abi": 2, "dxvk_staged_prebuilt": not args.rebuild,
               "outputs": [{"path": str(p.relative_to(ROOT)), "bytes": p.stat().st_size, "sha256": sha(p)} for p in outputs], "game_rendered": False}
    write(OUT / "build-receipt.json", json.dumps(receipt, indent=2) + "\n")

if __name__ == "__main__":
    main()
