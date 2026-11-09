"""Stripping packaging symbols must preserve loaded ARM64 code/data and offline symbols."""
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from elftools.elf.elffile import ELFFile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import build_apk


def loaded_image(path):
    with path.open("rb") as stream:
        elf = ELFFile(stream)
        sections = {s.name: (s['sh_addr'], s['sh_size'], s['sh_flags'], s['sh_type'],
                            None if s['sh_type'] == 'SHT_NOBITS' else s.data())
                    for s in elf.iter_sections() if s['sh_flags'] & 2}
        loads = [dict(s.header) for s in elf.iter_segments() if s['p_type'] == 'PT_LOAD']
        return sections, loads, elf.get_section_by_name('.symtab') is not None


class ApkSymbolsTest(unittest.TestCase):
    def test_arm64_payload_keeps_loaded_image_and_unstripped_input(self):
        sdk = Path(os.environ.get('ANDROID_SDK_ROOT', Path.home() / 'AppData/Local/Android/Sdk'))
        ndk = Path(os.environ.get('ANDROID_NDK_HOME', sdk / 'ndk/27.2.12479018'))
        llvm = ndk / 'toolchains/llvm/prebuilt/windows-x86_64/bin'
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            source, original, packaged = directory / 'fixture.c', directory / 'original.so', directory / 'packaged.so'
            source.write_text('static volatile int value=41;\nstatic int helper(void){return value+1;}\n'
                              '__attribute__((visibility("default"))) int entry(void){return helper();}\n')
            subprocess.run([str(llvm / 'clang.exe'), '--target=aarch64-linux-android29', '-O0', '-fPIC',
                            '-shared', '-nostdlib', str(source), '-o', str(original)], check=True, capture_output=True)
            original_sha = hashlib.sha256(original.read_bytes()).hexdigest()
            before = loaded_image(original)
            self.assertTrue(before[2])
            build_apk.strip_for_apk(original, packaged, llvm / 'llvm-strip.exe')
            after = loaded_image(packaged)
            self.assertEqual(before[:2], after[:2])
            self.assertFalse(after[2])
            self.assertLess(packaged.stat().st_size, original.stat().st_size)
            self.assertEqual(original_sha, hashlib.sha256(original.read_bytes()).hexdigest())
            with self.assertRaisesRegex(ValueError, 'unstripped'):
                build_apk.strip_for_apk(original, original, llvm / 'llvm-strip.exe')
            self.assertEqual(original_sha, hashlib.sha256(original.read_bytes()).hexdigest())


if __name__ == '__main__':
    unittest.main()
