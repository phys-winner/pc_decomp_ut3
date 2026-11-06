"""HOST-run the query pool choice of android/dxvk/query_pools.inc against upstream DXVK's allocator.

The plain C++17 part (namespace query_pools) is taken from the overlay as it is, compiled with MSVC and
driven like DxvkGpuQueryAllocator: per-pool LIFO lists of handles, a new pool when none is free. A
simulated UE3 frame begins N occlusion queries in one render and reads the previous frame's results in
another order (which returns their handles). Counted: pool switches between consecutive queries of a
render, the cause of PowerVR's render splits. Upstream's single LIFO list over all pools is simulated
next to it. With one pool both must hand out exactly the same handles in the same order. No device."""
from pathlib import Path
import subprocess
import unittest

from tests.host_environment import environment

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/tests/dxvk-query-pools"

HARNESS = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <utility>
#include <vector>

namespace dxvk {
/* ACTUAL */
}

struct Handle { uint32_t pool, id; };

// The overlay's allocator: free handles per pool, query_pools::pick chooses the pool.
struct Affine {
  uint32_t size;
  std::vector<std::vector<Handle>> lists;
  size_t current = 0;
  uint32_t pools = 0;
  explicit Affine(uint32_t s) : size(s) { }
  Handle alloc() {
    size_t pool = dxvk::query_pools::pick(lists, current);
    if (pool == lists.size()) {
      lists.emplace_back();
      for (uint32_t i = 0; i < size; i++) lists.back().push_back({pools, i});
      pools++;
    }
    current = pool;
    Handle h = lists[pool].back();
    lists[pool].pop_back();
    return h;
  }
  void release(Handle h) { lists[h.pool].push_back(h); }
};

// Upstream DXVK 2.4.1: one LIFO list over all pools.
struct Upstream {
  uint32_t size;
  std::vector<Handle> list;
  uint32_t pools = 0;
  explicit Upstream(uint32_t s) : size(s) { }
  Handle alloc() {
    if (list.empty()) {
      for (uint32_t i = 0; i < size; i++) list.push_back({pools, i});
      pools++;
    }
    Handle h = list.back();
    list.pop_back();
    return h;
  }
  void release(Handle h) { list.push_back(h); }
};

// frames of `queries` queries in one render each; the previous frame's handles come back in a shuffled
// order before the next render (UE3 reads last frame's results while it sets the frame up).
template<typename A>
void run(A& a, uint32_t queries, uint32_t frames, uint32_t seed, uint64_t* switches, uint64_t* worst) {
  std::mt19937 rng(seed);
  std::vector<Handle> previous;
  *switches = 0; *worst = 0;
  for (uint32_t f = 0; f < frames; f++) {
    std::shuffle(previous.begin(), previous.end(), rng);
    for (Handle h : previous) a.release(h);
    previous.clear();
    uint64_t frameSwitches = 0;
    for (uint32_t q = 0; q < queries; q++) {
      Handle h = a.alloc();
      if (q && h.pool != previous.back().pool) frameSwitches++;
      previous.push_back(h);
    }
    *switches += frameSwitches;
    if (frameSwitches > *worst) *worst = frameSwitches;
  }
}

