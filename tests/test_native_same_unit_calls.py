"""Native interception must survive the caller sharing a translation unit with the lifted fallback."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from build_android import UE3_NATIVE_TARGETS, WRAPS, native_same_unit_calls
from tests.host_environment import CLANG, environment


class NativeSameUnitCallsTest(unittest.TestCase):
    def test_undefined_and_unrelated_calls_stay_for_the_linker(self):
        text = 'void sub_12345678(void) {\n    RECOMP_CALL(sub_00486030);\n}\n'
        self.assertEqual(native_same_unit_calls(text), (text, 0))
        self.assertEqual(len(WRAPS), len(set(WRAPS)))
        self.assertTrue(set(UE3_NATIVE_TARGETS) <= set(WRAPS))

    def test_declarations_definition_and_cached_include(self):
        body = 'void sub_00486030(void) {\n    value += 7;\n}\n'
        source = '#include "functions.h"\n' + body + 'void caller(void) { RECOMP_CALL(sub_00486030); RECOMP_CALL(sub_00486030); RECOMP_CALL(sub_12345678); }\n'
        result, count = native_same_unit_calls(source)
        self.assertEqual(count, 2)
        self.assertIn(body, result)
        self.assertIn('#include "ut3/functions.h"', result)
        self.assertEqual(result.count('extern void __wrap_sub_00486030(void);'), 1)
        self.assertEqual(result.count('RECOMP_CALL(__wrap_sub_00486030)'), 2)
        self.assertIn('RECOMP_CALL(sub_12345678)', result)
        self.assertEqual(native_same_unit_calls(result), (result, 0))

    def test_compiled_same_unit_call_uses_native_and_keeps_fallback(self):
        definitions = ''.join(f'void {name}(void) {{\n    value += {i + 1};\n}}\n'
                              for i, name in enumerate(UE3_NATIVE_TARGETS))
        caller = 'static void caller(void) {\n' + ''.join('    RECOMP_CALL(' + name + ');\n' for name in UE3_NATIVE_TARGETS) + '}\n'
        source, count = native_same_unit_calls(definitions + caller)
        self.assertEqual(count, len(UE3_NATIVE_TARGETS))
        wrappers = ''.join('void __wrap_' + name + '(void) { ++calls; if (mode == 1) value += 100; else ' + name + '(); }\n'
                           for name in UE3_NATIVE_TARGETS)
        total = sum(range(1, count + 1))
        program = '#include <assert.h>\nstatic int value, calls, mode;\n#define RECOMP_CALL(f) f()\n' + source + wrappers + f'''
int main(void) {{
    for (mode = 0; mode <= 2; ++mode) {{
        value = calls = 0; caller();
        assert(calls == {count}); assert(value == (mode == 1 ? {100 * count} : {total}));
    }}
    value = calls = 0;
''' + ''.join(name + '();\n' for name in UE3_NATIVE_TARGETS) + f'assert(value == {total} && !calls); return 0; }}\n'
        env, linker = environment('x64')
        with tempfile.TemporaryDirectory() as folder:
            folder = Path(folder)
            path = folder / 'check.c'
            path.write_text(program, encoding='ascii')
            for command in ([CLANG, '/nologo', '/O2', '/std:c11', '/c', path, '/Fo' + str(folder / 'check.obj')],
                            [linker, '/nologo', folder / 'check.obj', '/OUT:' + str(folder / 'check.exe')],
                            [folder / 'check.exe']):
                result = subprocess.run(list(map(str, command)), env=env, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
