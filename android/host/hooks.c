/* Android has no Windows desktop hook chain. Report installation failure so
 * callers keep their ordinary window/DirectInput path without a fake handle. */
#include "host.h"

static uint32_t unavailable_hook(void)
{
    void_set_last_error(ERROR_NOT_SUPPORTED_);
    return 0;
}

WINAPI_EXPORT("user32.dll", SetWindowsHookExA, 4) { return unavailable_hook(); }
WINAPI_EXPORT("user32.dll", SetWindowsHookExW, 4) { return unavailable_hook(); }

WINAPI_EXPORT("user32.dll", UnhookWindowsHookEx, 1)
{
    void_set_last_error(1404); /* ERROR_INVALID_HOOK_HANDLE: no hook was installed. */
    return 0;
}

WINAPI_EXPORT("user32.dll", CallNextHookEx, 4)
{
    /* An empty chain returns zero and preserves LastError, including nCode<0. */
    return 0;
}
