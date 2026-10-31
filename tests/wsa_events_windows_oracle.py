"""Native WinSock event semantics, using owned events and no network traffic."""
import ctypes as c
import json
import platform
import threading
import time
from pathlib import Path


def main():
    winsock = c.WinDLL("ws2_32", use_last_error=True)
    kernel = c.WinDLL("kernel32", use_last_error=True)
    winsock.WSAStartup.argtypes = [c.c_uint16, c.c_void_p]
    winsock.WSAStartup.restype = c.c_int
    winsock.WSACleanup.argtypes = []
    winsock.WSACleanup.restype = c.c_int
    winsock.WSAGetLastError.argtypes = []
    winsock.WSAGetLastError.restype = c.c_int
    winsock.WSACreateEvent.argtypes = []
    winsock.WSACreateEvent.restype = c.c_void_p
    for name in ("WSASetEvent", "WSAResetEvent", "WSACloseEvent"):
        function = getattr(winsock, name)
        function.argtypes = [c.c_void_p]
        function.restype = c.c_int
    winsock.WSAWaitForMultipleEvents.argtypes = [c.c_uint32, c.c_void_p, c.c_int,
                                                c.c_uint32, c.c_int]
    winsock.WSAWaitForMultipleEvents.restype = c.c_uint32
    kernel.CreateEventW.argtypes = [c.c_void_p, c.c_int, c.c_int, c.c_wchar_p]
    kernel.CreateEventW.restype = c.c_void_p
    kernel.WaitForSingleObject.argtypes = [c.c_void_p, c.c_uint32]
    kernel.WaitForSingleObject.restype = c.c_uint32
    for name in ("SetEvent", "ResetEvent", "CloseHandle"):
        function = getattr(kernel, name)
        function.argtypes = [c.c_void_p]
        function.restype = c.c_int
    kernel.VirtualAlloc.argtypes = [c.c_void_p, c.c_size_t, c.c_uint32, c.c_uint32]
    kernel.VirtualAlloc.restype = c.c_void_p
    kernel.VirtualFree.argtypes = [c.c_void_p, c.c_size_t, c.c_uint32]
    kernel.VirtualFree.restype = c.c_int

    output = Path(__file__).resolve().parents[1] / "research/wsa-events-windows-oracle.json"
    result = {"windows_version": platform.version(), "pointer_bits": c.sizeof(c.c_void_p) * 8,
              "calls": [], "invalid": [], "cleanup": []}
    owned = set()
    started = False
    worker = None
    worker_event = None
    noaccess = None

    def save():
        output.write_text(json.dumps(result, indent=2) + "\n")

    def call(label, function, *args, group="calls"):
        c.set_last_error(0x1234)
        row = {"label": label}
        try:
            row["result"] = function(*args)
        except OSError as error:
            row["exception"] = str(error)
        row["win32_last_error"] = c.get_last_error()
        row["wsa_last_error"] = winsock.WSAGetLastError()
        result[group].append(row)
        save()
        return row

    def create(label):
        row = call(label, winsock.WSACreateEvent)
        handle = row.get("result")
        if handle:
            owned.add(handle)
        assert handle, row
        return handle

    def wait(label, handles, wait_all=False, timeout=0, alertable=False, group="calls"):
        values = (c.c_void_p * len(handles))(*handles)
        return call(label, winsock.WSAWaitForMultipleEvents, len(handles), values,
                    wait_all, timeout, alertable, group=group)

    def close(label, handle):
        row = call(label, winsock.WSACloseEvent, handle)
        if row.get("result"):
            owned.discard(handle)
        return row

    save()
    try:
        startup_data = (c.c_uint8 * 512)()
        startup = call("WSAStartup_2_2", winsock.WSAStartup, 0x0202, startup_data)
        assert startup["result"] == 0
        started = True
        result["startup_version_words"] = list((c.c_uint16 * 2).from_buffer(startup_data))
        first = create("create_first")
        second = create("create_second")
        assert wait("initial_timeout", [first])["result"] == 258
        assert call("set_first", winsock.WSASetEvent, first)["result"]
        assert wait("set_wait_1", [first])["result"] == 0
        assert wait("set_wait_2_manual_persists", [first])["result"] == 0
        assert call("reset_first", winsock.WSAResetEvent, first)["result"]
        assert wait("reset_timeout", [first])["result"] == 258
        assert call("set_second", winsock.WSASetEvent, second)["result"]
        assert wait("wait_any_second_index", [first, second])["result"] == 1
        assert wait("wait_any_second_repeated", [first, second])["result"] == 1
        assert wait("wait_all_one_missing", [first, second], True)["result"] == 258
        assert call("set_first_for_all", winsock.WSASetEvent, first)["result"]
        assert wait("wait_all_both_set", [first, second], True)["result"] == 0
        assert wait("wait_all_repeated_manual_persists", [first, second], True)["result"] == 0
        assert wait("wait_any_both_lowest_index", [first, second])["result"] == 0
        assert call("reset_first_for_alertable", winsock.WSAResetEvent, first)["result"]
        assert wait("alertable_timeout_no_apc", [first], alertable=True)["result"] == 258
        assert call("set_first_alertable", winsock.WSASetEvent, first)["result"]
        assert wait("alertable_signal_no_apc", [first], alertable=True)["result"] == 0

        worker_event = create("create_worker_event")
        ready = threading.Event()
        worker_state = {}

        def block():
            handles = (c.c_void_p * 1)(worker_event)
            c.set_last_error(0x1234)
            ready.set()
            begin = time.monotonic()
            value = winsock.WSAWaitForMultipleEvents(1, handles, False, 2000, False)
            worker_state.update(result=value, elapsed_seconds=time.monotonic() - begin,
                                win32_last_error=c.get_last_error(),
                                wsa_last_error=winsock.WSAGetLastError())

        worker = threading.Thread(target=block)
        worker.start()
        assert ready.wait(1)
        time.sleep(0.03)
        result["worker_was_blocked_before_signal"] = worker.is_alive()
        assert call("signal_blocking_worker", winsock.WSASetEvent, worker_event)["result"]
        worker.join(3)
        result["worker_wait"] = worker_state
        assert not worker.is_alive() and worker_state["result"] == 0

        for label, handle in (("null", None), ("random", 0x12345678)):
            for name in ("WSASetEvent", "WSAResetEvent", "WSACloseEvent"):
                call(name + "_" + label, getattr(winsock, name), handle, group="invalid")
            wait("wait_invalid_event_" + label, [handle], group="invalid")
        valid = (c.c_void_p * 65)(*([first] * 65))
        for count in (0, 65):
            call("wait_invalid_count_" + str(count), winsock.WSAWaitForMultipleEvents,
                 count, valid, False, 0, False, group="invalid")
        call("wait_null_handles_count1", winsock.WSAWaitForMultipleEvents,
             1, None, False, 0, False, group="invalid")
        noaccess = kernel.VirtualAlloc(None, 4096, 0x3000, 0x01)
        assert noaccess
        call("wait_own_PAGE_NOACCESS_handles", winsock.WSAWaitForMultipleEvents,
             1, noaccess, False, 0, False, group="invalid")
        temporary = create("create_for_double_close")
        assert close("close_owned_event", temporary)["result"]
        call("close_already_closed_event", winsock.WSACloseEvent, temporary, group="invalid")

        assert call("win32_wait_wsa_event", kernel.WaitForSingleObject, first, 0)["result"] == 0
        assert call("win32_reset_wsa_event", kernel.ResetEvent, first)["result"]
        assert wait("wsa_wait_after_win32_reset", [first])["result"] == 258
        native_event = kernel.CreateEventW(None, True, False, None)
        assert native_event
        owned.add(native_event)
        assert call("wsa_set_win32_created_event", winsock.WSASetEvent, native_event)["result"]
        assert call("win32_wait_win32_event_after_wsa_set", kernel.WaitForSingleObject,
                    native_event, 0)["result"] == 0
        assert wait("wsa_wait_win32_created_event", [native_event])["result"] == 0
        assert close("wsa_close_win32_created_event", native_event)["result"]
        call("win32_wait_after_wsa_close", kernel.WaitForSingleObject, native_event, 0, group="invalid")

        # Plain imports avoid ctypes' private LastError swap for this direct
        # verification that WinSock and Win32 use the same underlying slot.
        plain_kernel = c.WinDLL("kernel32", use_last_error=False)
        plain_winsock = c.WinDLL("ws2_32", use_last_error=False)
        plain_kernel.SetLastError.argtypes = [c.c_uint32]
        plain_kernel.SetLastError.restype = None
        plain_kernel.GetLastError.restype = c.c_uint32
        plain_winsock.WSASetLastError.argtypes = [c.c_int]
        plain_winsock.WSASetLastError.restype = None
        plain_winsock.WSAGetLastError.restype = c.c_int
        plain_kernel.SetLastError(0x1234)
        from_win32 = [plain_kernel.GetLastError(), plain_winsock.WSAGetLastError()]
        plain_winsock.WSASetLastError(10042)
        from_wsa = [plain_kernel.GetLastError(), plain_winsock.WSAGetLastError()]
        result["shared_last_error_slot"] = {"Win32Set_0x1234_then_GetWin32_GetWSA": from_win32,
                                            "WSASet_10042_then_GetWin32_GetWSA": from_wsa}
        save()
    finally:
        if worker is not None and worker.is_alive():
            if worker_event:
                winsock.WSASetEvent(worker_event)
            worker.join(3)
        for handle in list(owned):
            success = winsock.WSACloseEvent(handle)
            result["cleanup"].append({"kind": "event", "success": bool(success),
                                       "error": winsock.WSAGetLastError()})
            if success:
                owned.remove(handle)
        if noaccess:
            result["noaccess_page_freed"] = bool(kernel.VirtualFree(noaccess, 0, 0x8000))
        if started:
            c.set_last_error(0x1234)
            result["WSACleanup"] = {"result": winsock.WSACleanup(),
                                      "win32_last_error": c.get_last_error()}
        result["all_owned_events_closed"] = not owned
        result["worker_joined"] = worker is None or not worker.is_alive()
        result["scope"] = "Own isolated manual-reset events only; WSAStartup2.2/WSACleanup paired; no sockets or external network traffic."
        result["sources"] = [
            "https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-wsacreateevent",
            "https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-wsawaitformultipleevents",
            "https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-wsacloseevent",
        ]
        save()
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
