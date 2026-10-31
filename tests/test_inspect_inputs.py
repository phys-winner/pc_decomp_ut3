import contextlib
import importlib.util
import io
import tempfile
import unittest
from pathlib import Path


spec = importlib.util.spec_from_file_location("inspect_inputs", Path(__file__).resolve().parents[1] / "tools" / "inspect_inputs.py")
inventory = importlib.util.module_from_spec(spec)
spec.loader.exec_module(inventory)


class VerifyInputsTest(unittest.TestCase):
    def test_verified_changed_and_missing(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / "example.exe"
            path.write_bytes(b"original")
            manifest = {"modules": [{"path": path.name, "size": 8, "sha256": inventory.digest(path)}]}
            with contextlib.redirect_stdout(io.StringIO()):
                inventory.verify(root, manifest)
            path.write_bytes(b"modified")
            with self.assertRaisesRegex(SystemExit, "Changed input"):
                inventory.verify(root, manifest)
            path.unlink()
            with self.assertRaisesRegex(SystemExit, "Missing input"):
                inventory.verify(root, manifest)


if __name__ == "__main__":
    unittest.main()
