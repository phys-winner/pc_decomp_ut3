"""Independent Windows API oracle for UE3's NULL + MEM_COMMIT allocation."""
import ctypes
from ctypes import wintypes
import sys
import unittest


@unittest.skipUnless(sys.platform == "win32", "requires the original Windows API")
class VirtualAllocOracle(unittest.TestCase):
    def test_null_commit_query_zeroing_rounding_and_nonnull_failure(self):
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.VirtualAlloc.argtypes = [ctypes.c_void_p, ctypes.c_size_t, wintypes.DWORD, wintypes.DWORD]
        kernel.VirtualAlloc.restype = ctypes.c_void_p
        kernel.VirtualFree.argtypes = [ctypes.c_void_p, ctypes.c_size_t, wintypes.DWORD]
        kernel.VirtualFree.restype = wintypes.BOOL

        class MBI(ctypes.Structure):
            _fields_ = [("base", ctypes.c_void_p), ("allocation", ctypes.c_void_p),
                        ("allocation_protect", wintypes.DWORD), ("size", ctypes.c_size_t),
                        ("state", wintypes.DWORD), ("protect", wintypes.DWORD), ("kind", wintypes.DWORD)]

        kernel.VirtualQuery.argtypes = [ctypes.c_void_p, ctypes.POINTER(MBI), ctypes.c_size_t]
        kernel.VirtualQuery.restype = ctypes.c_size_t
        for size in (1, 0x10000, 0x10001):
            with self.subTest(size=size):
                rounded = (size + 4095) & -4096
                address = kernel.VirtualAlloc(None, size, 0x1000, 4)
                self.assertTrue(address)
                try:
                    self.assertFalse(address & 0xFFFF)
                    info = MBI()
                    self.assertEqual(kernel.VirtualQuery(address, ctypes.byref(info), ctypes.sizeof(info)), ctypes.sizeof(info))
                    self.assertEqual((info.base, info.allocation, info.allocation_protect), (address, address, 4))
                    self.assertEqual((info.size, info.state, info.protect, info.kind), (rounded, 0x1000, 4, 0x20000))
                    self.assertEqual(ctypes.string_at(address, rounded), bytes(rounded))
                    ctypes.memset(address, 0xAB, rounded)
                    self.assertEqual(kernel.VirtualAlloc(address, size, 0x1000, 4), address)
                    self.assertEqual(ctypes.string_at(address, rounded), b"\xAB" * rounded)
                    self.assertTrue(kernel.VirtualFree(address, rounded, 0x4000))
                    self.assertEqual(kernel.VirtualAlloc(address, size, 0x1000, 4), address)
                    self.assertEqual(ctypes.string_at(address, rounded), bytes(rounded))
                finally:
                    self.assertTrue(kernel.VirtualFree(address, 0, 0x8000))
                ctypes.set_last_error(0)
                self.assertFalse(kernel.VirtualAlloc(address, size, 0x1000, 4))
                self.assertEqual(ctypes.get_last_error(), 487)


if __name__ == "__main__":
    unittest.main()
