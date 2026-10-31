"""Native owner IDs of one own hidden window; no focus or desktop changes."""
import ctypes as c
import json
import platform
from pathlib import Path


def main():
    u = c.WinDLL("user32", use_last_error=True)
    k = c.WinDLL("kernel32", use_last_error=True)
    u.CreateWindowExW.argtypes = [c.c_uint32, c.c_wchar_p, c.c_wchar_p, c.c_uint32,
                                  c.c_int, c.c_int, c.c_int, c.c_int, c.c_void_p,
                                  c.c_void_p, c.c_void_p, c.c_void_p]
    u.CreateWindowExW.restype = c.c_void_p
    u.DestroyWindow.argtypes = [c.c_void_p]
    u.DestroyWindow.restype = c.c_int
    u.IsWindowVisible.argtypes = [c.c_void_p]
    u.IsWindowVisible.restype = c.c_int
    u.GetWindowThreadProcessId.argtypes = [c.c_void_p, c.c_void_p]
    u.GetWindowThreadProcessId.restype = c.c_uint32
    k.GetCurrentThreadId.restype = c.c_uint32
    k.GetCurrentProcessId.restype = c.c_uint32
    k.VirtualAlloc.argtypes = [c.c_void_p, c.c_size_t, c.c_uint32, c.c_uint32]
    k.VirtualAlloc.restype = c.c_void_p
    k.VirtualFree.argtypes = [c.c_void_p, c.c_size_t, c.c_uint32]
    k.VirtualFree.restype = c.c_int
    output = Path(__file__).resolve().parents[1] / "research/window-owner-windows-oracle.json"
    result = {"windows_version": platform.version(), "pointer_bits": c.sizeof(c.c_void_p) * 8,
              "expected_thread_id": k.GetCurrentThreadId(),
              "expected_process_id": k.GetCurrentProcessId(), "calls": [], "finally": {}}
    window = None
    noaccess = None

    def save():
        output.write_text(json.dumps(result, indent=2) + "\n")

    def call(label, hwnd, null=False, noaccess_pointer=None, offset=4):
        buffer = (c.c_uint8 * 16)(*([0xA5] * 16))
        pointer = None if null else noaccess_pointer if noaccess_pointer else c.addressof(buffer) + offset
        c.set_last_error(0x1234)
        row = {"label": label}
        try:
            row["thread_id_return"] = u.GetWindowThreadProcessId(hwnd, pointer)
        except OSError as error:
            row["exception"] = str(error)
        row["last_error"] = c.get_last_error()
        if not null and not noaccess_pointer:
            raw = bytes(buffer)
            row.update(pid_output=int.from_bytes(raw[offset:offset + 4], "little"),
                       buffer_hex=raw.hex(),
                       prefix_guard_unchanged=raw[:offset] == bytes([0xA5] * offset),
                       suffix_guard_unchanged=raw[offset + 4:] == bytes([0xA5] * (12 - offset)),
                       output_unchanged=raw[offset:offset + 4] == bytes([0xA5] * 4))
        result["calls"].append(row)
        save()

    save()
    try:
        window = u.CreateWindowExW(0x08000080, "STATIC", "UT3 owner oracle", 0x00CF0000,
                                   40, 60, 200, 150, None, None, None, None)
        assert window and not u.IsWindowVisible(window)
        call("valid_aligned_PID", window)
        call("valid_optional_NULL_PID", window, null=True)
        call("valid_unaligned_PID", window, offset=5)
        for label, handle in (("NULL_HWND", None), ("random_HWND", 0x123456)):
            call(label, handle)
            call(label + "_NULL_PID", handle, null=True)
        noaccess = k.VirtualAlloc(None, 4096, 0x3000, 0x01)
        assert noaccess
        call("valid_HWND_own_PAGE_NOACCESS_PID", window, noaccess_pointer=noaccess)
        stale = window
        result["explicit_destroy"] = bool(u.DestroyWindow(window))
        assert result["explicit_destroy"]
        window = None
        call("stale_HWND_after_destroy", stale)
        call("stale_HWND_NULL_PID", stale, null=True)
    finally:
        if noaccess:
            result["finally"]["own_noaccess_page_freed"] = bool(k.VirtualFree(noaccess, 0, 0x8000))
        if window:
            result["finally"]["window_still_hidden"] = not bool(u.IsWindowVisible(window))
            result["finally"]["window_destroyed"] = bool(u.DestroyWindow(window))
        result["scope"] = "Own hidden nonactivating STATIC window and own PAGE_NOACCESS page only; no focus/foreground/desktop settings changes."
        result["sources"] = ["https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getwindowthreadprocessid"]
        save()
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
