"""DirectSound capture enumeration only: no device opening or recording."""
import ctypes as c
import hashlib
import json
import platform
import uuid
from pathlib import Path


def main():
    dll_path = Path(r"C:\Windows\System32\dsound.dll")
    dsound = c.WinDLL(str(dll_path), use_last_error=True)
    kernel = c.WinDLL("kernel32", use_last_error=True)
    kernel.GetACP.restype = c.c_uint32
    codepage = kernel.GetACP()
    cookie = c.c_uint32(0x13579BDF)
    context = c.addressof(cookie)
    output = Path(__file__).resolve().parents[1] / "research/dsound-capture-windows-oracle.json"
    result = {"windows_version": platform.version(), "pointer_bits": c.sizeof(c.c_void_p) * 8,
              "dll": str(dll_path), "dll_sha256": hashlib.sha256(dll_path.read_bytes()).hexdigest(),
              "ansi_codepage": codepage, "calls": []}

    def run(suffix, stop_first=False, null_callback=False):
        rows = []
        text_type = c.c_char_p if suffix == "A" else c.c_wchar_p
        callback_type = c.WINFUNCTYPE(c.c_int, c.c_void_p, text_type, text_type, c.c_void_p)

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
                row["description"] = description
                row["module"] = module
            rows.append(row)
            return 0 if stop_first else 1

        callback = callback_type(collect)
        function = getattr(dsound, "DirectSoundCaptureEnumerate" + suffix)
        function.argtypes = [c.c_void_p, c.c_void_p]
        function.restype = c.c_uint32
        c.set_last_error(0x1234)
        status = function(None if null_callback else c.cast(callback, c.c_void_p), context)
        row = {"api": "DirectSoundCaptureEnumerate" + suffix,
               "callback": "NULL" if null_callback else "FALSE_first" if stop_first else "TRUE_all",
               "hresult": status, "hresult_hex": f"0x{status:08X}",
               "last_error": c.get_last_error(), "callback_count": len(rows), "callbacks": rows}
        result["calls"].append(row)
        output.write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    for suffix in ("A", "W"):
        run(suffix)
        run(suffix, stop_first=True)
        run(suffix, null_callback=True)
    result["scope"] = (
        "Read-only DirectSoundCaptureEnumerate callbacks only. No DirectSoundCaptureCreate, "
        "capture buffers, microphone opening/recording, endpoint enable/disable or device settings.")
    result["zero_inventory_limit"] = (
        "Windows endpoint availability was observed, not changed. Empty-inventory HRESULT behavior "
        "was not forced. Android host may expose zero capture capability without copying this desktop inventory.")
    result["early_stop_limit"] = (
        "This Windows inventory emitted only one NULL-GUID default pseudo-device. FALSE_first returned "
        "DS_OK after that callback, but stopping before a second real endpoint cannot be demonstrated here.")
    result["sources"] = [
        "https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee416761(v=vs.85)",
        "https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee417544(v=vs.85)",
    ]
    output.write_text(json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
