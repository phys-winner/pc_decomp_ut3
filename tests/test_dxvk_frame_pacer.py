"""Execute the pinned DXVK limiter and verify real linked Presenter call edges."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import unittest

from capstone import Cs, CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN
from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build/dxvk-frame-pacer"
DXVK = ROOT / "reference/dxvk"


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class DxvkFramePacerTest(unittest.TestCase):
    def test_original_offline_engine_smoothing_flag(self):
        from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
        from unicorn.x86_const import UC_X86_REG_ESP, UC_X86_REG_ECX
        import pefile
        OUT.mkdir(parents=True, exist_ok=True)
        config = json.loads((ROOT / "config/inputs.json").read_text())
        path = Path(config["source_root"]) / "Binaries/UT3.exe"
        digest = next(x["sha256"] for x in config["modules"] if x["path"] == "Binaries/UT3.exe")
        self.assertEqual(sha(path), digest)
        pe = pefile.PE(str(path), fast_load=True)
        image = pe.get_memory_mapped_image()
        cases = []
        for maximum, previous, enabled in [(60, 100, True), (62, 100, True), (60, 58, True), (62, 58, True)] + [
                (rate, 100, False) for rate in (30, 45, 60, 90, 120, 144, 240, 1000)]:
            vm = Uc(UC_ARCH_X86, UC_MODE_32)
            vm.mem_map(0, 4096)
            vm.mem_map(0x400000, (len(image) + 4095) & ~4095)
            vm.mem_write(0x400000, image)
            vm.mem_map(0x20000000, 0x10000)
            vm.mem_map(0x30000000, 0x10000)
            def word(address, value):
                vm.mem_write(address, struct.pack("<I", value))
            def floating(address, value):
                vm.mem_write(address, struct.pack("<f", value))
            stack, engine = 0x30008000, 0x20001000
            word(stack, 0x30000000)
            floating(stack + 4, 1 / 60)
            word(stack + 8, 1)  # AllowFrameSmoothing
            word(0x1DEE5EC, 0)  # Offline/null world: no network tick cap.
            word(0x1DB44A0, 1)
            word(engine + 0x44C, 8 if enabled else 0)
            floating(engine + 0x450, maximum)
            floating(engine + 0x454, 22)
            floating(0x1D1CB8C, 1 / previous)
            old_average = bytes(vm.mem_read(0x1D1CB8C, 4))
            vm.reg_write(UC_X86_REG_ESP, stack)
            vm.reg_write(UC_X86_REG_ECX, engine)
            returned = []
            def step(machine, address, size, data):
                if address == 0x102AF8F:  # Before original FLD return value.
                    sp = machine.reg_read(UC_X86_REG_ESP)
                    returned.append(struct.unpack("<f", machine.mem_read(sp + 0xC, 4))[0])
                    machine.emu_stop()
            vm.hook_add(UC_HOOK_CODE, step)
            vm.emu_start(0x102AC60, 0, count=10000)
            self.assertEqual(len(returned), 1)
            if not enabled:
                self.assertEqual(returned[0], 0)
                self.assertEqual(bytes(vm.mem_read(0x1D1CB8C, 4)), old_average)
            elif previous == 100:
                self.assertEqual(returned[0], maximum)
            else:
                self.assertAlmostEqual(returned[0], 58.00644302368164, places=5)
            cases.append({"maximum": maximum, "prior_average_fps": previous,
                          "smoothing": enabled, "returned_tick_cap": returned[0]})
        (OUT / "original-engine-oracle.json").write_text(json.dumps({
            "status": "PASS_ORIGINAL_X86_OFFLINE_SMOOTHING_FLAG", "original_exe_sha256": digest,
            "entry": "0x0102AC60", "return_probe": "0x0102AF8F", "cases": cases,
            "limitation": "Offline/null-world path only; original role/network branches are unchanged, not disabled by this candidate."
        }, indent=2) + "\n")
        print("PASS original x86:12 smoothing on/off cases, no adaptive cap or average update when disabled offline; Max60/62 comparison")

    def test_pinned_limiter_and_real_presenter_edges(self):
        OUT.mkdir(parents=True, exist_ok=True)
        pins = json.loads((ROOT / "dependencies-dxvk.json").read_text())["source_files"]
        names = ("src/util/util_fps_limiter.cpp", "src/util/util_fps_limiter.h",
                 "src/util/util_env.cpp", "src/dxvk/dxvk_presenter.cpp",
                 "src/dxvk/dxvk_presenter.h", "src/d3d9/d3d9_swapchain.cpp")
        hashes = {}
        for name in names:
            path = DXVK / name
            hashes[name] = sha(path)
            self.assertEqual(hashes[name], pins[name])
            reference, work = path.read_text(), (ROOT / "build/dxvk-source" / name).read_text()
            if name == "src/dxvk/dxvk_presenter.cpp":
                for start, end in (("  void Presenter::signalFrame(", "  VkResult Presenter::recreateSurface("),
                                   ("  void Presenter::runFrameThread()", None)):
                    self.assertEqual(reference[reference.index(start):reference.index(end) if end else len(reference)],
                                     work[work.index(start):work.index(end) if end else len(work)])
            elif name == "src/d3d9/d3d9_swapchain.cpp":
                start, end = "  void D3D9SwapChainEx::SyncFrameLatency()", "  void D3D9SwapChainEx::SetApiName("
                self.assertEqual(reference[reference.index(start):reference.index(end)], work[work.index(start):work.index(end)])
            else:
                self.assertEqual(reference, work)
        receipt = json.loads((ROOT / "build/dxvk/build-receipt.json").read_text())
        library = ROOT / "build/dxvk/libdxvk_d3d9.so"
        self.assertEqual(sha(library), next(x["sha256"] for x in receipt["outputs"]
                                         if Path(x["path"]).name == library.name))
        expected = {
            "_ZN4dxvk10FpsLimiterC2Ev": "_ZN4dxvk3env9getEnvVarEPKc",
            "_ZN4dxvk9Presenter11signalFrameE8VkResult16VkPresentModeKHRm": "_ZN4dxvk10FpsLimiter5delayEv",
            "_ZN4dxvk9Presenter14runFrameThreadEv": "_ZN4dxvk10FpsLimiter5delayEv",
        }
        edges = []
        with library.open("rb") as stream:
            elf = ELFFile(stream)
            symbols = {s.name: s for s in elf.get_section_by_name(".symtab").iter_symbols() if s.name}
            for owner, target in expected.items():
                symbol = symbols[owner]
                section = elf.get_section(symbol["st_shndx"])
                stream.seek(section["sh_offset"] + symbol["st_value"] - section["sh_addr"])
                found = []
                for instruction in Cs(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN).disasm(
                        stream.read(symbol["st_size"]), symbol["st_value"]):
                    if instruction.mnemonic == "bl" and int(instruction.op_str.lstrip("#"), 16) == symbols[target]["st_value"]:
                        found.append(hex(instruction.address))
                self.assertEqual(len(found), 1, owner)
                edges.append({"owner": owner, "target": target, "linked_call": found[0]})
        # Only platform/time boundaries are substituted. The complete production
        # class declaration and every limiter method execute verbatim.
        header = re.sub(r"^#include[^\n]*\n", "", (DXVK / names[1]).read_text(), flags=re.M)
        implementation = re.sub(r"^#include[^\n]*\n", "", (DXVK / names[0]).read_text(), flags=re.M)
        source = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <string>
namespace dxvk {
using mutex=std::mutex;
static int64_t clock_value;
static unsigned sleep_calls;
static std::string environment;
struct high_resolution_clock {
    using rep=int64_t; using period=std::nano; using duration=std::chrono::nanoseconds;
    using time_point=std::chrono::time_point<high_resolution_clock>;
    static time_point now() { return time_point(duration(clock_value)); }
};
struct Sleep {
    static void sleepUntil(high_resolution_clock::time_point, high_resolution_clock::time_point target) {
        assert(target.time_since_epoch().count()>=clock_value);
        clock_value=target.time_since_epoch().count(); ++sleep_calls;
    }
};
namespace env { std::string getEnvVar(const char* name) { assert(std::string(name)=="DXVK_FRAME_RATE");return environment; } }
namespace str { template<class... T>std::string format(const T&...) { return ""; } }
struct Logger { static void info(const std::string&) {} };
}
''' + header + implementation + r'''
int main() {
    unsigned cases=0;
    for(int rate:{30,45,60,90,120,144,240,1000}) {
        for(bool overrideEnvironment:{false,true}) {
            dxvk::environment=overrideEnvironment?std::to_string(rate):"";
            dxvk::clock_value=1000000000; dxvk::sleep_calls=0;
            dxvk::FpsLimiter limiter;
            limiter.setTargetFrameRate(overrideEnvironment?0:rate,3);
            if(overrideEnvironment) {
                limiter.setTargetFrameRate(-144,7);
                limiter.setTargetFrameRate(12,1);
            }
            const int64_t period=1000000000LL/rate, start=dxvk::clock_value;
            limiter.delay();
            assert(dxvk::sleep_calls==0);
            for(int frame=1;frame<=12;++frame) {
                dxvk::clock_value+=period/4;
                limiter.delay();
                assert(dxvk::clock_value==start+frame*period);
                assert(dxvk::sleep_calls==unsigned(frame));
            }
            // A late frame rebases the deadline; the next call waits one period.
            dxvk::clock_value+=4*period;
            int64_t late=dxvk::clock_value;unsigned previous=dxvk::sleep_calls;
            limiter.delay();assert(dxvk::clock_value==late && dxvk::sleep_calls==previous);
            dxvk::clock_value+=period/4;limiter.delay();
            assert(dxvk::clock_value==late+period && dxvk::sleep_calls==previous+1);
            ++cases;
        }
    }
    for(const char* value:{"","invalid","0"}) {
        dxvk::environment=value;dxvk::clock_value=1000000000;dxvk::sleep_calls=0;
        dxvk::FpsLimiter limiter;
        for(int i=0;i<8;++i) {limiter.delay();dxvk::clock_value+=1000;}
        assert(dxvk::sleep_calls==0);++cases;
    }
    std::printf("PASS pinned production DXVK limiter: %u rate/environment/deadline cases; 30/45/60/display rates, environment override, slow-frame rebase, disabled/invalid controls\n",cases);
}
'''
        fixture = OUT / "limiter_fixture.cpp"
        fixture.write_text(source)
        spec = importlib.util.spec_from_file_location("pacer_host_environment", ROOT / "tests/host_environment.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        environment, linker = module.environment("x64")
        compiler = linker.parent / "cl.exe"
        command = [str(compiler), "/nologo", "/O2", "/EHsc", "/std:c++17", "/W4", "/WX",
                   str(fixture), "/Fo" + str(OUT / "limiter.obj"), "/Fe" + str(OUT / "limiter.exe")]
        subprocess.run(command, env=environment, cwd=ROOT, check=True, capture_output=True)
        result = subprocess.run([str(OUT / "limiter.exe")], check=True, capture_output=True)
        (OUT / "limiter.log").write_bytes(result.stdout + result.stderr)
        (OUT / "receipt.json").write_text(json.dumps({
            "status": "PASS_PINNED_LIMITER_EXECUTION_AND_REAL_ANDROID_DSO_EDGES",
            "source_sha256": hashes, "library_sha256": sha(library), "linked_edges": edges,
            "fixture_sha256": sha(fixture), "compiler_sha256": sha(compiler),
            "platform_boundaries": ["deterministic clock", "Sleep::sleepUntil", "environment getter", "mutex/log adapter"],
            "device_executed": False,
            "limitation": "This proves limiter logic and real linked reach, not physical Android frame cadence or a performance gain."
        }, indent=2) + "\n")
        print(result.stdout.decode().strip())


if __name__ == "__main__":
    unittest.main()
