"""The PE images made at load time from the original Steam files equal generated/input; no adb, no APK."""
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/pe-images-test'
SOURCE = ROOT / 'android/host/pe_images.c'


class PeImagesTest(unittest.TestCase):
    def test_table_matches_modules_and_launcher(self):
        table = re.findall(r'\{"([^"]+)", (?:"([^"]+)"|NULL), \d+u, 0x[0-9A-F]{8}u\}', SOURCE.read_text(encoding='utf-8'))
        modules = {row['file']: row['path'] for row in json.loads((ROOT / 'config/modules.json').read_text())['modules']}
        listed = re.findall(r'^\s*\{\s*"[^"]+",\s*"([^"]+)"', (ROOT / 'generated/modules.inc').read_text(), re.M)
        self.assertEqual([file for file, _ in table], listed)
        for file, path in table:
            self.assertEqual(path or None, modules[file] if modules[file].startswith('Binaries/') else None, file)
        spec = json.loads((ROOT / 'android/port.json').read_text(encoding='utf-8'))
        part = next(p for d in spec['distributions'] for p in d['parts'] if p['id'] == 'game')
        self.assertTrue({path for _, path in table if path} <= set(part['required']))
        self.assertFalse(any(p['id'] == 'images' for d in spec['distributions'] for p in d['parts']))

    def test_images_from_original_files(self):
        OUT.mkdir(parents=True, exist_ok=True)
        spec = importlib.util.spec_from_file_location('pe_images_host_env', ROOT / 'tests/host_environment.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        env, linker = module.environment('x64')
        # zlib is Android's; the fixture supplies crc32 itself.
        (OUT / 'zlib.h').write_text('typedef unsigned char Bytef;\ntypedef unsigned int uInt;\ntypedef unsigned long uLong;\n'
                                    'uLong crc32(uLong crc, const Bytef *buf, uInt len);\n', encoding='ascii')
        rows = re.findall(r'^\s*\{\s*"[^"]+",\s*"([^"]+)",\s*(0x[0-9a-fA-F]+)u', (ROOT / 'generated/modules.inc').read_text(), re.M)
        (OUT / 'modules.txt').write_text(''.join(f'{file} {base}\n' for file, base in rows), encoding='ascii')
        obj, binary = OUT / 'test.obj', OUT / 'test.exe'
        for command in ([str(module.CLANG), '/nologo', '/O2', '/W4', '/std:c11', '/D_CRT_SECURE_NO_WARNINGS',
                         '/clang:--target=x86_64-pc-windows-msvc', '/I' + str(OUT), '/c', 'tests/test_pe_images.c', '/Fo' + str(obj)],
                        [str(linker), '/nologo', str(obj), '/OUT:' + str(binary)]):
            result = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        game = json.loads((ROOT / 'config/inputs.json').read_text())['source_root']
        result = subprocess.run([str(binary), game, str(ROOT / 'generated/input'), str(OUT / 'modules.txt')],
                                capture_output=True, text=True, timeout=300)
        print(result.stdout)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('PASS 20 images equal to generated/input', result.stdout)


if __name__ == '__main__':
    unittest.main()
