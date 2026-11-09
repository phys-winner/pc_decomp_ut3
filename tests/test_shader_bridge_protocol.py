"""Only explicitly proven native entries may reuse their lifted call contract."""
import copy
from pathlib import Path
import re
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import protocol_transform as pt
from build_android import (DEFAULT_CALL_PROTOCOL, NATIVE_CALL_CONTRACT, UE3_NATIVE_TARGETS, WRAPS,
                           native_same_unit_calls, validate_native_call_contract)


class ShaderBridgeProtocolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.proto = pt.Protocol(DEFAULT_CALL_PROTOCOL, full=set(WRAPS) - NATIVE_CALL_CONTRACT,
                                masked_native=NATIVE_CALL_CONTRACT)

    def test_actual_summary_has_exactly_the_proven_contract(self):
        self.assertEqual(NATIVE_CALL_CONTRACT,
                         {'sub_00EC2580', 'sub_00EC2610', 'sub_00EC26A0', 'sub_00EC2730', 'sub_0117D890'})
        self.assertTrue(NATIVE_CALL_CONTRACT <= set(UE3_NATIVE_TARGETS) <= set(WRAPS))
        validate_native_call_contract(self.proto)
        for name in NATIVE_CALL_CONTRACT:
            self.assertIn(name, self.proto.known)
            self.assertEqual(self.proto.cs(name), (0, 0))
            self.assertNotIn(name, self.proto.flags_in | self.proto.flags_out)
            self.assertEqual(self.proto.call_masks(name), (0x17, 0x17, 0))
            self.assertEqual(self.proto.call_masks('__wrap_' + name), (0x17, 0x17, 0))
        for name in set(WRAPS) - NATIVE_CALL_CONTRACT:
            self.assertEqual(self.proto.call_masks(name), (0xFF, 0xFF, 1))
            self.assertEqual(self.proto.call_masks('__wrap_' + name), (0xFF, 0xFF, 1))

    def test_changed_or_missing_summary_fails_closed(self):
        name = 'sub_00EC2580'
        for kind in ('known', 'regs', 'flags_in', 'flags_out'):
            proto = copy.copy(self.proto)
            if kind == 'known':
                proto.known = self.proto.known - {name}
            elif kind == 'regs':
                proto.regs = dict(self.proto.regs, **{name: (0x08, 0)})
            else:
                setattr(proto, kind, getattr(self.proto, kind) | {name})
            with self.subTest(kind=kind), self.assertRaisesRegex(ValueError, 'proven summary'):
                validate_native_call_contract(proto)

    def test_alias_is_masked_before_exit_and_dirty_analysis(self):
        body = (b'\n    RECOMP_CALL(__wrap_sub_00EC26A0); /* 0x00EC25FC: call 0xec26a0 */\n'
                b'    RECOMP_FLAGS_OUT(); esp += 4; { RECOMP_REGS_OUT(); return; }\n}\n')
        result = pt.transform_body('sub_00EC2580', body, self.proto, 'ut3')
        self.assertIn(b'RECOMP_CALL_P(__wrap_sub_00EC26A0, 0x17u, 0x17u, 0, 0x00EC25FCu)', result)
        self.assertIn(b'RECOMP_EXIT_P(0x17u)', result)
        self.assertNotIn(b'RECOMP_FLAGS_OUT', result)
        self.assertEqual(result.replace(b'__wrap_', b''),
                         pt.transform_body('sub_00EC2580', body.replace(b'__wrap_', b''), self.proto, 'ut3'))

    def test_other_wrapped_calls_keep_the_same_full_source(self):
        old_call = re.compile(rb'RECOMP_CALL\((sub_[0-9A-F]{8})\);')
        for name in set(WRAPS) - NATIVE_CALL_CONTRACT:
            body = (b'\n    RECOMP_CALL(__wrap_' + name.encode() + b'); /* 0x00EC25FC: call */\n'
                    b'    RECOMP_FLAGS_OUT(); esp += 4; { RECOMP_REGS_OUT(); return; }\n}\n')
            actual = pt.transform_body('sub_00EC2580', body, self.proto, 'ut3')
            with patch.object(pt, 'CALL', old_call):
                previous = pt.transform_body('sub_00EC2580', body, self.proto, 'ut3')
            self.assertEqual(actual, previous, name)
            self.assertIn(b'RECOMP_CALL(__wrap_' + name.encode() + b')', actual)
            self.assertIn(b'RECOMP_REGS_OUT()', actual)

    def test_real_same_unit_calls_and_original_bodies(self):
        raw = (ROOT / 'generated/ut3/functions_0417.c').read_text()
        source, count = native_same_unit_calls(raw)
        self.assertGreater(count, 0)
        result = pt.apply_protocol(source.encode(), self.proto, 'ut3')
        original = pt.apply_protocol(raw.encode(), self.proto, 'ut3')
        calls = re.findall(rb'RECOMP_CALL_P\(__wrap_(sub_00EC(?:2580|2610|26A0|2730)), ([^;]+);', result)
        self.assertGreater(len(calls), 0)
        for _, masks in calls:
            self.assertTrue(masks.startswith(b'0x17u, 0x17u, 0, '), masks)
        for name in NATIVE_CALL_CONTRACT - {'sub_0117D890'}:
            pattern = rb'void ' + name.encode() + rb'\(void\) \{.*?\n\}'
            a = re.search(pattern, result, re.S).group()
            b = re.search(pattern, original, re.S).group()
            self.assertEqual(a, b, name)
            self.assertIn(b'RECOMP_EXIT_P(0x1Fu)', a)
            self.assertNotIn(b'RECOMP_FLAGS_OUT()', a)


if __name__ == '__main__':
    unittest.main()
