// Persistent VkPipelineCache for DXVK on Android.
//
// DXVK 2.4.1 creates every pipeline with VK_NULL_HANDLE and relies on the driver's own disk cache.
// Qualcomm's proprietary Vulkan driver keeps none between runs, and Turnip's Mesa disk cache has no
// cache folder on Android, so every start recompiled every pipeline from scratch: the state cache's
// workers then race the game, and pipelines first seen in play (a weapon switch, an effect) stall
// the frame for the full driver compile.
//
// One cache per DxvkDevice, shared by all pipeline creation (VkPipelineCache is internally
// synchronized). Its file lives beside DXVK's state cache (DXVK_STATE_CACHE_PATH), named after the
// executable and the driver: <exe>.<vendor>-<device>-d<driverID>-<driver version>-<hash of
// pipelineCacheUUID>.vkpipelinecache, so the system driver and Turnip (different driverIDs) keep
// files of their own. Data the driver does not recognise as its own is never passed to it. A
// background thread writes the file (temporary file + rename) when pipeline creation has been quiet
// for 8 s and the last write is 2 minutes old (5 minutes while creation never stops), and once more
// when the device is destroyed; Android processes are usually killed rather than shut down. Turnip's
// caches reach 70-150 MB on WAR-Torlan, so writes stay rare to spare the flash.
//
// Every driver version (an OS update, another Turnip) gets a new file, so the thread first prunes the
// folder: of the files with this vendor, device and driverID it keeps the current one and the newest
// other one (switching between the built-in and an own Turnip, both MESA_TURNIP, keeps both caches)
// and removes the rest, names of the format before the driverID was part of them, and .part files of
// writes cut short. Files of other drivers and anything not named like a cache stay untouched.
//
// Included into the DXVK tree by tools/build_dxvk.py (--rebuild) as src/dxvk/dxvk_pipecache_file.h.
#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

#include "../util/log/log.h"
#include "../util/util_env.h"
#include "../util/util_string.h"
#include "../util/thread.h"
#include "../vulkan/vulkan_loader.h"

namespace dxvk {

  // File names and pruning; plain C++17, so tests/test_dxvk_pipeline_cache_prune.py runs them on a PC.
  namespace pipecache_files {

    struct Name {
      uint32_t vendor = 0, device = 0, driverId = 0, version = 0;
      unsigned long long uuid = 0;
      bool hasDriverId = false;   // false: the format before the driverID was part of the name
      bool partial = false;       // a .part file a write left
    };

    inline std::string format(const std::string& exe, uint32_t vendor, uint32_t device, uint32_t driverId,
        uint32_t version, unsigned long long uuid) {
      char name[96];
      std::snprintf(name, sizeof(name), ".%04x-%08x-d%u-%08x-%016llx.vkpipelinecache",
        vendor, device, driverId, version, uuid);
      return exe + name;
    }

    /// Whether `file` is exactly a name format() gives for `exe` (or the earlier format), with .part or not.
    inline bool parse(const std::string& exe, const std::string& file, Name* out) {
      static const std::string suffix = ".vkpipelinecache";
      std::string name = file;
      *out = Name();
      out->partial = name.size() > 5 && !name.compare(name.size() - 5, 5, ".part");
      if (out->partial)
        name.resize(name.size() - 5);
      if (name.size() <= exe.size() + 1 + suffix.size() || name.compare(0, exe.size(), exe)
       || name[exe.size()] != '.' || name.compare(name.size() - suffix.size(), suffix.size(), suffix))
        return false;

      std::string id = name.substr(exe.size() + 1, name.size() - exe.size() - 1 - suffix.size());
      unsigned int vendor = 0, device = 0, driverId = 0, version = 0;
      unsigned long long uuid = 0;
      char canonical[96];
      if (std::sscanf(id.c_str(), "%x-%x-d%u-%x-%llx", &vendor, &device, &driverId, &version, &uuid) == 5) {
        std::snprintf(canonical, sizeof(canonical), "%04x-%08x-d%u-%08x-%016llx", vendor, device, driverId, version, uuid);
        out->hasDriverId = true;
      } else if (std::sscanf(id.c_str(), "%x-%x-%x-%llx", &vendor, &device, &version, &uuid) == 4) {
        std::snprintf(canonical, sizeof(canonical), "%04x-%08x-%08x-%016llx", vendor, device, version, uuid);
        driverId = 0;
      } else
        return false;

      if (id != canonical)
        return false;
      out->vendor = vendor;
      out->device = device;
      out->driverId = driverId;
      out->version = version;
      out->uuid = uuid;
      return true;
    }

