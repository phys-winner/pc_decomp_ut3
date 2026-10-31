/* The current Android game input has no Windows IME/composition backend. */
#include "host.h"

WINAPI_EXPORT("imm32.dll", ImmGetContext, 1)
{
    uint32_t window = ARG(0);
    /* Native NULL HWND and a valid window with no associated context both
     * return NULL without changing LastError. A stale HWND is an error. */
    if (window && !void_window_thread(window)) void_set_last_error(1400); /* ERROR_INVALID_WINDOW_HANDLE */
    return 0;
}

WINAPI_EXPORT("imm32.dll", ImmCreateContext, 0)
{
    /* No native HIMC can be supplied. Original UE3 handles this failure by
     * disabling IME for its viewport and continues ordinary game input. */
    void_set_last_error(ERROR_NOT_SUPPORTED_);
    return 0;
}
