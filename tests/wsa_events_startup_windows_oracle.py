"""Run in fresh `python -I -S`: event APIs before/after explicit WSA startup."""
import ctypes as c
import json
from pathlib import Path

winsock = c.WinDLL("ws2_32", use_last_error=True)
kernel = c.WinDLL("kernel32", use_last_error=True)
winsock.WSACreateEvent.restype = c.c_void_p
winsock.WSACloseEvent.argtypes = [c.c_void_p]
winsock.WSACloseEvent.restype = c.c_int
winsock.WSAGetLastError.restype = c.c_int
winsock.WSAStartup.argtypes = [c.c_uint16, c.c_void_p]
winsock.WSAStartup.restype = c.c_int
winsock.WSACleanup.restype = c.c_int
kernel.WaitForSingleObject.argtypes = [c.c_void_p, c.c_uint32]
kernel.WaitForSingleObject.restype = c.c_uint32
kernel.CloseHandle.argtypes = [c.c_void_p]
kernel.CloseHandle.restype = c.c_int
result = {"fresh_interpreter": "python -I -S", "cases": []}
owned = set()
started = False


def probe(label):
    c.set_last_error(0x1234)
    handle = winsock.WSACreateEvent()
    row = {"label": label, "created": bool(handle),
           "win32_last_error": c.get_last_error(), "wsa_last_error": winsock.WSAGetLastError()}
    result["cases"].append(row)
    if handle:
        owned.add(handle)
        row["kernel32_wait0"] = kernel.WaitForSingleObject(handle, 0)
        c.set_last_error(0x1234)
        row["wsa_close_result"] = winsock.WSACloseEvent(handle)
        row["wsa_close_error"] = winsock.WSAGetLastError()
        if row["wsa_close_result"]:
            owned.remove(handle)


try:
    probe("before_explicit_WSAStartup")
    data = (c.c_uint8 * 512)()
    result["WSAStartup"] = winsock.WSAStartup(0x0202, data)
    assert result["WSAStartup"] == 0
    started = True
    result["matched_WSACleanup"] = winsock.WSACleanup()
    assert result["matched_WSACleanup"] == 0
    started = False
    probe("after_matched_WSACleanup")
finally:
    result["fallback_handle_closes"] = [bool(kernel.CloseHandle(h)) for h in owned]
    if started:
        result["finally_WSACleanup"] = winsock.WSACleanup()
    result["all_owned_events_closed"] = not owned or all(result["fallback_handle_closes"])
    output = Path(__file__).resolve().parents[1] / "research/wsa-events-windows-oracle.json"
    saved = json.loads(output.read_text())
    saved["fresh_process_startup_requirement"] = result
    output.write_text(json.dumps(saved, indent=2) + "\n")
print(json.dumps(result, indent=2))
