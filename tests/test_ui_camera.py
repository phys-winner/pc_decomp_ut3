"""HOST execute the production camera wrapper with complete core registers."""
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/ui-camera-tests'


class CameraTest(unittest.TestCase):
    def test_trusted_producer_callback(self):
        OUT.mkdir(parents=True, exist_ok=True)
        source = OUT / 'adpf_fixture.c'
        source.write_text(r'''
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <errno.h>
static volatile LONG camera_frame_callbacks;
void void_ui_camera_frame_begin(void) { InterlockedIncrement(&camera_frame_callbacks);errno=ERANGE; }
#define main adpf_existing_main
#include "../adpf-candidate/test_adpf.c"
#undef main
int main(void) {
    ut3_adpf_set_target(120);
    int result=adpf_existing_main();
    assert(camera_frame_callbacks>0);
    puts("PASS actual producer callback: optional camera cleanup called; ADPF errno/cancellation/thread teardown unchanged");
    return result;
}
''')
        spec = importlib.util.spec_from_file_location('camera_adpf_environment', ROOT / 'tests/host_environment.py')
        module = importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        env, linker = module.environment('x64')
        commands = [[str(module.CLANG), '/nologo', '/O2', '/std:c11', '/D_CRT_SECURE_NO_WARNINGS',
                     '/clang:--target=x86_64-pc-windows-msvc', '/I' + str(ROOT / 'build/adpf-candidate'),
                     '/c', str(source), '/Fo' + str(OUT / 'adpf.obj')],
                    [str(linker), '/nologo', str(OUT / 'adpf.obj'), '/OUT:' + str(OUT / 'adpf.exe')],
                    [str(OUT / 'adpf.exe')]]
        output=[]
        for command in commands:
            result=subprocess.run(command,cwd=ROOT,env=env,capture_output=True,timeout=30)
            output.append(result.stdout+result.stderr)
            self.assertEqual(result.returncode,0,output[-1].decode(errors='replace'))
        (OUT / 'adpf.log').write_bytes(b''.join(output))
        print(output[-1].decode().strip())

    def test_production_wrapper(self):
        OUT.mkdir(parents=True, exist_ok=True)
        runtime = (ROOT / 'android/runtime/recomp_types.h').read_text()
        host = (ROOT / 'android/host/host.h').read_text()
        core = (ROOT / 'android/host/core.c').read_text()
        regs = host[host.index('typedef struct void_regs {'):host.index('} void_regs;') + len('} void_regs;')]
        end = runtime.index('} xmm_t;') + len('} xmm_t;')
        xmm = runtime[runtime.rindex('typedef union {', 0, end):end]
        definitions = core[core.index('VOID_HIDDEN uint32_t g_eax'):core.index('/* --------------------------------------------------------------- logging */')]
        platform = r'''
#include "touch_host_win.h"
#include <stddef.h>
#include <stdlib.h>
#include <math.h>
#include <errno.h>
#define VOID_HIDDEN
#define RECOMP_X87_WINDOW
#define RECOMP_X87_SLOTS 256
#define ICALL_TRACE_SIZE 128
typedef struct recomp_ehframe recomp_ehframe_t;
''' + xmm + '\n' + regs + r'''
recomp_ehframe_t *g_eh_frames;
uint32_t g_eh_obj,g_eh_info;
#define g_st (*(double (*)[8])(g_st_win+g_fp_sp))
#define g_st_i64 (*(int64_t (*)[8])(g_st_i64_win+g_fp_sp))
''' + definitions
        (OUT / 'ui_camera_test_platform.h').write_text(platform)
        spec = importlib.util.spec_from_file_location('camera_host_environment', ROOT / 'tests/host_environment.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        env, linker = module.environment('x64')
        commands = [[str(module.CLANG), '/nologo', '/O2', '/W4', '/std:c11', '/D_CRT_SECURE_NO_WARNINGS',
                     '/clang:--target=x86_64-pc-windows-msvc', '/I' + str(OUT), '/I' + str(ROOT / 'tests'),
                     '/c', str(ROOT / 'tests/test_ui_camera.c'), '/Fo' + str(OUT / 'test.obj')],
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
        report = {'status': 'PASS_HOST_PRODUCTION_CAMERA_WRAPPER_CORE_REGISTERS', 'commands': commands,
                  'sources': {p: sha(ROOT / p) for p in ['android/host/ui_camera.c', 'tests/test_ui_camera.c',
                            'tests/test_ui_camera.py', 'android/host/core.c', 'android/host/host.h']},
                  'log_sha256': sha(OUT / 'host.log'), 'device_executed': False,
                  'scope': 'Actual wrapper with original core register save; constructor callback mocked. Separate original PE oracle executes real caller and complete constructor.'}
        (OUT / 'receipt.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    unittest.main()
