/* Do not replace a disk profile with the defaults loaded before sign-in. */
#include "host.h"

enum { PROFILE_WRITE = 0x0120A900u, PROFILE_LOGIN_STATUS = 0xD4u };
static void_fn profile_write_original;

static void profile_write(void)
{
    if (g_ecx && g_ecx <= UINT32_MAX - PROFILE_LOGIN_STATUS &&
        void_vm_readable(g_ecx + PROFILE_LOGIN_STATUS, 1) &&
        !MEM8(g_ecx + PROFILE_LOGIN_STATUS)) {
        void_log("[saves] refused profile write before sign-in");
        g_eax = 0;
        g_esp += 12; /* Original thiscall BOOL WriteProfileSettings(BYTE, object*), ret 8. */
        return;
    }
    profile_write_original();
}

int void_profiles_init(void)
{
    static const uint8_t expected[] = {0x55,0x8B,0xEC,0x6A,0xFF,0x68,0x23,0xFF,0x6B,0x01};
    if (profile_write_original) return 1;
    void_fn original = void_dispatch_find(PROFILE_WRITE);
    if (!original || !void_vm_readable(PROFILE_WRITE,sizeof expected) ||
        memcmp(GPTR(PROFILE_WRITE),expected,sizeof expected)) {
        void_log("[saves] original profile writer mismatch");
        return 0;
    }
    profile_write_original = original;
    void_dispatch_add(PROFILE_WRITE,profile_write);
    return 1;
}
