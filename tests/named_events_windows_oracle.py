"""Own uniquely named Local event objects only; no persistent/global settings."""
import ctypes as c
import json
import platform
import uuid
from pathlib import Path


def main():
    k = c.WinDLL("kernel32", use_last_error=True)
    for suffix, string_type in (("A", c.c_char_p), ("W", c.c_wchar_p)):
        f = getattr(k, "CreateEvent" + suffix)
        f.argtypes = [c.c_void_p, c.c_int, c.c_int, string_type]
        f.restype = c.c_void_p
        f = getattr(k, "OpenEvent" + suffix)
        f.argtypes = [c.c_uint32, c.c_int, string_type]
        f.restype = c.c_void_p
    k.WaitForSingleObject.argtypes = [c.c_void_p, c.c_uint32]
    k.WaitForSingleObject.restype = c.c_uint32
    for name in ("SetEvent", "ResetEvent", "CloseHandle"):
        f = getattr(k, name)
        f.argtypes = [c.c_void_p]
        f.restype = c.c_int
    k.GetHandleInformation.argtypes = [c.c_void_p, c.c_void_p]
    k.GetHandleInformation.restype = c.c_int
    event_name = "Local\\UT3Oracle_" + uuid.uuid4().hex
    wrong_case = "Local\\" + event_name.split("\\", 1)[1].lower()
    result = {"windows_version": platform.version(), "name": event_name,
              "calls": [], "cleanup": []}
    owned = set()
    output = Path(__file__).resolve().parents[1] / "research/named-events-windows-oracle.json"

    def save():
        output.write_text(json.dumps(result, indent=2) + "\n")

    def call(label, function, *args):
        c.set_last_error(0x1234)
        row = {"label": label}
        try:
            row["result"] = function(*args)
        except OSError as error:
            row["exception"] = str(error)
        row["error"] = c.get_last_error()
        result["calls"].append(row)
        save()
        return row

    def create(label, suffix, name, manual=True, initial=False):
        text = name.encode("ascii") if suffix == "A" and name is not None else name
        row = call(label, getattr(k, "CreateEvent" + suffix), None, manual, initial, text)
        if row.get("result"):
            owned.add(row["result"])
        return row

    def open_event(label, suffix, name, access=0x100000, inherit=False):
        text = name.encode("ascii") if suffix == "A" and name is not None else name
        row = call(label, getattr(k, "OpenEvent" + suffix), access, inherit, text)
        if row.get("result"):
            owned.add(row["result"])
        return row

    def close(label, handle):
        row = call(label, k.CloseHandle, handle)
        if row.get("result"):
            owned.remove(handle)
        return row

    save()
    try:
        first_row = create("create_A_unique_manual_unsignaled", "A", event_name)
        first = first_row.get("result")
        assert first and first_row["error"] != 183, "Unique event name unexpectedly existed"
        duplicate_row = create("create_W_existing_opposite_state_and_mode", "W", event_name,
                               manual=False, initial=True)
        duplicate = duplicate_row.get("result")
        assert duplicate and duplicate_row["error"] == 183
        assert call("existing_initial_true_ignored", k.WaitForSingleObject, duplicate, 0)["result"] == 258
        assert call("set_original", k.SetEvent, first)["result"]
        assert call("duplicate_shared_signal_wait1", k.WaitForSingleObject, duplicate, 0)["result"] == 0
        assert call("duplicate_shared_signal_wait2_manual_persists", k.WaitForSingleObject, duplicate, 0)["result"] == 0
        assert call("reset_duplicate", k.ResetEvent, duplicate)["result"]
        assert call("original_shared_reset", k.WaitForSingleObject, first, 0)["result"] == 258
        for suffix in ("A", "W"):
            opened = open_event("open_" + suffix + "_SYNCHRONIZE_inherit", suffix, event_name,
                                access=0x100000, inherit=True)
            handle = opened.get("result")
            assert handle
            flags = c.c_uint32(0xA5A5A5A5)
            row = call("handle_inheritance_" + suffix, k.GetHandleInformation, handle, c.byref(flags))
            row["flags"] = flags.value
            result["calls"][-1] = row
            assert row["result"] and flags.value & 1
            assert call("set_SYNCHRONIZE_only_" + suffix, k.SetEvent, handle)["result"] == 0
            assert result["calls"][-1]["error"] == 5
            open_event("open_" + suffix + "_wrong_case", suffix, wrong_case)
            for empty in (None, ""):
                create("create_" + suffix + ("_NULL_name" if empty is None else "_empty_name"), suffix, empty)
                open_event("open_" + suffix + ("_NULL_name" if empty is None else "_empty_name"), suffix, empty)
        for handle in list(owned):
            assert close("close_owned_before_lifetime_check", handle)["result"]
        result["after_last_close_A"] = open_event("open_A_after_last_close", "A", event_name)
        result["after_last_close_W"] = open_event("open_W_after_last_close", "W", event_name)
        assert not result["after_last_close_A"].get("result")
        assert not result["after_last_close_W"].get("result")
    finally:
        for handle in list(owned):
            success = k.CloseHandle(handle)
            result["cleanup"].append({"success": bool(success), "error": c.get_last_error()})
            if success:
                owned.remove(handle)
        result["all_owned_handles_closed"] = not owned
        result["scope"] = "Only unique owned Local event name and unnamed owned events; all handles closed in finally."
        save()
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
