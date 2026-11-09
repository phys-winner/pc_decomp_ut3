"""Exact entry inventory/transform tests and a real multithreaded atomic fixture.

No Android device access. --build-arm64 additionally compiles the same fixture
and the complete monitor runtime for the device owner to run separately.
"""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import function_coverage as fc


def lifted(va):
    return f"void sub_{va}(void) {{\n RECOMP_ENTER(0x{va}u);\n sub_{va}();\n}}\n"


class CoverageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.generated = self.root / "generated"
        (self.generated / "ut3").mkdir(parents=True)
        (self.generated / "physx").mkdir()
        self.source = self.generated / "ut3/functions_0000.c"
        self.source.write_text('#include "functions.h"\n' + lifted("00400000") + lifted("00400010"))
        self.physics = self.generated / "physx/functions_0000.c"
        self.physics.write_text('#include "functions.h"\n' + lifted("1E000000"))
        self.host = self.root / "host.c"
        self.host.write_text("void __wrap_sub_00400000(void) { mode(); }\n"
            "#define WRAP(address, kind) \\\n"
            " void __wrap_sub_##address(void) { mode(); }\n"
            "WRAP(00400010, 1)\n")
        self.plan = fc.CoveragePlan.create(self.generated, [self.source, self.physics], [self.host],
            ["sub_00400000", "sub_00400010"], self.root / "coverage",
            [{"name": "ut3", "file": "UT3.exe"}, {"name": "physx", "file": "PhysXCore.dll"}])

    def bitmap(self, cumulative, window):
        count = len(self.plan.manifest["entries"])
        data = fc.HEADER.pack(fc.MAGIC, 1, count, 1, 0, 1, 2, 3, 100, 101, 120000, 60000,
                              self.plan.manifest["identity_sha256"].encode())
        path = self.root / "capture.bin"
        path.write_bytes(data + struct.pack("<QQ", cumulative, window))
        return path

    def test_all_entries_direct_calls_and_separate_implementations(self):
        self.assertEqual(len(self.plan.manifest["entries"]), 5)
        text, count = self.plan.instrument_generated(self.source.read_text(), "ut3")
        self.assertEqual(count, 2)
        self.assertEqual(text.count("VOID_FUNCTION_COVERAGE_HIT("), 2)
        self.assertIn('#include "ut3/functions.h"', text)
        self.assertIn("sub_00400000();", text)  # Same-unit direct call still enters its instrumented body.
        text, count = self.plan.instrument_host(self.host.read_text())
        self.assertEqual(count, 2)
        self.assertIn("VOID_FUNCTION_COVERAGE_HIT(VOID_FC_WRAPPER_##address);", text)
        self.assertLess(text.index("VOID_FUNCTION_COVERAGE_HIT("), text.index("mode();"))

    def test_generator_drift_fails_closed(self):
        for text in (lifted("00400000").replace("RECOMP_ENTER", "OTHER_ENTER"),
                     lifted("00400000").replace("0x00400000u", "0x00400010u"),
                     lifted("00400000") + "RECOMP_ENTER(0x00400000u);"):
            with self.assertRaises(ValueError):
                fc.generated_entries(text)

    def test_missing_wrapper_fails_closed(self):
        with self.assertRaisesRegex(ValueError, "Uninstrumented wrappers"):
            fc.CoveragePlan.create(self.generated, [self.source, self.physics], [], ["sub_00400000"],
                                  self.root / "bad", [{"name": "ut3", "file": "UT3.exe"}, {"name": "physx", "file": "PhysXCore.dll"}])

    def test_explicit_dispatch_overrides_and_unknown_addresses(self):
        text = "#define ADDR 0x00400000u\nenum { ENUM_ADDR=0x00400010u };\n"
        text += "static void first(void) { mode(); }\nstatic void second(void) { mode(); }\n"
        text += "void_dispatch_add(ADDR,first); void_dispatch_add(ENUM_ADDR,second);\n"
        text += "void_dispatch_add(ADDR, saved[0]); void_dispatch_add(old[i].va,old[i].fn);\n"
        self.assertEqual(fc.override_entries(text), [("first", "00400000"), ("second", "00400010")])
        self.host.write_text(text)
        plan = fc.CoveragePlan.create(self.generated, [self.source], [self.host], [], self.root / "overrides",
                                     [{"name": "ut3", "file": "UT3.exe"}])
        transformed, count = plan.instrument_host(text)
        self.assertEqual(count, 2)
        self.assertEqual(transformed.count("VOID_FUNCTION_COVERAGE_HIT("), 2)
        for invalid in (text.replace("ADDR,first", "UNKNOWN,first"), text + "void_dispatch_add(0x00400010u,first);",
                        text + "void_dispatch_add(0x00400000u,unknown);"):
            with self.assertRaises(ValueError):
                fc.override_entries(invalid)

    def test_decode_union_and_separate_banks(self):
        result = fc.decode(self.plan.manifest_path, self.bitmap(31, 30))
        self.assertEqual(result["banks"]["cumulative"]["unique_guest_function_count"], 3)
        self.assertEqual(result["banks"]["window"]["unique_guest_function_count"], 2)
        self.assertEqual(result["banks"]["window"]["entered_implementation_count"], 4)
        self.assertEqual(result["actual_window_seconds_bounds"], [97e-9, 99e-9])

    def test_decode_rejects_wrong_build_padding_and_partial_write(self):
        path = self.bitmap(31, 30)
        data = path.read_bytes()
        for invalid in (data[:-1], data + b"x", data[:80] + b"x" + data[81:],
                        data[:-16] + struct.pack("<QQ", 63, 0),
                        data[:-16] + struct.pack("<QQ", 1, 2)):
            path.write_bytes(invalid)
            with self.assertRaises(ValueError):
                fc.decode(self.plan.manifest_path, path)


