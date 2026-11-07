"""tools/x87_slots.py on small synthetic lifted functions (the generator's forms, no game code).

    python -m unittest tests/test_x87_slots.py
"""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import x87_slots as x

RET = '    RECOMP_FLAGS_OUT(); esp += 4; { RECOMP_REGS_OUT(); return; } /* 0x10000040: ret */'


def body(*lines):
    return '\n'.join(['void sub_10000000(void) {', '    uint32_t _itail_tgt = 0;', *lines, '}', ''])


class X87SlotsTest(unittest.TestCase):
    def test_straight_line_uses_locals(self):
        text = x.transform(body(
            '    fp_push(*(float*)&MEM32(esi + 0x10)); /* 0x10000000: fld dword ptr [esi + 0x10] */',
            '    fp_push(*(float*)&MEM32(esi + 0x14)); /* 0x10000006: fld dword ptr [esi + 0x14] */',
            '    { double _v = fp_pop(); _st[0] += _v; } /* 0x1000000C: faddp st(1) */',
            '    { float _v = (float)fp_pop(); *(float*)ADDR(esi + 0x18) = _v; } /* 0x1000000E: fstp dword ptr [esi + 0x18] */',
            RET), 'sub_10000000')
        self.assertNotRegex(text, r'fp_push|fp_pop|_st\[')
        self.assertIn('s0 = (double)(*(float*)&MEM32(esi + 0x10))', text)
        self.assertIn('{ double _v = s1; s0 += _v; }', text)
        self.assertIn('{ float _v = (float)s0; *(float*)ADDR(esi + 0x18) = _v; }', text)

    def test_jump_table_targets_are_reached(self):
        # The case target is reached only through the table: its x87 code must be rewritten, not lost.
        text = x.transform(body(
            '    { _itail_tgt = (uint32_t)(eax); goto _ljump; }',
            'L_10000010:',
            '    fp_push(1.0); /* 0x10000010: fld1 */',
            '    { float _v = (float)fp_pop(); *(float*)ADDR(esi) = _v; } /* 0x10000012: fstp dword ptr [esi] */',
            RET,
            '  _ljump:',
            '    switch (_itail_tgt) {',
            '      case 0x10000010u: goto L_10000010;',
            '      default: RECOMP_ITAIL(_itail_tgt); { RECOMP_REGS_OUT(); return; }',
            '    }'), 'sub_10000000')
        self.assertIn('s0 = (double)(1.0)', text)
        self.assertIn('{ float _v = (float)s0; *(float*)ADDR(esi) = _v; }', text)

    def test_jump_table_with_values_on_the_stack_keeps_the_window(self):
        with self.assertRaises(x.X87Error):
            x.transform(body(
                '    fp_push(1.0); /* 0x10000000: fld1 */',
                '    { _itail_tgt = (uint32_t)(eax); goto _ljump; }',
                'L_10000010:',
                '    { float _v = (float)fp_pop(); *(float*)ADDR(esi) = _v; } /* 0x10000010: fstp dword ptr [esi] */',
                RET,
                '  _ljump:',
                '    switch (_itail_tgt) {',
                '      case 0x10000010u: goto L_10000010;',
                '      default: RECOMP_ITAIL(_itail_tgt); { RECOMP_REGS_OUT(); return; }',
                '    }'), 'sub_10000000')

    def test_unreached_x87_code_keeps_the_window(self):
        with self.assertRaises(x.X87Error):
            x.transform(body(
                RET,
                'L_10000010:',
                '    fp_push(1.0); /* 0x10000010: fld1 */',
                '    { float _v = (float)fp_pop(); *(float*)ADDR(esi) = _v; } /* 0x10000012: fstp dword ptr [esi] */',
                RET), 'sub_10000000')

    def test_interfaces_keep_the_window(self):
        cases = {
            'float result': ('    fp_push(1.0); /* 0x10000000: fld1 */', RET),
            'value from the caller': ('    { float _v = (float)fp_pop(); *(float*)ADDR(esi) = _v; } /* 0x10000000: fstp dword ptr [esi] */', RET),
            'discarded float result': ('    RECOMP_CALL(sub_10001000); /* 0x10000000: call 0x10001000 */',
                                       '    { _st[0] = _st[0]; g_st_i64[0] = g_st_i64[0]; fp_pop(); } /* 0x10000005: fstp st(0) */', RET),
            'call with a value': ('    fp_push(1.0); /* 0x10000000: fld1 */',
                                  '    RECOMP_CALL(sub_10001000); /* 0x10000002: call 0x10001000 */',
                                  '    { float _v = (float)fp_pop(); *(float*)ADDR(esi) = _v; } /* 0x10000007: fstp dword ptr [esi] */', RET),
            'exception frame': ('    RECOMP_EH_ENTER();', '    fp_push(1.0); /* 0x10000000: fld1 */',
                                '    { float _v = (float)fp_pop(); *(float*)ADDR(esi) = _v; } /* 0x10000002: fstp dword ptr [esi] */', RET),
            'integer shadow': ('    fp_push_i64((int64_t)MEM64(esi)); /* 0x10000000: fild qword ptr [esi] */', RET),
        }
        for name, lines in cases.items():
            with self.subTest(name), self.assertRaises(x.X87Error):
                x.transform(body(*lines), 'sub_10000000')

    def test_inconsistent_depth_keeps_the_window(self):
        with self.assertRaises(x.X87Error):
            x.transform(body(
                '    if (TEST_Z(_flag_a, _flag_b)) goto L_10000010; /* 0x10000000: je 0x10000010 */',
                '    fp_push(1.0); /* 0x10000002: fld1 */',
                'L_10000010:',
                '    fp_push(1.0); /* 0x10000010: fld1 */',
                '    { double _v = fp_pop(); _st[0] += _v; } /* 0x10000012: faddp st(1) */',
                '    { float _v = (float)fp_pop(); *(float*)ADDR(esi) = _v; } /* 0x10000014: fstp dword ptr [esi] */',
                RET), 'sub_10000000')

    def test_file_keeps_other_functions_and_uses_the_module_header(self):
        kept = body('    fp_push(1.0); /* 0x10000000: fld1 */', RET).replace('sub_10000000', 'sub_10000100')
        good = body('    fp_push(1.0); /* 0x10000000: fld1 */',
                    '    { float _v = (float)fp_pop(); *(float*)ADDR(esi) = _v; } /* 0x10000002: fstp dword ptr [esi] */', RET)
        integer = body('    eax = 1u;', RET).replace('sub_10000000', 'sub_10000200')
        source = '#include "functions.h"\n\n' + good + '\n' + kept + '\n' + integer
        text, rewritten, kept_count = x.transform_file(source, 'physxcore')
        self.assertEqual((rewritten, kept_count), (1, 1))
        self.assertTrue(text.startswith('#include "physxcore/functions.h"'))
        self.assertIn(kept, text)
        self.assertIn(integer, text)


if __name__ == '__main__':
    unittest.main()
