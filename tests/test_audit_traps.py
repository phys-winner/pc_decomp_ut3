import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("audit_traps", Path(__file__).resolve().parents[1] / "tools/audit_traps.py")
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


class TrapAuditTest(unittest.TestCase):
    def test_original_address_and_escaped_reason(self):
        source = r'void_lift_trap(0x100593D3u, "fnstenv [esp]"); void_lift_trap(0x1234, "quoted \"value\"");'
        self.assertEqual(audit.parse_traps(source), [(0x100593D3, "fnstenv [esp]"), (0x1234, 'quoted "value"')])
        self.assertEqual(audit.parse_traps("void_lift_trap(address, reason);"), [])


if __name__ == "__main__":
    unittest.main()
