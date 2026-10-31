"""HOST-run the canonical query policy and cross-compile its Android fixture; no adb."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/query-cooperative-canonical'


class QueryWaitTest(unittest.TestCase):
    def test_canonical_policy(self):
        OUT.mkdir(parents=True, exist_ok=True)
        spec = importlib.util.spec_from_file_location('query_policy_host_env', ROOT / 'tests/host_environment.py')
        module = importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        env, linker = module.environment('x64')
        source = ROOT / 'tests/test_dxvk_query_wait.c'
        obj, binary = OUT / 'test.obj', OUT / 'test.exe'
        commands, output = [], []

        def run(command, environment=None):
            commands.append(list(map(str, command)))
            result = subprocess.run(command, cwd=ROOT, env=environment, capture_output=True, timeout=30)
            text = (result.stdout + result.stderr).decode(errors='replace')
            output.append(text)
            self.assertEqual(result.returncode, 0, text)
            return text

        run([str(module.CLANG), '/nologo', '/O2', '/W4', '/std:c11', '/D_CRT_SECURE_NO_WARNINGS',
             '/clang:--target=x86_64-pc-windows-msvc', '/c', str(source), '/Fo' + str(obj)], env)
        run([str(linker), '/nologo', str(obj), '/OUT:' + str(binary)], env)
        cases = [('--unset', '0'), ('', '0'), ('0', '0'), ('50', '50'), ('1000', '1000'),
                 ('1001', '0'), ('invalid', '0'), ('-1', '0'), ('50x', '0'), ('18446744073709551616', '0')]
        for value, expected in cases:
            run([str(binary), value, expected], env)
        llvm = Path(os.environ.get('ANDROID_SDK_ROOT', Path.home() / 'AppData/Local/Android/Sdk')) / 'ndk/27.2.12479018/toolchains/llvm/prebuilt/windows-x86_64/bin/clang.exe'
        arm = OUT / 'test-query-policy'
        run([str(llvm), '--target=aarch64-linux-android29', '-O2', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
             str(source), '-o', str(arm)])
        (OUT / 'host.log').write_text('\n'.join(output), encoding='utf-8')
        sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
        sources = [ROOT / 'android/dxvk/query_wait.inc', source, Path(__file__)]
        activity = (ROOT / 'android/app/src/org/ut3/port/GameActivity.java').read_text(encoding='utf-8')
        self.assertIn('getIntExtra("queryWaitUs", 0)', activity)
        self.assertIn('getIntExtra("precacheWaitUs", 50)', activity)
        sources.append(ROOT / 'android/app/src/org/ut3/port/GameActivity.java')
        report = {'status': 'HOST_10_CASES_PASS_ANDROID_ARM64_CROSSCOMPILED_NOT_RUN',
                  'default_us': 0, 'precache_default_us': 50, 'cases': cases, 'commands': commands,
                  'source_sha256': {str(p.relative_to(ROOT)): sha(p) for p in sources},
                  'host_log_sha256': sha(OUT / 'host.log'), 'android_binary_sha256': sha(arm),
                  'scope': 'Canonical policy exact gate/reset/parser/EINTR/handoff/null hook; actual FIFO/register/native-call boundary covered by pinned build/query-cooperative/receipt.json; no adb or APK build.'}
        (OUT / 'receipt.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    unittest.main()