    /// Removes the files of `dir` named for `exe` that the current cache file `current` makes stale
    /// (see the comment at the top). Returns how many were removed and their bytes in *freed.
    inline uint32_t prune(const std::string& dir, const std::string& exe, const std::string& current,
        uint32_t vendor, uint32_t device, uint32_t driverId, uint64_t* freed) {
      namespace fs = std::filesystem;
      struct Other { fs::path path; fs::file_time_type time; };
      std::vector<Other> others;
      std::vector<fs::path> stale;
      std::error_code ec;

      for (fs::directory_iterator it(dir.empty() ? fs::path(".") : fs::path(dir), ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code fileEc;
        if (!fs::is_regular_file(it->symlink_status(fileEc)) || fileEc)
          continue;

        std::string file = it->path().filename().string();
        Name name;
        if (file == current || !parse(exe, file, &name))
          continue;

        if (name.partial || !name.hasDriverId)
          stale.push_back(it->path());
        else if (name.vendor == vendor && name.device == device && name.driverId == driverId) {
          fs::file_time_type time = it->last_write_time(fileEc);
          others.push_back({ it->path(), fileEc ? fs::file_time_type::min() : time });
        }
      }

      // The newest other file of this driver stays, older ones go.
      std::sort(others.begin(), others.end(), [] (const Other& a, const Other& b) { return a.time > b.time; });
      for (size_t i = 1; i < others.size(); i++)
        stale.push_back(others[i].path);

      uint32_t removed = 0;
      *freed = 0;
      for (const auto& path : stale) {
        std::error_code fileEc;
        uintmax_t size = fs::file_size(path, fileEc);
        if (fileEc)
          size = 0;
        if (fs::remove(path, fileEc) && !fileEc) {
          removed++;
          *freed += size;
        }
      }
      return removed;
    }

  }


  class DxvkPipelineCacheFile {

  public:

