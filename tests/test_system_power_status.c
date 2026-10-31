/* Real production power ABI + Android JNI snapshot; deterministic missing-data fixtures. */
#define _GNU_SOURCE
#define UT3_ANDROID_APP
#include "host.h"
#include <assert.h>
#include <errno.h>
static int fixture_mode;
static FILE *power_test_open(const char *path, const char *mode);
#define fopen power_test_open
#include "../android/host/power.c"
#undef fopen
#include "../android/host/android_main.c"

int ut3_game_main(const char *game, const char *data, int selftest)
{ (void)game; (void)data; (void)selftest; return 0; }
void void_process_exit(uint32_t code, int detach) { (void)detach; _exit((int)code); }

static FILE *power_test_open(const char *path, const char *mode)
{
    if (!fixture_mode) return fopen(path, mode);
    if (fixture_mode == 1) { errno = EACCES; return NULL; }
    const char *text = NULL;
    if (strstr(path, "/battery/present")) text = "1\n";
    else if (strstr(path, "/battery/capacity")) text = "83\n";
    else if (strstr(path, "/battery/status")) text = "Charging\n";
    else if (strstr(path, "/usb/online")) text = "1\n";
    if (!text) { errno = ENOENT; return NULL; }
    return fmemopen((void *)text, strlen(text), "r");
}

static uint32_t query(uint32_t out)
{
    const void_shim *shim = void_shim_find("kernel32.dll", "GetSystemPowerStatus", 0); assert(shim);
    uint32_t before = g_esp;
    uint32_t result = void_call_guest(void_thunk(shim->fn, shim->name), 1, &out, 1);
    assert(g_esp == before); return result;
}

static void check(uint32_t out, unsigned ac, unsigned flags, unsigned percent)
{
    void_set_last_error(0x1234); assert(query(out) && void_get_last_error() == 0x1234);
    assert(MEM8(out) == ac && MEM8(out + 1) == flags && MEM8(out + 2) == percent && !MEM8(out + 3));
    assert(MEM32(out + 4) == UINT32_MAX && MEM32(out + 8) == UINT32_MAX);
}

int main(void)
{
    assert(void_guest_reserve()); void_threads_init(); void_dispatch_init(); void_machine_enter();
    uint32_t allocation = void_galloc(20), out = allocation + 1;
    memset(GPTR(allocation), 0xA5, 20);
    assert(query(out));
    assert(MEM8(out) == 0 || MEM8(out) == 1 || MEM8(out) == 255);
    assert(MEM8(out + 2) <= 100 || MEM8(out + 2) == 255);
    printf("actual native sysfs fallback: AC=%u battery=%u percent=%u reserved=%u lifetime=%u/%u\n",
        MEM8(out), MEM8(out + 1), MEM8(out + 2), MEM8(out + 3), MEM32(out + 4), MEM32(out + 8));
    fixture_mode = 1; check(out, 255, 255, 255);
    fixture_mode = 2; check(out, 1, 9, 83);
    struct { int plugged,present,level,scale,status; uint8_t ac,flags,percent; } cases[] = {
        {2,1,76,100,2,1,9,76}, {0,1,3,100,3,0,6,3}, {0,1,32,100,3,0,2,32},
        {0,1,33,100,3,0,0,33}, {0,1,66,100,3,0,0,66}, {0,1,67,100,5,0,1,67},
        {1,1,50,100,2,1,8,50}, {0,0,0,100,1,0,128,255},
        {-1,1,-1,100,1,255,255,255}, {0,1,100,0,1,0,255,255},
        {0,1,1,2,3,0,0,50}, {0,1,101,100,3,0,255,255},
    };
    for (unsigned i = 0; i < sizeof cases / sizeof *cases; ++i) {
        Java_org_ut3_port_GameActivity_nativePower(NULL,NULL,cases[i].plugged,cases[i].present,cases[i].level,cases[i].scale,cases[i].status);
        check(out,cases[i].ac,cases[i].flags,cases[i].percent);
    }
    assert(MEM8(allocation) == 0xA5);
    for (unsigned i = 13; i < 20; ++i) assert(MEM8(allocation + i) == 0xA5);
    assert(!query(0) && void_get_last_error() == 998);
    assert(!query(0x1000) && void_get_last_error() == 998);
    uint32_t page = void_vm_alloc(0, 0x1000, 0x3000, 4); assert(page);
    assert(query(page + 0x1000 - 12));
    memset(GPTR(page + 0x1000 - 11), 0xA5, 11);
    assert(!query(page + 0x1000 - 11) && void_get_last_error() == 998);
    for (unsigned i = 0; i < 11; ++i) assert(MEM8(page + 0x1000 - 11 + i) == 0xA5);
    void_gfree(allocation);
    puts("PASS SYSTEM_POWER_STATUS12 exact/unaligned/guards, real-sysfs fallback, denied-data unknowns, JNI Android facts/thresholds/no battery, reserved0/unknown lifetimes, bounds and stdcall");
    return 0;
}
