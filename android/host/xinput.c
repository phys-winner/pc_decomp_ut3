/* Touch movement and a physical gamepad (input.c) enter the original XInput
 * consumer in slot zero; its rumble goes back to the gamepad. */
#include "host.h"

extern int void_input_touch_pad(uint32_t index, uint8_t out[16]);
extern int void_input_pad_connected(uint32_t index);
extern void void_input_pad_rumble(uint32_t index, uint16_t left, uint16_t right);

static uint32_t xinput_disconnected(void)
{
    if (ARG(0) > 3 || !ARG(1)) return 160; /* ERROR_BAD_ARGUMENTS; preserves LastError. */
    /* A disconnected slot never writes its XINPUT_STATE. Native XInput1_3
     * validates these arguments first and clears LastError on this path. */
    void_set_last_error(0);
    return 1167; /* ERROR_DEVICE_NOT_CONNECTED */
}
WINAPI_ORDINAL("xinput1_3.dll", XInputGetState, 2, 2)
{
    if (ARG(0) > 3 || !ARG(1)) return 160;
    uint8_t state[16];
    if (!void_input_touch_pad(ARG(0), state)) return xinput_disconnected();
    if (!void_vm_writable(ARG(1), 16)) return 998;
    memcpy(GPTR(ARG(1)), state, 16);
    void_set_last_error(0);
    return 0;
}
WINAPI_ORDINAL("xinput1_3.dll", XInputSetState, 3, 2)
{
    if (ARG(0) > 3 || !ARG(1)) return 160;
    if (!void_input_pad_connected(ARG(0))) return xinput_disconnected();
    if (!void_vm_readable(ARG(1), 4)) return 998;
    /* XINPUT_VIBRATION: wLeftMotorSpeed, wRightMotorSpeed. The Android side
     * drives the physical gamepad's motors with them, never the phone's. */
    void_input_pad_rumble(ARG(0), MEM16(ARG(1)), MEM16(ARG(1) + 2));
    void_set_last_error(0);
    return 0;
}
