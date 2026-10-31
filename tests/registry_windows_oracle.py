"""Read-only Windows registry oracle: fixed, prevalidated existing keys only.

No values or keys are created, modified or deleted. RegCreateKeyEx is exercised
only against the exact allowlisted keys already proven by RegOpenKeyEx, with
KEY_READ. Empty subkeys refer to an existing open key or predefined root.
"""
import ctypes as c
import json
import platform
from pathlib import Path


def main():
    advapi = c.WinDLL("advapi32", use_last_error=True)
    kernel = c.WinDLL("kernel32", use_last_error=True)
    for suffix, text in (("A", c.c_char_p), ("W", c.c_wchar_p)):
        function = getattr(advapi, "RegOpenKeyEx" + suffix)
        function.argtypes = [c.c_void_p, text, c.c_uint32, c.c_uint32, c.c_void_p]
        function.restype = c.c_int32
        function = getattr(advapi, "RegCreateKeyEx" + suffix)
        function.argtypes = [c.c_void_p, text, c.c_uint32, text, c.c_uint32,
                             c.c_uint32, c.c_void_p, c.c_void_p, c.c_void_p]
        function.restype = c.c_int32
    advapi.RegCloseKey.argtypes = [c.c_void_p]
    advapi.RegCloseKey.restype = c.c_int32
    advapi.RegQueryInfoKeyW.argtypes = [c.c_void_p] * 12
    advapi.RegQueryInfoKeyW.restype = c.c_int32
    kernel.CreateEventW.argtypes = [c.c_void_p, c.c_int, c.c_int, c.c_wchar_p]
    kernel.CreateEventW.restype = c.c_void_p
    kernel.WaitForSingleObject.argtypes = [c.c_void_p, c.c_uint32]
    kernel.WaitForSingleObject.restype = c.c_uint32
    kernel.CloseHandle.argtypes = [c.c_void_p]
    kernel.CloseHandle.restype = c.c_int

    hklm = c.c_void_p(-2147483646).value
    key_read = 0x20019
    allowlist = ("Software", r"Software\Microsoft\Windows\CurrentVersion")
    output = Path(__file__).resolve().parents[1] / "research/registry-windows-oracle.json"
    result = {"windows_version": platform.version(), "pointer_bits": c.sizeof(c.c_void_p) * 8,
              "access_mask": key_read, "prevalidation": [], "creates": [],
              "open_empty_subkeys": [], "close_checks": [], "cleanup": []}
    owned = set()
    events = set()
    validated = set()

    def save():
        output.write_text(json.dumps(result, indent=2) + "\n")

    def keep(handle, status):
        if status == 0 and handle and handle != hklm:
            owned.add(handle)

    def open_key(suffix, root, path, label):
        handle = c.c_void_p(0xA5A5A5A5A5A5A5A5)
        subkey = path.encode("ascii") if suffix == "A" and path is not None else path
        c.set_last_error(0x1234)
        status = getattr(advapi, "RegOpenKeyEx" + suffix)(root, subkey, 0, key_read, c.byref(handle))
        error = c.get_last_error()
        keep(handle.value, status)
        return handle.value, {"label": label, "api": "RegOpenKeyEx" + suffix,
                              "subkey": path, "status": status, "last_error": error,
                              "handle_nonnull": bool(handle.value), "handle_bits": handle.value,
                              "handle_equals_root": bool(handle.value) and handle.value == root}

    def create_existing(suffix, root, path, label, reserved=0, options=0):
        # No unvalidated nonempty registry path can reach RegCreateKeyEx.
        assert path is None or path == "" or path in validated
        handle = c.c_void_p(0xA5A5A5A5A5A5A5A5)
        disposition = c.c_uint32(0xA5A5A5A5)
        subkey = path.encode("ascii") if suffix == "A" and path is not None else path
        c.set_last_error(0x1234)
        status = getattr(advapi, "RegCreateKeyEx" + suffix)(
            root, subkey, reserved, None, options, key_read, None,
            c.byref(handle), c.byref(disposition))
        error = c.get_last_error()
        keep(handle.value, status)
        row = {"label": label, "api": "RegCreateKeyEx" + suffix, "subkey": path,
               "reserved": reserved, "options": options, "status": status,
               "last_error": error, "disposition": disposition.value,
               "handle_nonnull": bool(handle.value), "handle_bits": handle.value,
               "handle_equals_root": bool(handle.value) and handle.value == root}
        result["creates"].append(row)
        save()
        # This also detects any unexpected behavior immediately; all paths are
        # existing and KEY_READ prevents creating a child of an opened key.
        if status == 0:
            assert disposition.value == 2, "Native unexpectedly reports CREATED_NEW_KEY"
        return handle.value, row

    def close(handle, label):
        c.set_last_error(0x1234)
        status = advapi.RegCloseKey(handle)
        row = {"label": label, "status": status, "last_error": c.get_last_error()}
        if status == 0:
            owned.discard(handle)
        result["close_checks"].append(row)
        save()
        return status

    def query(handle):
        c.set_last_error(0x1234)
        status = advapi.RegQueryInfoKeyW(handle, *([None] * 11))
        return {"status": status, "last_error": c.get_last_error()}

    save()
    try:
        opened = {}
        for path in allowlist:
            for suffix in ("A", "W"):
                handle, row = open_key(suffix, hklm, path, "prevalidate_existing")
                result["prevalidation"].append(row)
                save()
                assert row["status"] == 0
                opened[(path, suffix)] = handle
            validated.add(path)

        for path in allowlist:
            for suffix in ("A", "W"):
                create_existing(suffix, hklm, path, "open_existing_read_only")
        for suffix in ("A", "W"):
            for subkey in (None, ""):
                create_existing(suffix, hklm, subkey, "predefined_root_empty_subkey")
                handle, row = open_key(suffix, hklm, subkey, "predefined_root_empty_subkey")
                result["open_empty_subkeys"].append(row)
                existing = opened[(allowlist[0], suffix)]
                create_existing(suffix, existing, subkey, "opened_key_empty_subkey")
                handle, row = open_key(suffix, existing, subkey, "opened_key_empty_subkey")
                result["open_empty_subkeys"].append(row)
                save()
            for reserved in (1, 0xFFFFFFFF):
                create_existing(suffix, hklm, allowlist[0], "invalid_reserved", reserved=reserved)
            for options in (8, 0x80000000):
                create_existing(suffix, hklm, allowlist[0], "options_probe", options=options)
            for root, label in ((None, "null_root"), (0x12345678, "invalid_handle")):
                create_existing(suffix, root, allowlist[0], label)

        original = opened[(allowlist[0], "W")]
        duplicate, row = open_key("W", original, None, "duplicate_existing_key")
        result["duplicate_lifetime"] = {"open": row, "same_handle": duplicate == original,
                                        "before_original_close": query(duplicate)}
        close(original, "original_before_duplicate")
        result["duplicate_lifetime"]["after_original_close"] = query(duplicate)
        close(duplicate, "duplicate")
        result["duplicate_lifetime"]["after_duplicate_close"] = query(duplicate)
        close(duplicate, "duplicate_double_close")

        event = kernel.CreateEventW(None, True, True, None)
        assert event
        events.add(event)
        c.set_last_error(0x1234)
        before = kernel.WaitForSingleObject(event, 0)
        before_error = c.get_last_error()
        status = close(event, "event_wrong_type")
        c.set_last_error(0x1234)
        after = kernel.WaitForSingleObject(event, 0)
        after_error = c.get_last_error()
        if status == 0 and after == 0xFFFFFFFF and after_error == 6:
            events.remove(event)  # Native RegCloseKey closed this owned event.
        result["wrong_type_event"] = {"reg_close_status": status, "wait_before": before,
                                       "wait_before_error": before_error, "wait_after": after,
                                       "wait_after_error": after_error,
                                       "event_remains_usable": before == after == 0,
                                       "event_closed_by_native_reg_close": event not in events}
        result["typed_host_divergence"] = (
            "Native Windows accepts RegCloseKey on an owned event kernel handle and closes it. "
            "The Android host intentionally rejects non-registry handle types with error6, "
            "so an erroneous registry close cannot destroy an unrelated guest event.")
        close(hklm, "predefined_root")
        handle, row = open_key("A", hklm, allowlist[0], "root_still_usable_after_close")
        result["root_after_close"] = row
        save()
    finally:
        for handle in list(owned):
            status = advapi.RegCloseKey(handle)
            result["cleanup"].append({"kind": "registry", "status": status})
            if status == 0:
                owned.remove(handle)
        for handle in list(events):
            success = kernel.CloseHandle(handle)
            result["cleanup"].append({"kind": "event", "success": bool(success)})
            if success:
                events.remove(handle)
        result["all_owned_handles_closed"] = not owned and not events
        result["scope"] = (
            "Only the two fixed existing HKLM paths were queried. RegCreateKeyEx used KEY_READ "
            "and required prior A/W RegOpenKeyEx validation. No nonexistent path, ALL_ACCESS, "
            "RegSetValue, RegDeleteValue or RegDeleteKey call is present in the replay.")
        result["sources"] = [
            "https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regcreatekeyexa",
            "https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regopenkeyexa",
            "https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-regclosekey",
        ]
        save()
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