int main(int argc, char** argv) {
  if (argc == 2 && !std::strcmp(argv[1], "pick")) {
    using L = std::vector<std::vector<int>>;
    assert(dxvk::query_pools::pick(L{}, 0) == 0);                         // no pool: create one
    assert(dxvk::query_pools::pick(L{{}}, 0) == 1);                       // all used up: create one
    assert(dxvk::query_pools::pick(L{{1}, {1, 2}}, 0) == 0);              // stay on the current pool
    assert(dxvk::query_pools::pick(L{{}, {1}, {1, 2, 3}, {1, 2}}, 0) == 2); // then the one with most free
    assert(dxvk::query_pools::pick(L{{1, 2}, {}, {1, 2}}, 1) == 0);       // ties: the first
    assert(dxvk::query_pools::pick(L{{1}}, 5) == 0);                      // a stale index is ignored
    std::puts("PASS pick");
    return 0;
  }
  if (argc == 2 && !std::strcmp(argv[1], "single")) {
    // One pool: the overlay hands out exactly upstream's handles in upstream's order.
    Affine a(4096); Upstream u(4096);
    std::mt19937 rng(7);
    std::vector<Handle> heldA, heldU;
    for (int step = 0; step < 200000; step++) {
      bool give = heldA.empty() || (heldA.size() < 3000 && rng() % 2);
      if (give) {
        Handle x = a.alloc(), y = u.alloc();
        assert(x.pool == 0 && y.pool == 0 && x.id == y.id);
        heldA.push_back(x); heldU.push_back(y);
      } else {
        size_t i = rng() % heldA.size();
        a.release(heldA[i]); u.release(heldU[i]);
        heldA[i] = heldA.back(); heldA.pop_back(); heldU[i] = heldU.back(); heldU.pop_back();
      }
    }
    assert(a.pools == 1 && u.pools == 1);
    std::puts("PASS single");
    return 0;
  }
  if (argc != 5)
    return 2;
  uint32_t size = uint32_t(std::atoi(argv[1])), queries = uint32_t(std::atoi(argv[2])), frames = uint32_t(std::atoi(argv[3]));
  uint32_t seed = uint32_t(std::atoi(argv[4]));
  uint64_t as, aw, us, uw;
  Affine a(size); Upstream u(size);
  run(a, queries, frames, seed, &as, &aw);
  run(u, queries, frames, seed, &us, &uw);
  std::printf("affine %llu %llu %u upstream %llu %llu %u\n", (unsigned long long)as, (unsigned long long)aw, a.pools,
              (unsigned long long)us, (unsigned long long)uw, u.pools);
  return 0;
}
'''


def block(text, opening):
    start = text.index(opening)
    depth, end = 1, text.index("{", start) + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end] + "\n"


class QueryPoolsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = (ROOT / "android/dxvk/query_pools.inc").read_text(encoding="utf-8")
        OUT.mkdir(parents=True, exist_ok=True)
        (OUT / "probe.cpp").write_text(HARNESS.replace("/* ACTUAL */", block(source, "  namespace query_pools {")),
                                       encoding="utf-8")
        cls.env, linker = environment("x64")
        for command in ([linker.parent / "cl.exe", "/nologo", "/O2", "/std:c++17", "/EHsc", "/W4", "/WX",
                         "/c", "probe.cpp", "/Foprobe.obj"],
                        [linker, "/nologo", "probe.obj", "/OUT:probe.exe"]):
            result = subprocess.run([str(part) for part in command], cwd=OUT, env=cls.env, capture_output=True, timeout=300)
            assert result.returncode == 0, (result.stdout + result.stderr).decode(errors="replace")

    def probe(self, *args):
        result = subprocess.run([str(OUT / "probe.exe"), *map(str, args)], capture_output=True, text=True, timeout=120)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout

    def simulate(self, size, queries, frames=300, seed=1):
        words = self.probe(size, queries, frames, seed).split()
        return {"affine": tuple(map(int, words[1:4])), "upstream": tuple(map(int, words[5:8]))}

    def test_pick(self):
        self.assertIn("PASS pick", self.probe("pick"))

    def test_single_pool_is_upstream_order(self):
        self.assertIn("PASS single", self.probe("single"))

    def test_powervr_pool_holds_ue3_frames_in_one_pool(self):
        # UE3 begins ~250-600 occlusion queries a frame and holds them until the next frame reads them.
        for queries in (250, 600, 1500):
            result = self.simulate(4096, queries)
            self.assertEqual(result["affine"], (0, 0, 1), result)
            self.assertEqual(result["upstream"], (0, 0, 1), result)

    def test_small_pools_switch_once_per_used_up_pool(self):
        # The earlier 64-entry PowerVR pools: upstream alternates between pools inside a render.
        for queries in (250, 600):
            result = self.simulate(64, queries)
            switches, worst, pools = result["affine"]
            up_switches, up_worst, up_pools = result["upstream"]
            limit = -(-queries // 64)                  # a render needs ceil(queries / 64) pools at least
            self.assertLessEqual(worst, limit, result)
            self.assertGreater(up_worst, 3 * limit, result)
            self.assertLess(switches * 5, up_switches, result)
            self.assertLessEqual(pools, up_pools + 1, result)

    def test_overflow_into_a_second_pool_stays_rare(self):
        # More queries than one pool holds: the render switches pools a few times, not at every query.
        result = self.simulate(1024, 1500, frames=200, seed=3)
        switches, worst, pools = result["affine"]
        self.assertLessEqual(worst, 3, result)
        self.assertGreater(result["upstream"][1], 100, result)
        self.assertLessEqual(pools, 3, result)


if __name__ == "__main__":
    unittest.main()