NATIVE = r'''
#include "function_coverage_runtime.h"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
extern int void_function_coverage_parse_request(const char *,size_t,uint64_t *,uint64_t *);
#ifdef _WIN32
#include <windows.h>
typedef HANDLE thread_t;
#define THREAD_RET DWORD WINAPI
#define THREAD_DONE return 0
#define YIELD() SwitchToThread()
static thread_t spawn(LPTHREAD_START_ROUTINE fn, void *arg) { HANDLE h=CreateThread(NULL,0,fn,arg,0,NULL); assert(h); return h; }
static void join(thread_t t) { assert(WaitForSingleObject(t,INFINITE)==WAIT_OBJECT_0); CloseHandle(t); }
#else
#include <pthread.h>
#include <sched.h>
typedef pthread_t thread_t;
#define THREAD_RET void *
#define THREAD_DONE return NULL
#define YIELD() sched_yield()
static thread_t spawn(void *(*fn)(void *), void *arg) { pthread_t t; assert(!pthread_create(&t,NULL,fn,arg)); return t; }
static void join(thread_t t) { assert(!pthread_join(t,NULL)); }
#endif
static _Atomic unsigned phase, done, admitted, release_writer, stopped;
void void_fc_test_admitted(uint32_t word, uint64_t mask) {
    if(word==64 && mask==1) {
        atomic_store(&admitted,1);
        while(!atomic_load(&release_writer)) YIELD();
    }
}
static THREAD_RET worker(void *argument) {
    unsigned base=(unsigned)(uintptr_t)argument*256;
    while(atomic_load(&phase)<1) YIELD();
    for(unsigned i=0;i<128;++i) VOID_FUNCTION_COVERAGE_HIT(base+i);
    for(unsigned i=0;i<10000;++i) VOID_FUNCTION_COVERAGE_HIT(4095);
    atomic_fetch_add(&done,1);
    while(atomic_load(&phase)<2) YIELD();
    for(unsigned i=64;i<192;++i) VOID_FUNCTION_COVERAGE_HIT(base+i);
    for(unsigned i=0;i<10000;++i) VOID_FUNCTION_COVERAGE_HIT(4095);
    atomic_fetch_add(&done,1);
    THREAD_DONE;
}
static THREAD_RET held_writer(void *argument) { (void)argument; VOID_FUNCTION_COVERAGE_HIT(4096); THREAD_DONE; }
static THREAD_RET stopper(void *argument) { (void)argument; void_function_coverage_stop(); atomic_store(&stopped,1); THREAD_DONE; }
static void test_request_parser(void) {
    const struct { const char *text; int valid; uint64_t warmup,duration; } cases[] = {
        {"120000 60000\n",1,120000,60000}, {"0 1",1,0,1}, {"3600000 600000",1,3600000,600000},
        {" \t00001\r\n00002 \t",1,1,2}, {"",0,0,0}, {" \n",0,0,0}, {"0",0,0,0},
        {"0 0",0,0,0}, {"3600001 1",0,0,0}, {"0 600001",0,0,0},
        {"-1 60000",0,0,0}, {"-18446744073709551615 60000",0,0,0},
        {"0 -18446744073709551615",0,0,0}, {"+0 1",0,0,0}, {"0 +1",0,0,0},
        {"0+1",0,0,0}, {"18446744073709551616 1",0,0,0}, {"0 18446744073709551616",0,0,0},
        {"999999999999999999999999999999999999 1",0,0,0}, {"0 1 trailing",0,0,0},
        {"0 1 2",0,0,0}, {"0 1x",0,0,0}, {"0 0x10",0,0,0}, {"1.0 2",0,0,0}
    };
    for(unsigned i=0;i<sizeof cases/sizeof cases[0];++i) {
        uint64_t warmup=111,duration=222;
        const int valid=void_function_coverage_parse_request(cases[i].text,strlen(cases[i].text),&warmup,&duration);
        assert(valid==cases[i].valid);
        assert(warmup==(valid?cases[i].warmup:111) && duration==(valid?cases[i].duration:222));
    }
    const char embedded[]="0 1\0trailing";
    uint64_t warmup=111,duration=222;
    assert(!void_function_coverage_parse_request(embedded,sizeof embedded-1,&warmup,&duration));
    assert(warmup==111 && duration==222);
    puts("PASS coverage request parser: 25 cases, signs/overflow/trailing/NUL rejected, zero/min/max bounds checked");
}
int main(int argc,char **argv) {
    assert(argc==2);
    test_request_parser();
    assert(!void_function_coverage_export(argv[1],1,2,3,4,5,0,1));
    thread_t workers[8];
    for(unsigned i=0;i<8;++i) workers[i]=spawn(worker,(void *)(uintptr_t)i);
    atomic_store(&phase,1);
    while(atomic_load(&done)<8) YIELD();
    assert(void_function_coverage_arm());
    assert(!void_function_coverage_arm());
    atomic_store(&phase,2);
    while(atomic_load(&done)<16) YIELD();
    for(unsigned i=0;i<8;++i) join(workers[i]);
    thread_t held=spawn(held_writer,NULL);
    while(!atomic_load(&admitted)) YIELD();
    thread_t closing=spawn(stopper,NULL);
    while(atomic_load(&void_fc_state)&VOID_FC_FLAGS) YIELD();
    assert(!atomic_load(&stopped));
    VOID_FUNCTION_COVERAGE_HIT(4097);
    atomic_store(&release_writer,1);
    join(held); join(closing);
    assert(atomic_load(&void_fc_state)==0);
    for(unsigned i=0;i<VOID_FC_COUNT;++i) {
        const int special=(i==4095 || i==4096);
        const int expected_c=special || (i<2048 && i%256<192);
        const int expected_w=special || (i<2048 && i%256>=64 && i%256<192);
        const int c=(atomic_load(&void_fc_cumulative[i/64])>>(i%64))&1;
        const int w=(atomic_load(&void_fc_window[i/64])>>(i%64))&1;
        assert(c==expected_c && w==expected_w);
    }
    for(unsigned i=0;i<VOID_FC_COUNT;++i) VOID_FUNCTION_COVERAGE_HIT(i);
    assert(!((atomic_load(&void_fc_cumulative[64])>>1)&1));
    assert(void_function_coverage_export(argv[1],1,2,3,100,101,120000,60000));
    puts("PASS exact function coverage: 8 threads, 160000 repeated entries, 1538 cumulative IDs, 1026 window IDs, held-writer close race");
    return 0;
}
'''


