// Occlusion query pools for DXVK on Android: the pool size per driver, and query handles kept per pool.
//
// PowerVR's proprietary Vulkan driver (Pixel 10, D-Series DXT-48-1536, driver 26.1@7036548) ends the
// render it is in and starts a new one whenever an occlusion query uses another VkQueryPool than the
// query before it in the same render: every attachment is stored to memory and loaded again. Measured
// with raw Vulkan there (one 2424x1080 RGBA16F + D24S8 render, 256 draws, each inside a query): 2.2 ms
// of GPU time with all queries in one pool, 12.9 ms with a new 64-entry pool after every 64 queries,
// 223-284 ms when consecutive queries alternate between 64-entry pools.
//
// Upstream DXVK hands handles out from one LIFO list over all pools, so as soon as several pools are in
// use consecutive queries come from pools in whatever order their results were read. This driver needs
// pools smaller than upstream's 16384 entries (a 16384-entry pool reports 8 instead of 10210 samples at
// index 8191 and 0 at 16383; 64 to 8192 entries are exact with every slot of the pool in one render), and
// the 64-entry pools used for that before made UE3's ~250-600 queries a frame alternate between pools:
// the frames cost ~10 times their GPU time (DXVK 9-13 fps where GLES holds its 60 fps limit).
//
// Now the PowerVR pool has 4096 entries, half the largest size verified exact, more than UE3 has in
// flight; and every driver gets handles from one pool until that pool has no free handle left, then
// from the pool with the most free handles, a new pool only when all are in use. With a single pool
// this is upstream's order exactly (the same LIFO list).
//
// Included into the DXVK tree by tools/build_dxvk.py (--rebuild) as src/dxvk/dxvk_query_pools.inc,
// inside namespace dxvk of src/dxvk/dxvk_gpu_query.cpp, in place of upstream's allocQuery and freeQuery.

  // Pool choice; plain C++17, so tests/test_dxvk_query_pools.py runs it on a PC.
  namespace query_pools {

    /**
     * \brief Pool the next query handle comes from
     *
     * \param [in] lists Free handles of each pool
     * \param [in] current Pool of the previous allocation
     * \returns Index of the pool, \c lists.size() when no pool has a free handle
     */
    template<typename Lists>
    size_t pick(const Lists& lists, size_t current) {
      if (current < lists.size() && !lists[current].empty())
        return current;

      size_t best = lists.size();

      for (size_t i = 0; i < lists.size(); i++) {
        if (!lists[i].empty() && (best == lists.size() || lists[i].size() > lists[best].size()))
          best = i;
      }

      return best;
    }

  }


  static uint32_t occlusionQueryPoolSize(DxvkDevice* device) {
#if defined(DXVK_WSI_ANDROID)
    if (device->adapter()->matchesDriver(VK_DRIVER_ID_IMAGINATION_PROPRIETARY))
      return 4096;
#endif
    return 16384;
  }


  DxvkGpuQueryHandle DxvkGpuQueryAllocator::allocQuery() {
    std::lock_guard<dxvk::mutex> lock(m_mutex);

    size_t pool = query_pools::pick(m_handles, m_currentPool);

    if (pool == m_handles.size()) {
      this->createQueryPool();

      if (pool == m_handles.size())
        return DxvkGpuQueryHandle();
    }

    m_currentPool = pool;

    DxvkGpuQueryHandle result = m_handles[pool].back();
    m_handles[pool].pop_back();
    return result;
  }


  void DxvkGpuQueryAllocator::freeQuery(DxvkGpuQueryHandle handle) {
    std::lock_guard<dxvk::mutex> lock(m_mutex);

    for (size_t i = 0; i < m_pools.size(); i++) {
      if (m_pools[i] == handle.queryPool) {
        m_handles[i].push_back(handle);
        return;
      }
    }
  }
