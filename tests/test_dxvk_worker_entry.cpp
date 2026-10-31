/* The same Android thread constructor overlay compiled into native DXVK. */
#include "../build/dxvk-source/src/util/thread.h"
#include <cassert>
#include <vector>
extern "C" void ut3_fixture_dxvk_worker_enter();
extern "C" void ut3_dxvk_native_worker_enter(){ut3_fixture_dxvk_worker_enter();}
extern "C" void ut3_fixture_run_dxvk_workers(void (*work)()) {
  dxvk::thread first([work]{work();});
  dxvk::thread moved(std::move(first));assert(!first.joinable());moved.join();
  std::vector<dxvk::thread> workers;
  for(unsigned i=0;i<8;++i)workers.emplace_back([work]{work();});
  for(auto &worker:workers)worker.join();
}
