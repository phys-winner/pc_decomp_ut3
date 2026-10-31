/* Process-local SPI settings used by UE3 to save/disable/restore keyboard
 * accessibility hotkeys on focus changes. The Android input bridge does not
 * implement StickyKeys, ToggleKeys or FilterKeys: AVAILABLE and ON stay clear.
 * Disabled-feature preferences may be saved/restored; enabling is unsupported. */
#include "host.h"

static uint32_t accessibility[3][6] = {{8, 0}, {8, 0}, {24, 0, 0, 0, 0, 0}};
static const uint32_t accessibility_masks[3] = {0x1FC, 0x3C, 0x7C};

static uint32_t system_parameters(uint32_t action, uint32_t ui, uint32_t buffer, uint32_t flags)
{
    unsigned index;
    switch (action) {
    case 0x3A: case 0x3B: index = 0; break; /* STICKYKEYS */
    case 0x34: case 0x35: index = 1; break; /* TOGGLEKEYS */
    case 0x32: case 0x33: index = 2; break; /* FILTERKEYS */
    default: void_set_last_error(ERROR_INVALID_PARAMETER_); return 0;
    }
    uint32_t size = accessibility[index][0];
    if (!buffer || !void_vm_readable(buffer, 4)) {
        void_set_last_error(998); /* ERROR_NOACCESS; no unsafe native pointer dereference. */
        return 0;
    }
    uint32_t supplied = MEM32(buffer);
    if (!(action & 1u)) {
        /* Native A/W wrappers reject zero/oversized cbSize without writing.
         * For a nonzero short cbSize or wrong uiParam, they clear the entire
         * fixed-size payload, preserve cbSize/LastError, and return FALSE. */
        if (!supplied || supplied > size) return 0;
        if (!void_vm_writable(buffer, size)) { void_set_last_error(998); return 0; }
        if (supplied != size || (ui && ui != size)) {
            memset(GPTR(buffer + 4), 0, size - 4);
            return 0;
        }
        memcpy(GPTR(buffer), accessibility[index], size);
        return 1;
    }
    if (supplied != size || (ui && ui != size)) return 0;
    if (!void_vm_readable(buffer, size)) { void_set_last_error(998); return 0; }
    /* Profile persistence and system-wide setting broadcasts are outside
     * this process-local model. UE3's observed calls use fWinIni == 0. */
    if (flags) { void_set_last_error(ERROR_NOT_SUPPORTED_); return 0; }
    uint32_t requested = MEM32(buffer + 4);
    if (requested & 1u) { void_set_last_error(ERROR_NOT_SUPPORTED_); return 0; }
    if (requested & ~(accessibility_masks[index] | 2u)) {
        void_set_last_error(ERROR_INVALID_PARAMETER_);
        return 0;
    }
    memcpy(accessibility[index], GPTR(buffer), size);
    accessibility[index][1] &= accessibility_masks[index]; /* AVAILABLE is read-only and false. */
    return 1;
}

WINAPI_EXPORT("user32.dll", SystemParametersInfoA, 4)
{
    return system_parameters(ARG(0), ARG(1), ARG(2), ARG(3));
}
WINAPI_EXPORT("user32.dll", SystemParametersInfoW, 4)
{
    return system_parameters(ARG(0), ARG(1), ARG(2), ARG(3));
}
