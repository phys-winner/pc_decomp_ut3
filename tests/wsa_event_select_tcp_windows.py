"""Native TCP WSAEventSelect oracle; own loopback sockets and bounded waits."""
import ctypes as c
import json
import platform
import time
from pathlib import Path


def run_tcp():
    w = c.WinDLL("ws2_32", use_last_error=True)
    socket_t = c.c_size_t
    invalid_socket = socket_t(-1).value
    w.WSAStartup.argtypes = [c.c_uint16, c.c_void_p]
    w.WSAStartup.restype = c.c_int
    w.WSACleanup.restype = c.c_int
    w.WSAGetLastError.restype = c.c_int
    w.socket.argtypes = [c.c_int, c.c_int, c.c_int]
    w.socket.restype = socket_t
    w.closesocket.argtypes = [socket_t]
    w.closesocket.restype = c.c_int
    for name in ("bind", "connect"):
        f = getattr(w, name)
        f.argtypes = [socket_t, c.c_void_p, c.c_int]
        f.restype = c.c_int
    w.listen.argtypes = [socket_t, c.c_int]
    w.listen.restype = c.c_int
    w.getsockname.argtypes = [socket_t, c.c_void_p, c.c_void_p]
    w.getsockname.restype = c.c_int
    w.accept.argtypes = [socket_t, c.c_void_p, c.c_void_p]
    w.accept.restype = socket_t
    w.setsockopt.argtypes = [socket_t, c.c_int, c.c_int, c.c_void_p, c.c_int]
    w.setsockopt.restype = c.c_int
    w.ioctlsocket.argtypes = [socket_t, c.c_int32, c.c_void_p]
    w.ioctlsocket.restype = c.c_int
    w.send.argtypes = [socket_t, c.c_void_p, c.c_int, c.c_int]
    w.send.restype = c.c_int
    w.recv.argtypes = [socket_t, c.c_void_p, c.c_int, c.c_int]
    w.recv.restype = c.c_int
    w.shutdown.argtypes = [socket_t, c.c_int]
    w.shutdown.restype = c.c_int
    w.htons.argtypes = [c.c_uint16]
    w.htons.restype = c.c_uint16
    w.ntohs.argtypes = [c.c_uint16]
    w.ntohs.restype = c.c_uint16
    w.WSACreateEvent.restype = c.c_void_p
    w.WSACloseEvent.argtypes = [c.c_void_p]
    w.WSACloseEvent.restype = c.c_int
    w.WSAEventSelect.argtypes = [socket_t, c.c_void_p, c.c_int32]
    w.WSAEventSelect.restype = c.c_int
    w.WSAEnumNetworkEvents.argtypes = [socket_t, c.c_void_p, c.c_void_p]
    w.WSAEnumNetworkEvents.restype = c.c_int
    w.WSAWaitForMultipleEvents.argtypes = [c.c_uint32, c.c_void_p, c.c_int,
                                          c.c_uint32, c.c_int]
    w.WSAWaitForMultipleEvents.restype = c.c_uint32

    class Address(c.Structure):
        _fields_ = [("family", c.c_uint16), ("port", c.c_uint16),
                    ("address", c.c_uint32), ("zero", c.c_uint8 * 8)]

    class NetworkEvents(c.Structure):
        _fields_ = [("mask", c.c_int32), ("errors", c.c_int32 * 10)]

    assert c.sizeof(Address) == 16 and c.sizeof(NetworkEvents) == 44
    result = {"windows_version": platform.version(), "pointer_bits": c.sizeof(c.c_void_p) * 8,
              "network_events_size": 44, "calls": [], "enums": [], "cleanup": []}
    sockets = set()
    events = set()
    started = False

    def call(label, fn, *args):
        c.set_last_error(0x1234)
        begin = time.monotonic()
        value = fn(*args)
        row = {"label": label, "result": value, "error": w.WSAGetLastError(),
               "elapsed_seconds": time.monotonic() - begin}
        result["calls"].append(row)
        return row

    def create_socket(label):
        row = call(label, w.socket, 2, 1, 6)
        s = row["result"]
        assert s != invalid_socket, row
        sockets.add(s)
        return s

    def close_socket(label, s):
        row = call(label, w.closesocket, s)
        if row["result"] == 0:
            sockets.remove(s)
        assert row["result"] == 0, row

    def event(label):
        row = call(label, w.WSACreateEvent)
        e = row["result"]
        assert e, row
        events.add(e)
        return e

    def address(port=0):
        return Address(2, w.htons(port), 0x0100007F)

    def bound_socket(label):
        s = create_socket(label)
        a = address()
        assert call(label + "_bind", w.bind, s, c.byref(a), 16)["result"] == 0
        size = c.c_int(16)
        assert call(label + "_getsockname", w.getsockname, s, c.byref(a), c.byref(size))["result"] == 0
        return s, a

    def select(label, s, e, mask):
        row = call(label, w.WSAEventSelect, s, e, mask)
        assert row["result"] == 0, row

    def wait(label, e, timeout=1000):
        handles = (c.c_void_p * 1)(e)
        return call(label, w.WSAWaitForMultipleEvents, 1, handles, False, timeout, False)

    def enum(label, s, e):
        data = NetworkEvents(0x5A5A5A5A, (c.c_int32 * 10)(*([0x5A5A5A5A] * 10)))
        row = call(label, w.WSAEnumNetworkEvents, s, e, c.byref(data))
        row = {**row, "mask": data.mask, "errors": list(data.errors),
               "changed_error_slots": [i for i, value in enumerate(data.errors) if value != 0x5A5A5A5A]}
        result["enums"].append(row)
        assert row["result"] == 0, row
        return row

    def receive(label, s, count):
        data = (c.c_uint8 * count)()
        row = call(label, w.recv, s, data, count, 0)
        row["bytes_hex"] = bytes(data)[:max(row["result"], 0)].hex()
        return row

    try:
        data = (c.c_uint8 * 512)()
        assert call("WSAStartup_2_2", w.WSAStartup, 0x0202, data)["result"] == 0
        started = True
        listener, local = bound_socket("listener")
        result["loopback_port"] = w.ntohs(local.port)
        assert call("listen", w.listen, listener, 4)["result"] == 0
        listener_event = event("listener_event")
        # FD_READ|FD_ACCEPT|FD_CLOSE also lets the accepted socket reveal
        # inherited selection without calling WSAEventSelect on it ourselves.
        select("select_listener_READ_ACCEPT_CLOSE", listener, listener_event, 1 | 8 | 32)
        client = create_socket("client")
        client_event = event("client_event")
        select("select_client_CONNECT_CLOSE", client, client_event, 16 | 32)
        connect = call("connect_success_start", w.connect, client, c.byref(local), 16)
        assert connect["result"] == 0 or connect["error"] == 10035, connect
        assert wait("connect_success_wait", client_event)["result"] == 0
        result["connect_success"] = enum("connect_success_enum", client, client_event)
        assert wait("accept_notification_wait", listener_event)["result"] == 0
        result["accept_notification"] = enum("accept_notification_enum", listener, listener_event)
        accepted_row = call("accept_native", w.accept, listener, None, None)
        accepted = accepted_row["result"]
        assert accepted != invalid_socket, accepted_row
        sockets.add(accepted)
        # A timeout bounds this test even if inherited nonblocking were absent.
        timeout = c.c_uint32(100)
        assert call("accepted_set_recv_timeout", w.setsockopt, accepted, 0xFFFF, 0x1006,
                    c.byref(timeout), 4)["result"] == 0
        result["accepted_empty_recv_inherited_mode"] = receive("accepted_empty_recv", accepted, 1)
        disable_nonblock = c.c_uint32(0)
        result["accepted_disable_nonblock_while_selected"] = call(
            "accepted_FIONBIO_zero", w.ioctlsocket, accepted, 0x8004667E, c.byref(disable_nonblock))
        if result["accepted_disable_nonblock_while_selected"]["result"] == 0:
            enable = c.c_uint32(1)
            assert call("accepted_restore_nonblock", w.ioctlsocket, accepted, 0x8004667E,
                        c.byref(enable))["result"] == 0
        payload = c.create_string_buffer(b"abcdef")
        assert call("send_six_bytes", w.send, client, payload, 6, 0)["result"] == 6
        assert call("shutdown_peer_send", w.shutdown, client, 1)["result"] == 0
        close_socket("close_peer_after_data", client)
        time.sleep(0.03)
        result["before_drain_wait"] = wait("before_drain_wait", listener_event)
        result["before_drain"] = enum("before_drain_enum_accepted_inherited_event", accepted, listener_event)
        result["partial_drain_recv"] = receive("partial_drain_recv2", accepted, 2)
        result["after_partial_wait"] = wait("after_partial_drain_wait", listener_event)
        result["after_partial_drain"] = enum("after_partial_drain_enum", accepted, listener_event)
        result["final_drain_recv"] = receive("final_drain_recv", accepted, 64)
        result["after_final_wait"] = wait("after_final_drain_wait", listener_event, 300)
        result["after_final_drain"] = enum("after_final_drain_enum", accepted, listener_event)
        result["eof_recv"] = receive("recv_eof", accepted, 64)
        result["after_eof_wait"] = wait("after_eof_wait", listener_event, 100)
        result["after_eof"] = enum("after_eof_enum", accepted, listener_event)

        reserved, unused = bound_socket("refused_port_reservation")
        result["refused_port"] = w.ntohs(unused.port)
        close_socket("close_refused_port_reservation", reserved)
        refused = create_socket("refused_client")
        refused_event = event("refused_event")
        select("select_refused_CONNECT_CLOSE", refused, refused_event, 16 | 32)
        result["refused_connect_start"] = call("connect_refused_start", w.connect, refused, c.byref(unused), 16)
        result["refused_wait"] = wait("refused_connect_wait", refused_event, 6000)
        result["connect_refused"] = enum("refused_connect_enum", refused, refused_event)
        result["refused_wait_after_enum"] = wait("refused_after_enum_wait", refused_event, 0)
        result["refused_second_enum"] = enum("refused_empty_second_enum", refused, refused_event)

        # Separate scenario preserves the READ/CLOSE drain trace above.
        write_case = {}
        result["write_connect_registration"] = write_case
        select("reselect_listener_0x3B", listener, listener_event, 0x3B)
        second_client = create_socket("write_connect_client")
        second_event = event("write_connect_event")
        select("select_client_0x33", second_client, second_event, 0x33)
        start = call("connect_write_connect_start", w.connect, second_client, c.byref(local), 16)
        assert start["result"] == 0 or start["error"] == 10035, start
        assert wait("write_connect_initial_wait", second_event)["result"] == 0
        write_case["client_initial"] = enum("write_connect_initial_enum", second_client, second_event)
        assert wait("write_accept_wait", listener_event)["result"] == 0
        write_case["listener_accept"] = enum("write_listener_accept_enum", listener, listener_event)
        accepted_write_row = call("accept_inherits_write", w.accept, listener, None, None)
        accepted_write = accepted_write_row["result"]
        assert accepted_write != invalid_socket, accepted_write_row
        sockets.add(accepted_write)
        write_case["accepted_initial_wait"] = wait("accepted_inherited_write_wait", listener_event, 300)
        write_case["accepted_initial"] = enum("accepted_inherited_write_enum", accepted_write, listener_event)
        select("reregister_connected_CONNECT_only", second_client, second_event, 16)
        write_case["reregister_connected_wait"] = wait("reregister_connected_wait", second_event, 100)
        write_case["reregister_connected"] = enum("reregister_connected_enum", second_client, second_event)

        # First registration of CONNECT after completion: establish using only
        # WRITE, whose notification supplies a bounded completion check.
        late_client = create_socket("late_connect_registration_client")
        late_event = event("late_connect_registration_event")
        select("late_client_initial_WRITE_only", late_client, late_event, 2)
        start = call("late_client_connect_start", w.connect, late_client, c.byref(local), 16)
        assert start["result"] == 0 or start["error"] == 10035, start
        assert wait("late_client_write_completion_wait", late_event)["result"] == 0
        write_case["late_client_write_completion"] = enum("late_client_write_completion_enum", late_client, late_event)
        select("first_CONNECT_registration_after_completion", late_client, late_event, 16)
        write_case["first_CONNECT_after_completion_wait"] = wait("first_CONNECT_after_completion_wait", late_event, 100)
        write_case["first_CONNECT_after_completion"] = enum("first_CONNECT_after_completion_enum", late_client, late_event)
    finally:
        for s in list(sockets):
            status = w.closesocket(s)
            result["cleanup"].append({"kind": "socket", "status": status, "error": w.WSAGetLastError()})
            if status == 0:
                sockets.remove(s)
        for e in list(events):
            success = w.WSACloseEvent(e)
            result["cleanup"].append({"kind": "event", "success": bool(success), "error": w.WSAGetLastError()})
            if success:
                events.remove(e)
        if started:
            result["WSACleanup"] = w.WSACleanup()
        result["all_owned_sockets_closed"] = not sockets
        result["all_owned_events_closed"] = not events
    return result


if __name__ == "__main__":
    observed = run_tcp()
    output = Path(__file__).resolve().parents[1] / "research/wsa-event-select-tcp-windows.json"
    output.write_text(json.dumps(observed, indent=2) + "\n")
    print(json.dumps(observed, indent=2))
