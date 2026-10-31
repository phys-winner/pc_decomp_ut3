/* Android WSI for the native DXVK port. HWND is an acquired ANativeWindow*.
 * An embedding application's explicit guest-display policy may expose one
 * logical monitor. Its dimensions do not change the real Vulkan surface.
 * Offscreen/no-policy callers expose no monitor. Exclusive mode is unsupported.
 */
#include "wsi_platform.h"
#include "../util/util_error.h"
#include <android/native_window.h>
#include <dlfcn.h>
#include <limits>
#include <atomic>
#include <vulkan/vulkan_android.h>

using Ut3DisplayQuery = int (*)(uint32_t*, uint32_t*, uint32_t*);
using Ut3PresentNotify = void (*)(const void*, uint64_t, int32_t);
using Ut3WorkerEntry = void (*)();
static std::atomic<Ut3DisplayQuery> ut3DisplayQuery;
static std::atomic<Ut3PresentNotify> ut3PresentNotify;
static std::atomic<Ut3WorkerEntry> ut3WorkerEntry;
static std::atomic<PFN_vkGetInstanceProcAddr> ut3VulkanLoader;
extern "C" __attribute__((visibility("default"))) void ut3_dxvk_set_vulkan_loader(PFN_vkGetInstanceProcAddr loader) {
  ut3VulkanLoader.store(loader, std::memory_order_release);
}
extern "C" PFN_vkGetInstanceProcAddr ut3_dxvk_native_vulkan_loader() {
  return ut3VulkanLoader.load(std::memory_order_acquire);
}
extern "C" __attribute__((visibility("default"))) void ut3_dxvk_set_callbacks_v2(Ut3DisplayQuery display, Ut3PresentNotify present, Ut3WorkerEntry worker) {
  ut3DisplayQuery.store(display, std::memory_order_release);
  ut3PresentNotify.store(present, std::memory_order_release);
  ut3WorkerEntry.store(worker, std::memory_order_release);
}
extern "C" void ut3_dxvk_native_present_notify(const void* presenter, uint64_t frameId, int32_t result) {
  if (auto notify = ut3PresentNotify.load(std::memory_order_acquire)) notify(presenter, frameId, result);
}
extern "C" void ut3_dxvk_native_worker_enter() {
  if (auto enter = ut3WorkerEntry.load(std::memory_order_acquire)) enter();
}

namespace dxvk::wsi {
static HMONITOR virtualMonitor() { return reinterpret_cast<HMONITOR>(uintptr_t(1)); }
static bool displayMode(WsiMode* mode) {
  using GetMode = int (*)(uint32_t*, uint32_t*, uint32_t*);
  auto query = ut3DisplayQuery.load(std::memory_order_acquire);
  uint32_t width = 0, height = 0, refresh = 0;
  if (!mode || !query || query(&width, &height, &refresh) != 1 || !width || !height || !refresh
      || width > uint32_t(std::numeric_limits<int32_t>::max())
      || height > uint32_t(std::numeric_limits<int32_t>::max())) return false;
  *mode = {width, height, {refresh, 1}, 32, false};
  return true;
}
static bool monitorMode(HMONITOR monitor, WsiMode* mode) {
  return monitor == virtualMonitor() && displayMode(mode);
}
class AndroidDriver final : public WsiDriver {
public:
  std::vector<const char*> getInstanceExtensions() override {
    return {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME};
  }
  HMONITOR getDefaultMonitor() override { WsiMode mode; return displayMode(&mode) ? virtualMonitor() : nullptr; }
  HMONITOR enumMonitors(uint32_t index) override { return index ? nullptr : getDefaultMonitor(); }
  HMONITOR enumMonitors(const LUID*[], uint32_t, uint32_t index) override { return enumMonitors(index); }
  bool getDisplayName(HMONITOR monitor, WCHAR (&name)[32]) override {
    WsiMode mode;if (!monitorMode(monitor, &mode)) return false;
    for (auto& c : name) c = 0;
    const char text[] = "\\\\.\\DISPLAY1";
    for (size_t i = 0; i < sizeof(text) - 1; ++i) name[i] = WCHAR(text[i]);
    return true;
  }
  bool getDesktopCoordinates(HMONITOR monitor, RECT* rect) override {
    WsiMode mode;if (!rect || !monitorMode(monitor, &mode)) return false;
    *rect = {0, 0, LONG(mode.width), LONG(mode.height)};return true;
  }
  bool getDisplayMode(HMONITOR monitor, uint32_t index, WsiMode* mode) override {
    return !index && monitorMode(monitor, mode);
  }
  bool getCurrentDisplayMode(HMONITOR monitor, WsiMode* mode) override { return monitorMode(monitor, mode); }
  bool getDesktopDisplayMode(HMONITOR monitor, WsiMode* mode) override { return monitorMode(monitor, mode); }
  WsiEdidData getMonitorEdid(HMONITOR) override { return {}; }
  void getWindowSize(HWND w, uint32_t* width, uint32_t* height) override {
    auto* window = static_cast<ANativeWindow*>(w);
    if (width) *width = window ? std::max(0, ANativeWindow_getWidth(window)) : 0;
    if (height) *height = window ? std::max(0, ANativeWindow_getHeight(window)) : 0;
  }
  void resizeWindow(HWND window, DxvkWindowState*, uint32_t width, uint32_t height) override {
    WsiMode mode;
    if (window && displayMode(&mode) && width == mode.width && height == mode.height) return;
    throw DxvkError("Android WSI: application-controlled resize is unsupported");
  }
  bool setWindowMode(HMONITOR, HWND, const WsiMode&) override { return false; }
  bool enterFullscreenMode(HMONITOR, HWND, DxvkWindowState*, bool) override { return false; }
  bool leaveFullscreenMode(HWND, DxvkWindowState*, bool) override { return false; }
  bool restoreDisplayMode() override { return false; }
  HMONITOR getWindowMonitor(HWND window) override { return window ? getDefaultMonitor() : nullptr; }
  bool isWindow(HWND w) override { return w != nullptr; }
  bool isMinimized(HWND w) override {
    return !w || ANativeWindow_getWidth(static_cast<ANativeWindow*>(w)) <= 0
              || ANativeWindow_getHeight(static_cast<ANativeWindow*>(w)) <= 0;
  }
  bool isOccluded(HWND w) override { return isMinimized(w); }
  void updateFullscreenWindow(HMONITOR, HWND, bool) override {
    throw DxvkError("Android WSI: fullscreen display control is unsupported");
  }
  VkResult createSurface(HWND w, PFN_vkGetInstanceProcAddr gipa,
                         VkInstance instance, VkSurfaceKHR* surface) override {
    if (!w || !surface || !gipa) return VK_ERROR_INITIALIZATION_FAILED;
    auto create = reinterpret_cast<PFN_vkCreateAndroidSurfaceKHR>(
      gipa(instance, "vkCreateAndroidSurfaceKHR"));
    if (!create) return VK_ERROR_EXTENSION_NOT_PRESENT;
    VkAndroidSurfaceCreateInfoKHR info = {VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};
    info.window = static_cast<ANativeWindow*>(w);
    return create(instance, &info, nullptr, surface);
  }
};
static bool createAndroidDriver(WsiDriver** driver) {
  *driver = new AndroidDriver();
  return true;
}
WsiBootstrap AndroidWSI = {"Android", createAndroidDriver};
}
