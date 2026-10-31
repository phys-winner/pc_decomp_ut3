"""Run actual input producers and exact DI reader on HOST, cross-compile ARM64; no adb."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/dinput-overflow-test'


class DInputOverflowTest(unittest.TestCase):
    def test_real_producers_and_reader(self):
        OUT.mkdir(parents=True, exist_ok=True)
        source = (ROOT / 'android/host/dinput8.c').read_text()
        com = (ROOT / 'android/host/com.h').read_text()
        structure = re.search(r'typedef struct \{\s*void_com_object com;\s*int kind;.*?\} device_t;', source, re.S)[0]
        base = com[com.index('typedef struct void_com_object {'):com.index('} void_com_object;') + len('} void_com_object;')]
        begin = source.index('COM_METHOD(dev, GetDeviceData, 5)')
        end = source.index('\nCOM_METHOD(', begin + 1)
        # The exact acquisition bodies: the original mouse recovery depends on Poll failing.
        acquisition = ''
        for name in ('Acquire, 1', 'Unacquire, 1', 'Poll, 1'):
            first = source.index('COM_METHOD(dev, ' + name + ')')
            acquisition += source[first:source.index('\n}', first) + 2] + '\n'
        header = 'typedef struct void_com_class void_com_class;\n' + base + '\n' + structure + r'''
#define DI_OK 0u
#define DI_NOEFFECT 1u
#define DI_BUFFEROVERFLOW 1u
#define DIERR_NOTACQUIRED 0x8007000Cu
enum { DEV_KEYBOARD, DEV_MOUSE };
static device_t di_fixture;
static uint32_t di_args[5];
#undef ARG
#define ARG(i) di_args[i]
#define THIS_ ARG(0)
#define COM_METHOD(prefix,name,nargs) static uint32_t prefix##_##name##_impl(void)
static void *void_com_host(uint32_t value){assert(value==1);return &di_fixture;}
''' + acquisition + source[begin:end]
        (OUT / 'dinput_overflow_reader.h').write_text(header)
        spec = importlib.util.spec_from_file_location('dinput_host_env', ROOT / 'tests/host_environment.py')
        module = importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        env, linker = module.environment('x64')
        commands, logs = [], []

        def run(command, environment=None):
            commands.append(list(map(str, command)))
            result = subprocess.run(command, cwd=ROOT, env=environment, capture_output=True, timeout=30)
            text = (result.stdout + result.stderr).decode(errors='replace');logs.append(text)
            self.assertEqual(result.returncode, 0, text)
            return text

        test = ROOT / 'tests/test_dinput_overflow.c';obj = OUT / 'test.obj';binary = OUT / 'test.exe'
        run([str(module.CLANG), '/nologo', '/O2', '/W4', '/std:c11', '/D_CRT_SECURE_NO_WARNINGS',
             '/clang:--target=x86_64-pc-windows-msvc', '/I' + str(OUT), '/I' + str(ROOT / 'tests'), '/c', str(test), '/Fo' + str(obj)], env)
        run([str(linker), '/nologo', str(obj), '/OUT:' + str(binary)], env)
        print(run([str(binary)], env).strip())
        llvm = Path(os.environ.get('ANDROID_SDK_ROOT', Path.home() / 'AppData/Local/Android/Sdk')) / 'ndk/27.2.12479018/toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe'
        arm = OUT / 'test-dinput-overflow'
        run([str(llvm), '--target=aarch64-linux-android29', '-O2', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
             '-fno-strict-aliasing', '-fwrapv', '-DRECOMP_X87_WINDOW', '-I' + str(OUT), '-Iandroid/runtime', '-Iandroid/host', '-Igenerated', str(test), '-o', str(arm), '-lm'])
        for name in ('input.c', 'dinput8.c'):
            run([str(llvm), '--target=aarch64-linux-android29', '-O2', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                 '-fno-strict-aliasing', '-fwrapv', '-DRECOMP_X87_WINDOW', '-DUT3_ANDROID_APP', '-Iandroid/runtime', '-Iandroid/host', '-Igenerated',
                 '-c', str(ROOT / 'android/host' / name), '-o', str(OUT / (name + '.o'))])
        java = Path(os.environ.get('JAVA_HOME', 'C:/Portable/jdk-24.0.1'))
        regressions = [ROOT / 'tests' / ('test_touch_' + name + '.c') for name in ('native', 'ui', 'text')]
        for regression in regressions:
            obj = OUT / (regression.stem + '.obj');exe = OUT / (regression.stem + '.exe')
            run([str(module.CLANG), '/nologo', '/O2', '/std:c11', '/D_CRT_SECURE_NO_WARNINGS',
                 '/clang:--target=x86_64-pc-windows-msvc', '/FI' + str(ROOT / 'tests/touch_host_win.h'),
                 '/DJNINativeInterface=JNINativeInterface_',
                 '/I' + str(java / 'include'), '/I' + str(java / 'include/win32'),
                 '/c', str(regression), '/Fo' + str(obj)], env)
            run([str(linker), '/nologo', str(obj), '/OUT:' + str(exe)], env)
            print(run([str(exe)], env).strip())
            run([str(llvm), '--target=aarch64-linux-android29', '-O2', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                 '-Wno-unused-function', '-fno-strict-aliasing', '-fwrapv', '-DRECOMP_X87_WINDOW',
                 '-Iandroid/runtime', '-Iandroid/host', '-Igenerated', str(regression), '-o', str(OUT / regression.stem), '-lm'])
        (OUT / 'host.log').write_text('\n'.join(logs), encoding='utf-8')
        sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
        paths = [test, Path(__file__), ROOT / 'tests/host_environment.py', ROOT / 'tests/touch_host_win.h', *regressions,
                 ROOT / 'android/host/input.c', ROOT / 'android/host/dinput8.c', ROOT / 'android/host/input.h', ROOT / 'android/host/com.h', OUT / 'dinput_overflow_reader.h']
        report = {'status': 'HOST_PASS_ARM64_FIXTURE_AND_BOTH_PRODUCTION_FILES_COMPILED_DEVICE_PENDING',
                  'commands': commands, 'source_sha256': {str(p.relative_to(ROOT)): sha(p) for p in paths},
                  'android_binary_sha256': sha(arm), 'host_log_sha256': sha(OUT / 'host.log'),
                  'reader': 'Exact production GetDeviceData body and device/com structs; test ARG/COM lookup boundary only.',
                  'limits': 'No adb/game/APK/full build; existing shared-ring DIPROP_BUFFERSIZE behavior deliberately unchanged.'}
        (OUT / 'receipt.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    unittest.main()
