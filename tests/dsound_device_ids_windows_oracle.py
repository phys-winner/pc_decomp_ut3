"""Read-only DirectSound GetDeviceID/playback enumeration; no device opening."""
import ctypes as c
import hashlib
import json
import platform
import uuid
from pathlib import Path


def main():
    dll_path = Path(r"C:\Windows\System32\dsound.dll")
    ds = c.WinDLL(str(dll_path), use_last_error=True)
    k = c.WinDLL("kernel32", use_last_error=True)
    k.GetACP.restype = c.c_uint32
    codepage = k.GetACP()
    ds.GetDeviceID.argtypes = [c.c_void_p, c.c_void_p]
    ds.GetDeviceID.restype = c.c_uint32
    output = Path(__file__).resolve().parents[1] / "research/dsound-device-ids-windows-oracle.json"
    result = {"windows_version": platform.version(), "pointer_bits": c.sizeof(c.c_void_p) * 8,
              "dll": str(dll_path), "dll_sha256": hashlib.sha256(dll_path.read_bytes()).hexdigest(),
              "ansi_codepage": codepage, "device_ids": [], "enumeration": []}

    def save():
        output.write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    guids = [("DefaultPlayback", "def00000-9c6d-47ed-aaf1-4dda8f2b5c03"),
             ("DefaultCapture", "def00001-9c6d-47ed-aaf1-4dda8f2b5c03"),
             ("VoicePlayback", "def00002-9c6d-47ed-aaf1-4dda8f2b5c03"),
             ("VoiceCapture", "def00003-9c6d-47ed-aaf1-4dda8f2b5c03"),
             ("ordinary_custom", "12345678-9abc-4def-8123-456789abcdef"),
             ("zero_GUID", "00000000-0000-0000-0000-000000000000"),
             ("NULL_source", None)]
    save()
    for name, text in guids:
        raw = uuid.UUID(text).bytes_le if text else None
        source = (c.c_uint8 * 16).from_buffer_copy(raw) if raw else None
        destination = (c.c_uint8 * 32)(*([0xA5] * 32))
        c.set_last_error(0x1234)
        row = {"name": name, "source_guid": text}
        try:
            status = ds.GetDeviceID(c.byref(source) if source is not None else None,
                                    c.addressof(destination) + 8)
            row.update(hresult=status, hresult_hex=f"0x{status:08X}")
        except OSError as error:
            row["exception"] = str(error)
        row["last_error"] = c.get_last_error()
        copied = bytes(destination)[8:24]
        row.update(output_hex=copied.hex(), output_guid=str(uuid.UUID(bytes_le=copied)),
                   output_unchanged=copied == bytes([0xA5] * 16),
                   prefix_guard_unchanged=bytes(destination)[:8] == bytes([0xA5] * 8),
                   suffix_guard_unchanged=bytes(destination)[24:] == bytes([0xA5] * 8))
        result["device_ids"].append(row)
        save()
    for name, text in (("NULL_output_custom", guids[4][1]), ("NULL_output_default", guids[0][1]),
                       ("both_NULL", None)):
        source = (c.c_uint8 * 16).from_buffer_copy(uuid.UUID(text).bytes_le) if text else None
        row = {"name": name, "source_guid": text}
        c.set_last_error(0x1234)
        try:
            status = ds.GetDeviceID(c.byref(source) if source is not None else None, None)
            row.update(hresult=status, hresult_hex=f"0x{status:08X}")
        except OSError as error:
            row["exception"] = str(error)
        row["last_error"] = c.get_last_error()
        result["device_ids"].append(row)
        save()

    cookie = c.c_uint32(0x13579BDF)
    context = c.addressof(cookie)
    for suffix in ("A", "W"):
        text_type = c.c_char_p if suffix == "A" else c.c_wchar_p
        callback_type = c.WINFUNCTYPE(c.c_int, c.c_void_p, text_type, text_type, c.c_void_p)
        for mode in ("TRUE_all", "FALSE_first", "NULL"):
            callbacks = []

            def collect(guid, description, module, passed_context):
                row = {"guid_null": not guid,
                       "guid": str(uuid.UUID(bytes_le=c.string_at(guid, 16))) if guid else None,
                       "context_matches": passed_context == context}
                if suffix == "A":
                    row["description_bytes_hex"] = description.hex() if description is not None else None
                    row["module_bytes_hex"] = module.hex() if module is not None else None
                    row["description"] = description.decode("cp" + str(codepage), errors="replace") if description is not None else None
                    row["module"] = module.decode("cp" + str(codepage), errors="replace") if module is not None else None
                else:
                    row.update(description=description, module=module)
                callbacks.append(row)
                return 0 if mode == "FALSE_first" else 1

            callback = callback_type(collect)
            f = getattr(ds, "DirectSoundEnumerate" + suffix)
            f.argtypes = [c.c_void_p, c.c_void_p]
            f.restype = c.c_uint32
            c.set_last_error(0x1234)
            status = f(None if mode == "NULL" else c.cast(callback, c.c_void_p), context)
            result["enumeration"].append({"api": "DirectSoundEnumerate" + suffix, "mode": mode,
                                           "hresult": status, "hresult_hex": f"0x{status:08X}",
                                           "last_error": c.get_last_error(), "callback_count": len(callbacks),
                                           "callbacks": callbacks})
            save()
    result["scope"] = (
        "GetDeviceID and DirectSoundEnumerateA/W metadata only. No DirectSoundCreate, "
        "DirectSoundCaptureCreate, buffers, playback, microphone recording or device configuration.")
    result["limits"] = (
        "This native inventory exposed only the default NULL-GUID pseudo-device, no actual endpoint GUID. "
        "GetDeviceID returned NODRIVER for all non-NULL source test GUIDs without touching output. "
        "Identity success for an installed real device could not be tested. The documented identity rule "
        "applies to a valid device identifier, not an arbitrary UUID. FALSE_first stopping before a second "
        "endpoint cannot be demonstrated with the observed single callback.")
    result["sources"] = [
        "https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee417724(v=vs.85)",
        "https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee416763(v=vs.85)",
        "https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee417545(v=vs.85)",
    ]
    save()
    print(json.dumps(result, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
