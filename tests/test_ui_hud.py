"""HOST execute actual HUD helper and original core register/dispatch code."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/ui-hud-tests'


class HudScaleTest(unittest.TestCase):
    def test_original_native_crosshair_and_argument_boundaries(self):
        result = subprocess.run([os.sys.executable, str(ROOT / 'tests/hud_crosshair_original.py')],
                                cwd=ROOT, capture_output=True, timeout=30)
        self.assertEqual(result.returncode, 0, (result.stdout + result.stderr).decode(errors='replace'))
        original = json.loads((ROOT / 'build/crosshair-aspect-probe/receipt.json').read_text())
        self.assertEqual(original['cases'], 144)
        self.assertEqual(len(original['physical_native_draw_cases']), 32)
        self.assertEqual(original['actual_weapon_caller_operand']['argument_kind'], 'Core.ObjectProperty')
        self.assertEqual(original['actual_metadata_decoder_cases']['bool_mask_offset'], '0x84')
        source = (ROOT / 'android/host/ui_hud.c').read_text()
        address = re.search(r'WEAPON_CLASS_GLOBAL=(0x[0-9A-Fa-f]+)u', source)
        self.assertIsNotNone(address)
        self.assertEqual(int(address.group(1),16),int(original['weapon_class_registration']['global'],16))
        self.assertIn('hud_member(field,0x84,&mask)', source)
        self.assertIn('hud_member(frame,0x10,&locals)', source)
        print(result.stdout.decode().strip())

    def test_original_native_draw_and_stock_hud_coverage(self):
        result = subprocess.run([os.sys.executable, str(ROOT / 'tests/hud_draw_original.py')],
                                cwd=ROOT, capture_output=True, timeout=30)
        self.assertEqual(result.returncode, 0, (result.stdout + result.stderr).decode(errors='replace'))
        original = json.loads((ROOT / 'build/hud-aspect-probe/original-draw-receipt.json').read_text())
        self.assertEqual(len(original['native_glow_text_cases']), 12)
        self.assertEqual(len(original['native_weapon_box_cases']), 3)
        inventory = json.loads((ROOT / 'research/hud-class-inventory.json').read_text())
        self.assertEqual(inventory['hierarchy_coverage']['classes_examined'], 2310)
        self.assertEqual(len(inventory['hierarchy_coverage']['hud_ancestry_matches']), 13)
        self.assertEqual(len(inventory['hierarchy_coverage']['packages']), 7)
        for entry in inventory['classes']:
            source = ROOT / entry['source']
            self.assertEqual(hashlib.sha256(source.read_bytes()).hexdigest(), entry['sha256'])
        hud = (ROOT / 'research/native-settings-source/UTHUD.uc').read_text()
        team = (ROOT / 'research/native-settings-source/UTTeamHUD.uc').read_text()
        for original_formula in ('HUDScaleX = Canvas.ClipX/1280;', 'HUDScaleY = Canvas.ClipX/1280;',
                                 'ResolutionScaleX = Canvas.ClipX/1024;', 'ResolutionScale = Canvas.ClipY/768;'):
            self.assertTrue(re.sub(r'\s+', '', original_formula) in re.sub(r'\s+', '', hud), original_formula)
        self.assertIn('W = 214 * ResolutionScaleX;', team)
        self.assertIn('H = 87 * ResolutionScale;', team)
        print(result.stdout.decode().strip())
        print('PASS source-pinned original HUD4:3 authored ratio and complete7package/2310class/13HUD ancestry inventory')

    def test_original_pe_class_registration_and_cast(self):
        result = subprocess.run([os.sys.executable, str(ROOT / 'build/hud-scale-probe/class_global_oracle.py')],
                                cwd=ROOT, capture_output=True, timeout=30)
        self.assertEqual(result.returncode, 0, (result.stdout + result.stderr).decode(errors='replace'))
        self.assertIn(b'PASS original PE', result.stdout)
        original = json.loads((ROOT / 'build/hud-scale-probe/class-global-oracle-receipt.json').read_text())
        production = (ROOT / 'android/host/ui_hud.c').read_text()
        import re
        address = re.search(r'HUD_CLASS_GLOBAL=(0x[0-9A-Fa-f]+)u', production)
        self.assertIsNotNone(address)
        self.assertEqual(int(address.group(1), 16), int(original['correct_uthud_global'], 16))
        print(result.stdout.decode().strip())

    def test_existing_adpf_with_frame_callback(self):
        OUT.mkdir(parents=True, exist_ok=True)
        source = OUT / 'adpf_fixture.c'
        source.write_text(r'''
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <errno.h>
static volatile LONG hud_frame_callbacks;
void void_ui_hud_frame_begin(void) { InterlockedIncrement(&hud_frame_callbacks);errno=ERANGE; }
#define main adpf_existing_main
#include "../adpf-candidate/test_adpf.c"
#undef main
int main(void) {
    /* Existing oracle deliberately checks120Hz; production default is60. */
    ut3_adpf_set_target(120);
    int result=adpf_existing_main();
    assert(hud_frame_callbacks>0);
    puts("PASS actual producer callback: optional HUD cleanup called; original ADPF120Hz oracle/errno/cancellation/thread teardown unchanged");
    return result;
}
''')
        path = ROOT / 'tests/host_environment.py'
        spec = importlib.util.spec_from_file_location('hud_adpf_host_environment', path)
        module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
        env, linker = module.environment('x64')
        commands = [[str(module.CLANG), '/nologo', '/O2', '/std:c11', '/D_CRT_SECURE_NO_WARNINGS',
                     '/clang:--target=x86_64-pc-windows-msvc', '/I' + str(ROOT / 'build/adpf-candidate'),
                     '/c', str(source), '/Fo' + str(OUT / 'adpf.obj')],
                    [str(linker), '/nologo', str(OUT / 'adpf.obj'), '/OUT:' + str(OUT / 'adpf.exe')],
                    [str(OUT / 'adpf.exe')]]
        for command in commands:
            result = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, timeout=30)
            self.assertEqual(result.returncode, 0, (result.stdout + result.stderr).decode(errors='replace'))
        (OUT / 'adpf.log').write_bytes(result.stdout + result.stderr)
        print(result.stdout.decode().strip())

    def test_typed_settings_observer(self):
        OUT.mkdir(parents=True, exist_ok=True)
        path = ROOT / 'tests/host_environment.py'
        spec = importlib.util.spec_from_file_location('hud_observer_host_environment', path)
        module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
        env, linker = module.environment('x64')
        java = Path(os.environ.get('JAVA_HOME', 'C:/Portable/jdk-24.0.1'))
        commands = [[str(module.CLANG), '/nologo', '/O2', '/std:c11', '/D_CRT_SECURE_NO_WARNINGS',
                     '/clang:--target=x86_64-pc-windows-msvc', '/I' + str(java / 'include'),
                     '/I' + str(java / 'include/win32'), '/c', str(ROOT / 'tests/test_touch_game.c'),
                     '/Fo' + str(OUT / 'observer.obj')],
                    [str(linker), '/nologo', str(OUT / 'observer.obj'), '/OUT:' + str(OUT / 'observer.exe')],
                    [str(OUT / 'observer.exe')]]
        for command in commands:
            result = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, timeout=30)
            self.assertEqual(result.returncode, 0, (result.stdout + result.stderr).decode(errors='replace'))
        (OUT / 'observer.log').write_bytes(result.stdout + result.stderr)
        self.assertIn(b'PASS HUD settings observer', result.stdout)
        print(result.stdout.decode().strip())

    def test_production_scope_and_dispatch(self):
        OUT.mkdir(parents=True, exist_ok=True)
        core = (ROOT / 'android/host/core.c').read_text()
        host = (ROOT / 'android/host/host.h').read_text()
        runtime = (ROOT / 'android/runtime/recomp_types.h').read_text()
        regs = host[host.index('typedef struct void_regs {'):host.index('} void_regs;') + len('} void_regs;')]
        end = runtime.index('} xmm_t;') + len('} xmm_t;')
        xmm = runtime[runtime.rindex('typedef union {', 0, end):end]
        definitions = core[core.index('VOID_HIDDEN uint32_t g_eax'):core.index('/* --------------------------------------------------------------- logging */')]
        dispatch = core[core.index('typedef struct { uint32_t va;'):core.index('void void_dispatch_image(')]
        resolve = core[core.index('static __attribute__((noinline)) recomp_func_t resolve_slow'):core.index('/* ---------------------------------------------------------------- traps */')]
        platform = r'''
#ifdef _WIN32
#include "touch_host_win.h"
#else
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#define VOID_HOST_H
#define VOID_GUEST_BASE 0x2000000000ull
#define ADDR(address) ((uintptr_t)(uint32_t)(address)+(uintptr_t)VOID_GUEST_BASE)
#define GPTR(address) ((void *)ADDR(address))
#define MEM32(address) (*(uint32_t *)ADDR(address))
#define MEM_RESERVE 0
#define MEM_COMMIT 0
#define PAGE_READWRITE 0
static void *VirtualAlloc(void *address,size_t bytes,int flags,int protection) {
    (void)flags;(void)protection;
    return mmap(address,bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0);
}
#endif
#include <stddef.h>
#include <stdlib.h>
#include <math.h>
#include <errno.h>
#define VOID_HIDDEN
#define RECOMP_X87_WINDOW
#define RECOMP_X87_SLOTS 256
#define RECOMP_RETADDR 0xDEAD0000u
#define ICALL_TRACE_SIZE 128
#define GUEST_THUNK_BASE 0x7FF00000u
#define MAX_THUNKS 16
typedef void (*void_fn)(void);
typedef void (*recomp_func_t)(void);
typedef struct recomp_ehframe recomp_ehframe_t;
''' + xmm + '\n' + regs + r'''
void void_regs_save(void_regs *);
void void_regs_load(const void_regs *);
void void_dispatch_add(uint32_t,void_fn);
void_fn void_dispatch_find(uint32_t);
recomp_func_t void_resolve(uint32_t);
int void_machine_is_owned(void);
void void_log(const char *format,...);
int void_vm_readable(uint32_t address,uint32_t size);
int void_vm_writable(uint32_t address,uint32_t size);
static _Noreturn void void_fatal(const char *text,...) { (void)text;abort(); }
static void void_lift_trap(uint32_t va,const char *text) { (void)va;(void)text;abort(); }
static void void_module_prepare_call(uint32_t va) { (void)va; }
static int in_image(uint32_t va) { (void)va;return 0; }
static int count_thunks,trace_n;
static uint32_t *thunk_calls,thunk_count;
static void_fn trace_wrap(uint32_t va,void_fn fn) { (void)va;return fn; }
'''
        (OUT / 'ui_hud_test_platform.h').write_text(platform)
        (OUT / 'ui_hud_core.inc').write_text('recomp_ehframe_t *g_eh_frames;\nuint32_t g_eh_obj,g_eh_info;\n#define g_st (*(double (*)[8])(g_st_win+g_fp_sp))\n#define g_st_i64 (*(int64_t (*)[8])(g_st_i64_win+g_fp_sp))\n' + definitions + dispatch + resolve)
        path = ROOT / 'tests/host_environment.py'
        spec = importlib.util.spec_from_file_location('hud_test_host_environment', path)
        module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
        env, linker = module.environment('x64')
        commands = [[str(module.CLANG), '/nologo', '/O2', '/W4', '/std:c11', '/D_CRT_SECURE_NO_WARNINGS',
                     '/clang:--target=x86_64-pc-windows-msvc', '/I' + str(OUT), '/I' + str(ROOT / 'tests'),
                     '/c', str(ROOT / 'tests/test_ui_hud.c'), '/Fo' + str(OUT / 'test.obj')],
                    [str(linker), '/nologo', str(OUT / 'test.obj'), '/OUT:' + str(OUT / 'test.exe')],
                    [str(OUT / 'test.exe')]]
        output = []
        for command in commands:
            result = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, timeout=30)
            output.append(result.stdout + result.stderr)
            self.assertEqual(result.returncode, 0, output[-1].decode(errors='replace'))
        (OUT / 'host.log').write_bytes(b''.join(output))
        print(output[-1].decode().strip())
        sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
        report = {'status': 'PASS_HOST_ACTUAL_HUD_SCOPE_AND_CORE_DISPATCH_REGISTERS',
                  'commands': commands, 'helper_sha256': sha(ROOT / 'android/host/ui_hud.c'),
                  'fixture_sha256': sha(ROOT / 'tests/test_ui_hud.c'),
                  'crosshair_fixture_sha256': sha(ROOT / 'tests/test_ui_crosshair.inc'),
                  'core_source_sha256': sha(ROOT / 'android/host/core.c'),
                  'host_header_sha256': sha(ROOT / 'android/host/host.h'),
                  'actual_extracted_core_sha256': sha(OUT / 'ui_hud_core.inc'),
                  'platform_sha256': sha(OUT / 'ui_hud_test_platform.h'),
                  'log_sha256': sha(OUT / 'host.log'),
                  'device_executed': False,
                  'scope': 'Actual production helper, core register save/load, dispatch and resolve cache; guest Push/Pop and script bodies mocked. Independent original x86 oracle covers real matrix/VM APIs.'}
        (OUT / 'receipt.json').write_text(json.dumps(report, indent=2) + '\n')
        clang = module.CLANG.with_name('clang.exe')
        arm_commands = [[str(clang), '--target=aarch64-linux-android29', '-O2', '-std=gnu11',
                         '-fno-strict-aliasing', '-fwrapv', '-ffp-contract=off', '-Wall', '-Werror',
                         '-Wno-unused-function', '-Wno-unused-variable', '-I' + str(OUT), '-I' + str(ROOT / 'tests'),
                         str(ROOT / 'tests/test_ui_hud.c'), '-o', str(OUT / 'test-ui-hud-arm64'), '-lm'],
                        [str(clang), '--target=aarch64-linux-android29', '-O2', '-std=gnu11',
                         '-fno-strict-aliasing', '-fwrapv', '-ffp-contract=off', '-fno-math-errno',
                         '-DRECOMP_X87_WINDOW', '-DUT3_ANDROID_APP', '-Wall', '-Werror',
                         '-Iandroid/runtime', '-Iandroid/host', '-Igenerated', '-c',
                         'android/host/ui_hud.c', '-o', str(OUT / 'ui_hud.c.o')]]
        for command in arm_commands:
            result = subprocess.run(command, cwd=ROOT, capture_output=True, timeout=30)
            self.assertEqual(result.returncode, 0, (result.stdout + result.stderr).decode(errors='replace'))
        (OUT / 'arm64-build-receipt.json').write_text(json.dumps({
            'status': 'PASS_ANDROID_ARM64_CROSSCOMPILE_NOT_EXECUTED', 'commands': arm_commands,
            'helper_sha256': sha(ROOT / 'android/host/ui_hud.c'),
            'fixture_sha256': sha(ROOT / 'tests/test_ui_hud.c'),
            'crosshair_fixture_sha256': sha(ROOT / 'tests/test_ui_crosshair.inc'),
            'arm64_fixture_sha256': sha(OUT / 'test-ui-hud-arm64'),
            'arm64_production_object_sha256': sha(OUT / 'ui_hud.c.o')}, indent=2)+'\n')
        print('PASS strict Android ARM64 production helper and standalone actual-helper/core fixture crosscompile (not device executed)')


if __name__ == '__main__':
    unittest.main()