    DxvkPipelineCacheFile(
      const Rc<vk::DeviceFn>&             vkd,
      const DxvkDeviceInfo&               info)
    : m_vkd(vkd), m_properties(info.core.properties), m_driverId(uint32_t(info.vk12.driverID)) {
      m_dir = env::getEnvVar("DXVK_STATE_CACHE_PATH");
      if (env::getEnvVar("DXVK_PIPELINE_CACHE") == "0" || !m_vkd->vkCreatePipelineCache)
        return;

      uint64_t uuid = 1469598103934665603ull;
      for (uint32_t i = 0; i < VK_UUID_SIZE; i++)
        uuid = (uuid ^ m_properties.pipelineCacheUUID[i]) * 1099511628211ull;

      m_exe = env::getExeBaseName();
      m_name = pipecache_files::format(m_exe, m_properties.vendorID, m_properties.deviceID, m_driverId,
        m_properties.driverVersion, static_cast<unsigned long long>(uuid));
      m_path = (m_dir.empty() ? std::string() : m_dir + "/") + m_name;

      std::vector<char> data = readFile();

      VkPipelineCacheCreateInfo createInfo = { VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
      createInfo.initialDataSize = data.size();
      createInfo.pInitialData    = data.empty() ? nullptr : data.data();

      if (m_vkd->vkCreatePipelineCache(m_vkd->device(), &createInfo, nullptr, &m_cache) != VK_SUCCESS) {
        // A driver may still refuse data that carries its own header; start empty.
        createInfo.initialDataSize = 0;
        createInfo.pInitialData    = nullptr;
        if (m_vkd->vkCreatePipelineCache(m_vkd->device(), &createInfo, nullptr, &m_cache) != VK_SUCCESS) {
          m_cache = VK_NULL_HANDLE;
          Logger::warn("DXVK: Failed to create pipeline cache");
          return;
        }
        data.clear();
      }

      m_savedSize = data.size();
      Logger::info(str::format("DXVK: Pipeline cache ", m_path, ": ", data.size(), " bytes loaded"));
      m_thread = dxvk::thread([this] { run(); });
    }

    ~DxvkPipelineCacheFile() {
      if (m_thread.joinable()) {
        { std::lock_guard<std::mutex> lock(m_mutex);
          m_stop = true; }
        m_cond.notify_all();
        m_thread.join();
      }

      if (m_cache) {
        save();
        m_vkd->vkDestroyPipelineCache(m_vkd->device(), m_cache, nullptr);
      }
    }

    DxvkPipelineCacheFile             (const DxvkPipelineCacheFile&) = delete;
    DxvkPipelineCacheFile& operator = (const DxvkPipelineCacheFile&) = delete;

    /// The cache for one pipeline creation call; marks the file as out of date.
    VkPipelineCache use() const {
      if (m_cache)
        m_created.fetch_add(1, std::memory_order_relaxed);
      return m_cache;
    }

  private:

    Rc<vk::DeviceFn>            m_vkd;
    VkPhysicalDeviceProperties  m_properties;
    uint32_t                    m_driverId = 0;
    std::string                 m_dir;
    std::string                 m_exe;
    std::string                 m_name;
    std::string                 m_path;
    VkPipelineCache             m_cache = VK_NULL_HANDLE;
    mutable std::atomic<uint64_t> m_created = { 0 };
    uint64_t                    m_saved = 0;
    size_t                      m_savedSize = 0;
    std::mutex                  m_mutex;
    std::condition_variable     m_cond;
    bool                        m_stop = false;
    dxvk::thread                m_thread;

    std::vector<char> readFile() const {
      std::ifstream file(m_path, std::ios::binary);
      if (!file)
        return { };

      std::vector<char> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

      // Only data with this driver's own header: VkPipelineCacheHeaderVersionOne.
      VkPipelineCacheHeaderVersionOne header = { };
      if (data.size() < sizeof(header))
        return { };

      std::memcpy(&header, data.data(), sizeof(header));

      if (header.headerSize < sizeof(header)
       || header.headerVersion != VK_PIPELINE_CACHE_HEADER_VERSION_ONE
       || header.vendorID != m_properties.vendorID
       || header.deviceID != m_properties.deviceID
       || std::memcmp(header.pipelineCacheUUID, m_properties.pipelineCacheUUID, VK_UUID_SIZE))
        return { };

      return data;
    }

    void prune() {
      uint64_t freed = 0;
      uint32_t removed = pipecache_files::prune(m_dir, m_exe, m_name,
        m_properties.vendorID, m_properties.deviceID, m_driverId, &freed);
      if (removed)
        Logger::info(str::format("DXVK: Pipeline cache: removed ", removed, " stale file(s), ", freed >> 20, " MiB"));
    }

    void run() {
      using namespace std::chrono_literals;
      prune();

      auto lastSave = std::chrono::steady_clock::now() - 120s;  // the first quiet spell saves
      uint64_t lastSeen = m_created.load();

      std::unique_lock<std::mutex> lock(m_mutex);

      while (!m_cond.wait_for(lock, 8s, [this] { return m_stop; })) {
        uint64_t created = m_created.load(std::memory_order_relaxed);
        auto now = std::chrono::steady_clock::now();
        bool quiet = created == lastSeen;
        lastSeen = created;

        if (created != m_saved && ((quiet && now - lastSave >= 120s) || now - lastSave >= 300s)) {
          lock.unlock();
          save();
          lock.lock();
          lastSave = std::chrono::steady_clock::now();
        }
      }
    }

    void save() {
      uint64_t created = m_created.load(std::memory_order_relaxed);
      if (created == m_saved)
        return;

      size_t size = 0;
      if (m_vkd->vkGetPipelineCacheData(m_vkd->device(), m_cache, &size, nullptr) != VK_SUCCESS || !size)
        return;

      std::vector<char> data(size);
      VkResult vr = m_vkd->vkGetPipelineCacheData(m_vkd->device(), m_cache, &size, data.data());
      if (vr != VK_SUCCESS || !size)
        return;

      m_saved = created;
      if (size == m_savedSize)
        return;

      std::string temporary = m_path + ".part";
      bool ok;
      { std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        ok = file.write(data.data(), size).good();
        file.close();
        ok = ok && !file.fail(); }

      if (ok && !std::rename(temporary.c_str(), m_path.c_str())) {
        m_savedSize = size;
        Logger::info(str::format("DXVK: Pipeline cache saved: ", size, " bytes"));
      } else {
        std::remove(temporary.c_str());
        Logger::warn(str::format("DXVK: Failed to write pipeline cache ", m_path));
      }
    }

  };

}
