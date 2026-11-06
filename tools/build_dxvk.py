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

def replace_all(path, before, after, count):
    value = OVERLAY_TEXT.get(path)
    if value is None:
        value = (SOURCE / path.relative_to(WORK)).read_text(encoding="utf-8")
    if value.count(before) != count:
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
        for name, expected in prebuilt.get("overlay_sha256", {}).items():
            if sha(ROOT / name) != expected:
                raise ValueError(f"{name} differs from the prebuilt DXVK's: build it with --rebuild")
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
        # Occlusion query pools: PowerVR's proprietary driver loses sample counts in a 16384-entry pool
        # and starts a new render (store and reload of every attachment) whenever a query uses another
        # pool than the query before it. 4096-entry pools there, and handles come from one pool until it
        # is used up instead of upstream's LIFO list over all pools: android/dxvk/query_pools.inc.
        write(WORK / "src/dxvk/dxvk_query_pools.inc", (ROOT / "android/dxvk/query_pools.inc").read_bytes())
        replace(WORK / "src/dxvk/dxvk_gpu_query.h",
                "    dxvk::mutex                     m_mutex;\n    std::vector<DxvkGpuQueryHandle> m_handles;\n    std::vector<VkQueryPool>        m_pools;\n",
                "    dxvk::mutex                     m_mutex;\n"
                "    // Free handles of each pool (parallel to m_pools), see dxvk_query_pools.inc\n"
                "    std::vector<std::vector<DxvkGpuQueryHandle>> m_handles;\n"
                "    std::vector<VkQueryPool>        m_pools;\n"
                "    size_t                          m_currentPool = 0;\n")
        replace(WORK / "src/dxvk/dxvk_gpu_query.cpp",
                "  DxvkGpuQueryHandle DxvkGpuQueryAllocator::allocQuery() {\n    std::lock_guard<dxvk::mutex> lock(m_mutex);\n\n"
                "    if (m_handles.size() == 0)\n      this->createQueryPool();\n\n    if (m_handles.size() == 0)\n      return DxvkGpuQueryHandle();\n    \n"
                "    DxvkGpuQueryHandle result = m_handles.back();\n    m_handles.pop_back();\n    return result;\n  }\n\n\n"
                "  void DxvkGpuQueryAllocator::freeQuery(DxvkGpuQueryHandle handle) {\n    std::lock_guard<dxvk::mutex> lock(m_mutex);\n"
                "    m_handles.push_back(handle);\n  }\n",
                "#include \"dxvk_query_pools.inc\"\n")
        replace(WORK / "src/dxvk/dxvk_gpu_query.cpp",
                "    m_pools.push_back(queryPool);\n\n    for (uint32_t i = 0; i < m_queryPoolSize; i++)\n      m_handles.push_back({ this, queryPool, i });\n",
                "    m_pools.push_back(queryPool);\n    m_handles.emplace_back();\n    m_handles.back().reserve(m_queryPoolSize);\n\n"
                "    for (uint32_t i = 0; i < m_queryPoolSize; i++)\n      m_handles.back().push_back({ this, queryPool, i });\n")
        replace(WORK / "src/dxvk/dxvk_gpu_query.cpp",
                "  : m_occlusion(device, VK_QUERY_TYPE_OCCLUSION,                     16384),",
                "  : m_occlusion(device, VK_QUERY_TYPE_OCCLUSION,                     occlusionQueryPoolSize(device)),")
        # Qualcomm's proprietary Adreno driver has no *_SCALED vertex buffer formats (D3D9 UBYTE4,
        # SHORT2/4, UDEC3); DXVK then drops those draws (UE3 terrain). Fetch the mandatory UINT/SINT
        # formats instead and convert in the vertex shader: android/dxvk/scaled_vertex_formats.inc.
        write(WORK / "src/dxvk/dxvk_scaled_vertex.h", (ROOT / "android/dxvk/scaled_vertex_formats.inc").read_bytes())
        write(WORK / "src/dxvk/dxvk_scaled_vertex_spirv.inc", (ROOT / "android/dxvk/scaled_vertex_spirv.inc").read_bytes())
        replace(WORK / "src/dxvk/dxvk_shader.h", "    uint32_t  undefinedInputs = 0;\n",
                "    uint32_t  undefinedInputs = 0;\n    uint32_t  vsUintInputs    = 0;\n    uint32_t  vsSintInputs    = 0;\n")
        replace(WORK / "src/dxvk/dxvk_shader.h", "    static void eliminateInput(\n",
                "    static void convertIntegerInput(\n            SpirvCodeBuffer&          code,\n"
                "            uint32_t                  location,\n            bool                      isSigned);\n\n"
                "    static void eliminateInput(\n")
        replace(WORK / "src/dxvk/dxvk_shader.cpp", "           && undefinedInputs == other.undefinedInputs;",
                "           && undefinedInputs == other.undefinedInputs\n"
                "           && vsUintInputs    == other.vsUintInputs\n"
                "           && vsSintInputs    == other.vsSintInputs;")
        replace(WORK / "src/dxvk/dxvk_shader.cpp", "    hash.add(undefinedInputs);\n",
                "    hash.add(undefinedInputs);\n    hash.add(vsUintInputs);\n    hash.add(vsSintInputs);\n")
        replace(WORK / "src/dxvk/dxvk_shader.cpp", "      eliminateInput(spirvCode, u);\n",
                "      eliminateInput(spirvCode, u);\n\n"
                "    // Integer fetch of SCALED vertex formats, see dxvk_scaled_vertex.h\n"
                "    for (uint32_t u : bit::BitMask(state.vsUintInputs))\n"
                "      convertIntegerInput(spirvCode, u, false);\n"
                "    for (uint32_t u : bit::BitMask(state.vsSintInputs))\n"
                "      convertIntegerInput(spirvCode, u, true);\n"
                "    if (bit::popcnt(state.vsUintInputs) > 1 || bit::popcnt(state.vsSintInputs) > 1) {\n"
                "      static std::atomic<bool> reported = { false };\n"
                "      if (!reported.exchange(true))\n"
                "        Logger::info(str::format(\"DXVK: SCALED vertex fallback: a shader converts \",\n"
                "          bit::popcnt(state.vsUintInputs), \" unsigned and \", bit::popcnt(state.vsSintInputs), \" signed inputs\"));\n"
                "    }\n")
        replace(WORK / "src/dxvk/dxvk_shader.cpp", "  void DxvkShader::eliminateInput(SpirvCodeBuffer& code, uint32_t location) {\n",
                "#include \"dxvk_scaled_vertex_spirv.inc\"\n\n  void DxvkShader::eliminateInput(SpirvCodeBuffer& code, uint32_t location) {\n")
        replace(WORK / "src/dxvk/dxvk_graphics.h",
                "    DxvkGraphicsPipelineShaderState(\n      const DxvkGraphicsPipelineShaders&    shaders,\n",
                "    DxvkGraphicsPipelineShaderState(\n      const DxvkDevice*                     device,\n      const DxvkGraphicsPipelineShaders&    shaders,\n")
        replace(WORK / "src/dxvk/dxvk_graphics.h",
                "    DxvkShaderModuleCreateInfo getCreateInfo(\n      const DxvkGraphicsPipelineShaders&    shaders,\n",
                "    DxvkShaderModuleCreateInfo getCreateInfo(\n      const DxvkDevice*                     device,\n      const DxvkGraphicsPipelineShaders&    shaders,\n")
        replace(WORK / "src/dxvk/dxvk_graphics.h", "    : shState(shaders, state),", "    : shState(device, shaders, state),")
        replace(WORK / "src/dxvk/dxvk_graphics.cpp", "#include \"dxvk_pipemanager.h\"\n",
                "#include \"dxvk_pipemanager.h\"\n#include \"dxvk_scaled_vertex.h\"\n")
        replace(WORK / "src/dxvk/dxvk_graphics.cpp", "        attr.format = state.ilAttributes[i].format();\n",
                "        uint32_t conversion = 0;\n"
                "        attr.format = scaledVertexFetchFormat(device, state.ilAttributes[i].format(), &conversion);\n")
        replace(WORK / "src/dxvk/dxvk_graphics.cpp",
                "  DxvkGraphicsPipelineShaderState::DxvkGraphicsPipelineShaderState(\n    const DxvkGraphicsPipelineShaders&    shaders,\n    const DxvkGraphicsPipelineStateInfo&  state)\n"
                "  : vsInfo  (getCreateInfo(shaders, shaders.vs, state)),\n    tcsInfo (getCreateInfo(shaders, shaders.tcs, state)),\n"
                "    tesInfo (getCreateInfo(shaders, shaders.tes, state)),\n    gsInfo  (getCreateInfo(shaders, shaders.gs, state)),\n"
                "    fsInfo  (getCreateInfo(shaders, shaders.fs, state)) {",
                "  DxvkGraphicsPipelineShaderState::DxvkGraphicsPipelineShaderState(\n    const DxvkDevice*                     device,\n"
                "    const DxvkGraphicsPipelineShaders&    shaders,\n    const DxvkGraphicsPipelineStateInfo&  state)\n"
                "  : vsInfo  (getCreateInfo(device, shaders, shaders.vs, state)),\n    tcsInfo (getCreateInfo(device, shaders, shaders.tcs, state)),\n"
                "    tesInfo (getCreateInfo(device, shaders, shaders.tes, state)),\n    gsInfo  (getCreateInfo(device, shaders, shaders.gs, state)),\n"
                "    fsInfo  (getCreateInfo(device, shaders, shaders.fs, state)) {")
        replace(WORK / "src/dxvk/dxvk_graphics.cpp",
                "  DxvkShaderModuleCreateInfo DxvkGraphicsPipelineShaderState::getCreateInfo(\n    const DxvkGraphicsPipelineShaders&    shaders,\n",
                "  DxvkShaderModuleCreateInfo DxvkGraphicsPipelineShaderState::getCreateInfo(\n    const DxvkDevice*                     device,\n    const DxvkGraphicsPipelineShaders&    shaders,\n")
        replace(WORK / "src/dxvk/dxvk_graphics.cpp",
                "      for (uint32_t i = 0; i < state.il.attributeCount(); i++)\n        providedInputs |= 1u << state.ilAttributes[i].location();\n",
                "      for (uint32_t i = 0; i < state.il.attributeCount(); i++) {\n"
                "        uint32_t location = state.ilAttributes[i].location();\n"
                "        uint32_t conversion = 0;\n"
                "        providedInputs |= 1u << location;\n"
                "        if (consumedInputs & (1u << location)) {\n"
                "          scaledVertexFetchFormat(device, state.ilAttributes[i].format(), &conversion);\n"
                "          if (conversion == 1) info.vsUintInputs |= 1u << location;\n"
                "          if (conversion == 2) info.vsSintInputs |= 1u << location;\n"
                "        }\n"
                "      }\n")
        # Base (GPL fast-link) pipelines use the unpatched vertex shader library.
        replace(WORK / "src/dxvk/dxvk_graphics.cpp",
                "    if (m_shaders.tcs != nullptr) {\n      // If tessellation shaders are present, the input patch\n",
                "    for (uint32_t i = 0; i < state.il.attributeCount(); i++) {\n"
                "      uint32_t conversion = 0;\n"
                "      if (m_shaders.vs->info().inputMask & (1u << state.ilAttributes[i].location()))\n"
                "        scaledVertexFetchFormat(m_device, state.ilAttributes[i].format(), &conversion);\n"
                "      if (conversion)\n        return false;\n    }\n\n"
                "    if (m_shaders.tcs != nullptr) {\n      // If tessellation shaders are present, the input patch\n")
        replace(WORK / "src/dxvk/dxvk_graphics.cpp",
                "      DxvkFormatFeatures formatInfo = m_device->getFormatFeatures(attribute.format());\n",
                "      uint32_t conversion = 0;\n"
                "      DxvkFormatFeatures formatInfo = m_device->getFormatFeatures(\n"
                "        scaledVertexFetchFormat(m_device, attribute.format(), &conversion));\n")
        # Persistent VkPipelineCache shared by all pipeline creation: android/dxvk/pipeline_cache.inc.
        write(WORK / "src/dxvk/dxvk_pipecache_file.h", (ROOT / "android/dxvk/pipeline_cache.inc").read_bytes())
        replace(WORK / "src/dxvk/dxvk_device.h", "#include \"dxvk_options.h\"\n",
                "#include \"dxvk_options.h\"\n#include \"dxvk_pipecache_file.h\"\n")
        replace(WORK / "src/dxvk/dxvk_device.h", "    DxvkObjects                 m_objects;\n",
                "    DxvkPipelineCacheFile       m_pipelineCache;\n    DxvkObjects                 m_objects;\n")
        replace(WORK / "src/dxvk/dxvk_device.h", "    const DxvkDeviceFeatures& features() const {\n",
                "    /**\n     * \\brief Pipeline cache for one pipeline creation call\n     */\n"
                "    VkPipelineCache usePipelineCache() const {\n      return m_pipelineCache.use();\n    }\n\n"
                "    const DxvkDeviceFeatures& features() const {\n")
        replace(WORK / "src/dxvk/dxvk_device.cpp", "    m_perfHints         (getPerfHints()),\n",
                "    m_perfHints         (getPerfHints()),\n    m_pipelineCache     (vkd, adapter->devicePropertiesExt()),\n")
        replace_all(WORK / "src/dxvk/dxvk_graphics.cpp", "vk->vkCreateGraphicsPipelines(vk->device(),\n      VK_NULL_HANDLE,",
                    "vk->vkCreateGraphicsPipelines(vk->device(),\n      m_device->usePipelineCache(),", 2)
        replace_all(WORK / "src/dxvk/dxvk_graphics.cpp", "vk->vkCreateGraphicsPipelines(vk->device(), VK_NULL_HANDLE,",
                    "vk->vkCreateGraphicsPipelines(vk->device(), m_device->usePipelineCache(),", 2)
        replace_all(WORK / "src/dxvk/dxvk_shader.cpp", "vk->vkCreateGraphicsPipelines(vk->device(), VK_NULL_HANDLE,",
                    "vk->vkCreateGraphicsPipelines(vk->device(), m_device->usePipelineCache(),", 2)
        replace_all(WORK / "src/dxvk/dxvk_shader.cpp", "vk->vkCreateComputePipelines(vk->device(), VK_NULL_HANDLE,",
                    "vk->vkCreateComputePipelines(vk->device(), m_device->usePipelineCache(),", 1)
        replace_all(WORK / "src/dxvk/dxvk_compute.cpp", "vk->vkCreateComputePipelines(vk->device(),\n          VK_NULL_HANDLE,",
                    "vk->vkCreateComputePipelines(vk->device(),\n          m_device->usePipelineCache(),", 1)
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
        # Keep the builder's home folder out of the binary (NDK header paths in libc++ assertions).
        prefix_map = f"-ffile-prefix-map={Path.home().as_posix()}=C:/Users/builder"
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
c_args = ['-fPIC', '{prefix_map}']
cpp_args = ['-fPIC', '{prefix_map}']
""")
        # The host tools are used in place; no project or system installation is changed.
        host_tools = args.host_tools.resolve()
        env = os.environ.copy()
        env["PYTHONPATH"] = str(host_tools / "python")
        # vcs_tag runs git describe in the source copy; stop it from finding this project's
        # repository so the version stays DXVK's own (meson project version fallback).
        env["GIT_CEILING_DIRECTORIES"] = str(WORK.parent)
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