def native_fixture(build_arm64):
    out = ROOT / "build/visibility-native-2026-11-07/function-coverage-tests"
    out.mkdir(parents=True, exist_ok=True)
    entries = [{"id": i, "module": "fixture", "image": "fixture.exe", "address": f"{0x400000+i*16:08X}",
                "symbol": f"sub_{0x400000+i*16:08X}", "kind": "generated", "source": "fixture.c"} for i in range(4099)]
    manifest = {"version": 1, "scope": "fixture", "excluded": "fixture", "meaning": "fixture", "entries": entries}
    identity = fc.digest_bytes(fc.canonical(manifest))
    manifest["identity_sha256"] = identity
    (out / "manifest.json").write_text(json.dumps(manifest))
    (out / "function_coverage_ids.h").write_text(f'#define VOID_FC_COUNT 4099u\n#define VOID_FC_WORDS 65u\n#define VOID_FC_IDENTITY "{identity}"\n')
    source = out / "fixture.c"
    source.write_text(NATIVE)
    if os.name == "nt":
        from host_environment import CLANG, environment
        env, linker = environment("x64")
        # NDK's installed stdatomic.h assumes Bionic sys/cdefs.h even for a
        # Windows target. Map the standard operations to the same Clang C11
        # intrinsics in a test-private header; production uses Android's header.
        adapter = out / "host-include"
        adapter.mkdir(exist_ok=True)
        (adapter / "stdatomic.h").write_text('''#pragma once
enum { memory_order_relaxed=__ATOMIC_RELAXED, memory_order_acquire=__ATOMIC_ACQUIRE,
 memory_order_release=__ATOMIC_RELEASE, memory_order_acq_rel=__ATOMIC_ACQ_REL,
 memory_order_seq_cst=__ATOMIC_SEQ_CST };
#define atomic_load_explicit __c11_atomic_load
#define atomic_store_explicit __c11_atomic_store
#define atomic_fetch_add_explicit __c11_atomic_fetch_add
#define atomic_fetch_sub_explicit __c11_atomic_fetch_sub
#define atomic_fetch_or_explicit __c11_atomic_fetch_or
#define atomic_fetch_and_explicit __c11_atomic_fetch_and
#define atomic_compare_exchange_weak_explicit __c11_atomic_compare_exchange_weak
#define atomic_compare_exchange_strong_explicit __c11_atomic_compare_exchange_strong
#define atomic_load(p) atomic_load_explicit(p,memory_order_seq_cst)
#define atomic_store(p,v) atomic_store_explicit(p,v,memory_order_seq_cst)
#define atomic_fetch_add(p,v) atomic_fetch_add_explicit(p,v,memory_order_seq_cst)
#define atomic_compare_exchange_strong(p,e,v) atomic_compare_exchange_strong_explicit(p,e,v,memory_order_seq_cst,memory_order_seq_cst)
''')
        binary = out / "fixture.exe"
        command = [str(CLANG), "/nologo", "/O2", "/clang:-std=c11", "/clang:-Werror", "/D_CRT_SECURE_NO_WARNINGS", "/DVOID_FC_NO_MONITOR", "/DVOID_FC_TEST_HOOK",
                   "/I" + str(adapter), "/I" + str(ROOT / "tools"), "/I" + str(out), str(source), str(ROOT / "tools/function_coverage_runtime.c"),
                   "/c"]
    else:
        env = os.environ.copy()
        binary = out / "fixture"
        command = ["clang", "-std=c11", "-O2", "-Werror", "-DVOID_FC_NO_MONITOR", "-DVOID_FC_TEST_HOOK", "-I" + str(ROOT / "tools"),
                   "-I" + str(out), str(source), str(ROOT / "tools/function_coverage_runtime.c"), "-pthread", "-o", str(binary)]
    built = subprocess.run(command, cwd=out, env=env, text=True, capture_output=True)
    (out / "host-build.log").write_text(built.stdout + built.stderr)
    assert built.returncode == 0, built.stdout + built.stderr
    if os.name == "nt":
        linked = subprocess.run([str(linker), "/nologo", str(out / "fixture.obj"), str(out / "function_coverage_runtime.obj"),
                                 "/OUT:" + str(binary)], cwd=out, env=env, text=True, capture_output=True)
        assert linked.returncode == 0, linked.stdout + linked.stderr
    # Windows rename() does not replace an existing destination. Give each host
    # fixture a fresh path; production Android still publishes with atomic rename.
    with tempfile.TemporaryDirectory(prefix="capture-", dir=out) as temporary:
        capture = Path(temporary) / "capture.bin"
        ran = subprocess.run([str(binary), str(capture)], capture_output=True, text=True, timeout=30)
        (out / "host-result.txt").write_text(ran.stdout + ran.stderr)
        assert ran.returncode == 0 and "PASS exact function coverage:" in ran.stdout and "PASS coverage request parser:" in ran.stdout, ran.stdout + ran.stderr
        decoded = fc.decode(out / "manifest.json", capture)
        (out / "capture.bin").write_bytes(capture.read_bytes())
    assert decoded["banks"]["cumulative"]["unique_guest_function_count"] == 1538
    assert decoded["banks"]["window"]["unique_guest_function_count"] == 1026
    print(ran.stdout, end="")
    if build_arm64:
        sdk = Path(os.environ.get("ANDROID_SDK_ROOT", Path.home() / "AppData/Local/Android/Sdk"))
        ndk = Path(os.environ.get("ANDROID_NDK_HOME", sdk / "ndk/27.2.12479018"))
        clang = ndk / "toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe"
        common = [str(clang), "--target=aarch64-linux-android29", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                  "-I" + str(ROOT / "tools"), "-I" + str(out)]
        arm = subprocess.run([*common, "-DVOID_FC_NO_MONITOR", "-DVOID_FC_TEST_HOOK", str(source),
                              str(ROOT / "tools/function_coverage_runtime.c"), "-o", str(out / "fixture-arm64")], capture_output=True, text=True)
        monitor = subprocess.run([*common, "-c", str(ROOT / "tools/function_coverage_runtime.c"), "-o", str(out / "monitor-arm64.o")], capture_output=True, text=True)
        (out / "arm64-build.log").write_text(arm.stdout + arm.stderr + monitor.stdout + monitor.stderr)
        assert arm.returncode == monitor.returncode == 0, arm.stderr + monitor.stderr
        print("ARM64 fixture and full POSIX monitor compiled:", out / "fixture-arm64")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-arm64", action="store_true")
    args = parser.parse_args()
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(CoverageTests))
    if not result.wasSuccessful():
        raise SystemExit(1)
    native_fixture(args.build_arm64)
