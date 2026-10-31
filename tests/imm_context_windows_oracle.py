"""IME context queries on one own hidden window; restore association in finally."""
import ctypes as c
import json
import platform
from pathlib import Path


def main():
    user = c.WinDLL("user32", use_last_error=True)
    imm = c.WinDLL("imm32", use_last_error=True)
    user.CreateWindowExW.argtypes = [c.c_uint32, c.c_wchar_p, c.c_wchar_p, c.c_uint32,
                                    c.c_int, c.c_int, c.c_int, c.c_int, c.c_void_p,
                                    c.c_void_p, c.c_void_p, c.c_void_p]
    user.CreateWindowExW.restype = c.c_void_p
    user.DestroyWindow.argtypes = [c.c_void_p]
    user.DestroyWindow.restype = c.c_int
    user.IsWindowVisible.argtypes = [c.c_void_p]
    user.IsWindowVisible.restype = c.c_int
    imm.ImmGetContext.argtypes = [c.c_void_p]
    imm.ImmGetContext.restype = c.c_void_p
    imm.ImmReleaseContext.argtypes = [c.c_void_p, c.c_void_p]
    imm.ImmReleaseContext.restype = c.c_int
    imm.ImmAssociateContext.argtypes = [c.c_void_p, c.c_void_p]
    imm.ImmAssociateContext.restype = c.c_void_p
    imm.ImmGetOpenStatus.argtypes = [c.c_void_p]
    imm.ImmGetOpenStatus.restype = c.c_int
    output = Path(__file__).resolve().parents[1] / "research/imm-context-windows-oracle.json"
    result = {"windows_version": platform.version(), "pointer_bits": c.sizeof(c.c_void_p) * 8,
              "calls": [], "finally": {}}
    window = None
    previous = None
    association_changed = False

    def save():
        output.write_text(json.dumps(result, indent=2) + "\n")

    def call(label, fn, *args):
        c.set_last_error(0x1234)
        value = fn(*args)
        row = {"label": label, "result": value, "last_error": c.get_last_error()}
        result["calls"].append(row)
        save()
        return value

    save()
    try:
        # Hidden, nonactivating own tool window; WS_VISIBLE is deliberately absent.
        window = user.CreateWindowExW(0x08000080, "STATIC", "UT3 IMM oracle", 0x00CF0000,
                                      40, 60, 200, 150, None, None, None, None)
        assert window, c.get_last_error()
        assert not user.IsWindowVisible(window)
        normal = call("GetContext_own_hidden_normal", imm.ImmGetContext, window)
        if normal:
            call("GetOpenStatus_own_context", imm.ImmGetOpenStatus, normal)
            assert call("ReleaseContext_normal", imm.ImmReleaseContext, window, normal)
        previous = call("AssociateContext_own_window_NULL", imm.ImmAssociateContext, window, None)
        association_changed = True
        result["previous_association"] = previous
        save()
        disabled = call("GetContext_own_hidden_disabled", imm.ImmGetContext, window)
        if disabled:
            call("ReleaseContext_unexpected_disabled_context", imm.ImmReleaseContext, window, disabled)
        call("GetContext_NULL_hwnd", imm.ImmGetContext, None)
        call("GetContext_invalid_hwnd", imm.ImmGetContext, 0x123456)
    finally:
        if window:
            if association_changed:
                c.set_last_error(0x1234)
                replaced = imm.ImmAssociateContext(window, previous)
                result["finally"]["restore_association"] = {"previous_replaced": replaced,
                                                             "last_error": c.get_last_error()}
                c.set_last_error(0x1234)
                restored = imm.ImmGetContext(window)
                result["finally"]["restored_context"] = restored
                result["finally"]["context_matches_previous"] = restored == previous
                if restored:
                    result["finally"]["release_restored_context"] = imm.ImmReleaseContext(window, restored)
            result["finally"]["window_still_hidden"] = not bool(user.IsWindowVisible(window))
            result["finally"]["destroy_window"] = bool(user.DestroyWindow(window))
        result["scope"] = (
            "Only own hidden nonactivating STATIC window. No focus, foreground, keyboard, "
            "input-language or global IME setting change. Association restored before DestroyWindow.")
        result["sources"] = [
            "https://learn.microsoft.com/en-us/windows/win32/api/imm/nf-imm-immgetcontext",
            "https://learn.microsoft.com/en-us/windows/win32/api/imm/nf-imm-immassociatecontext",
            "https://learn.microsoft.com/en-us/windows/win32/api/imm/nf-imm-immreleasecontext",
        ]
        save()
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
