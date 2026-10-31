"""XInput1_3 queries plus zero-stop requests guarded by no-controller checks."""
import ctypes as c
import hashlib
import json
import platform
from pathlib import Path


def main():
    path = Path(r"C:\Windows\System32\xinput1_3.dll")
    xinput = c.WinDLL(str(path), use_last_error=True)
    kernel = c.WinDLL("kernel32", use_last_error=True)
    kernel.GetProcAddress.argtypes = [c.c_void_p, c.c_void_p]
    kernel.GetProcAddress.restype = c.c_void_p
    xinput.XInputGetState.argtypes = [c.c_uint32, c.c_void_p]
    xinput.XInputGetState.restype = c.c_uint32
    xinput.XInputGetCapabilities.argtypes = [c.c_uint32, c.c_uint32, c.c_void_p]
    xinput.XInputGetCapabilities.restype = c.c_uint32
    xinput.XInputSetState.argtypes = [c.c_uint32, c.c_void_p]
    xinput.XInputSetState.restype = c.c_uint32
    state_ordinal = kernel.GetProcAddress(xinput._handle, 2)
    caps_ordinal = kernel.GetProcAddress(xinput._handle, 4)
    set_ordinal = kernel.GetProcAddress(xinput._handle, 3)
    state_type = c.WINFUNCTYPE(c.c_uint32, c.c_uint32, c.c_void_p, use_last_error=True)
    state_by_ordinal = state_type(state_ordinal)
    set_by_ordinal = state_type(set_ordinal)

    class Gamepad(c.Structure):
        _fields_ = [("buttons", c.c_uint16), ("left_trigger", c.c_uint8),
                    ("right_trigger", c.c_uint8), ("thumb_lx", c.c_int16),
                    ("thumb_ly", c.c_int16), ("thumb_rx", c.c_int16), ("thumb_ry", c.c_int16)]

    class State(c.Structure):
        _fields_ = [("packet_number", c.c_uint32), ("gamepad", Gamepad)]

    class Vibration(c.Structure):
        _fields_ = [("left_motor", c.c_uint16), ("right_motor", c.c_uint16)]

    class Capabilities(c.Structure):
        _fields_ = [("type", c.c_uint8), ("subtype", c.c_uint8), ("flags", c.c_uint16),
                    ("gamepad", Gamepad), ("vibration", Vibration)]

    assert c.sizeof(State) == 16 and c.sizeof(Gamepad) == 12 and c.sizeof(Capabilities) == 20
    output = Path(__file__).resolve().parents[1] / "research/xinput-windows-oracle.json"
    result = {"windows_version": platform.version(), "dll": str(path),
              "dll_sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
              "pointer_bits": c.sizeof(c.c_void_p) * 8,
              "state_size": 16, "state_offsets": {"packet_number": 0, "gamepad": 4},
              "gamepad_offsets": {name: getattr(Gamepad, name).offset for name, _ in Gamepad._fields_},
              "capabilities_size": 20,
              "ordinal2_equals_GetState": state_ordinal == c.cast(xinput.XInputGetState, c.c_void_p).value,
              "ordinal4_equals_GetCapabilities": caps_ordinal == c.cast(xinput.XInputGetCapabilities, c.c_void_p).value,
              "ordinal3_equals_SetState": set_ordinal == c.cast(xinput.XInputSetState, c.c_void_p).value,
              "state_calls": [], "capability_calls": [], "zero_stop_calls": []}

    def probe(api, index, size, flags=None, offset=8, null=False, ordinal=False):
        buffer = (c.c_uint8 * (size + 16))(*([0xA5] * (size + 16)))
        pointer = None if null else c.addressof(buffer) + offset
        row = {"index": index, "null_output": null, "offset": offset, "ordinal": ordinal}
        c.set_last_error(0x1234)
        try:
            if api == "state":
                row["result"] = (state_by_ordinal if ordinal else xinput.XInputGetState)(index, pointer)
            else:
                row["flags"] = flags
                row["result"] = xinput.XInputGetCapabilities(index, flags, pointer)
        except OSError as error:
            row["exception"] = str(error)
        row["last_error"] = c.get_last_error()
        if not null:
            raw = bytes(buffer)
            row.update(output_hex=raw[offset:offset + size].hex(),
                       output_unchanged=raw[offset:offset + size] == bytes([0xA5] * size),
                       prefix_guard_unchanged=raw[:offset] == bytes([0xA5] * offset),
                       suffix_guard_unchanged=raw[offset + size:] == bytes([0xA5] * (16 - offset)))
        result["state_calls" if api == "state" else "capability_calls"].append(row)
        output.write_text(json.dumps(result, indent=2) + "\n")

    for index in (0, 1, 2, 3, 4, 0xFFFFFFFF):
        probe("state", index, 16)
        probe("state", index, 16, null=True)
    probe("state", 0, 16, offset=5)
    probe("state", 0, 16, ordinal=True)
    for index in (0, 1, 2, 3, 4, 0xFFFFFFFF):
        for flags in (0, 1):
            probe("caps", index, 20, flags=flags)
        probe("caps", index, 20, flags=0, null=True)
    probe("caps", 0, 20, flags=1, offset=5)
    # Explicitly authorized zero-stop companion only: never send a nonzero
    # motor value, and skip if a fresh query finds any connected controller.
    statuses = []
    for index in range(4):
        state = State()
        statuses.append(xinput.XInputGetState(index, c.byref(state)))
    result["zero_stop_global_preflight_GetState"] = statuses
    if statuses == [1167] * 4:
        for index in (0, 1, 2, 3, 4, 0xFFFFFFFF):
            for null in (False, True):
                row = {"index": index, "null_input": null, "motors": [0, 0]}
                if index < 4:
                    preflight = State()
                    row["immediate_GetState"] = xinput.XInputGetState(index, c.byref(preflight))
                    if row["immediate_GetState"] != 1167:
                        row["skipped"] = "Controller is now connected; no stop request sent."
                        result["zero_stop_calls"].append(row)
                        continue
                buffer = (c.c_uint8 * 20)(*([0xA5] * 20))
                for position in range(8, 12):
                    buffer[position] = 0
                before = bytes(buffer)
                c.set_last_error(0x1234)
                row["result"] = xinput.XInputSetState(index, None if null else c.addressof(buffer) + 8)
                row["last_error"] = c.get_last_error()
                row["input_and_guards_unchanged"] = bytes(buffer) == before
                result["zero_stop_calls"].append(row)
        preflight = State()
        status = xinput.XInputGetState(0, c.byref(preflight))
        if status == 1167:
            zero = Vibration(0, 0)
            c.set_last_error(0x1234)
            status = set_by_ordinal(0, c.byref(zero))
            result["zero_stop_ordinal3"] = {"result": status, "last_error": c.get_last_error(),
                                             "motors": [zero.left_motor, zero.right_motor]}
    else:
        result["zero_stop_skipped"] = "At least one controller is connected; no SetState calls sent."
    result["scope"] = (
        "State/capabilities read-only queries; explicitly authorized zero-stop SetState companion only "
        "on a freshly verified disconnected profile, never nonzero motors. No Enable or device configuration.")
    output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
