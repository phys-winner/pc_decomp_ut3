/* Production version exports, Win32 data widths and x86 stack cleanup. */
#include "../android/host/kernel32.c"
#include <assert.h>

typedef struct { uint32_t size, major, minor, build, platform; uint8_t csd[128]; } info_a;
typedef struct { uint32_t size, major, minor, build, platform; uint16_t csd[128]; } info_w;
typedef struct { info_a base; uint16_t sp_major, sp_minor, suite; uint8_t product, reserved; } info_ex_a;
typedef struct { info_w base; uint16_t sp_major, sp_minor, suite; uint8_t product, reserved; } info_ex_w;
_Static_assert(sizeof(info_a) == 148 && sizeof(info_ex_a) == 156, "OSVERSIONINFOA layout");
_Static_assert(sizeof(info_w) == 276 && sizeof(info_ex_w) == 284, "OSVERSIONINFOW layout");

void void_process_exit(uint32_t code, int detach) { (void)detach; _exit((int)code); }

static uint32_t api(const char *name, int count, const uint32_t *args)
{
    const void_shim *shim = void_shim_find("kernel32.dll", name, 0);
    assert(shim);
    uint32_t before = g_esp;
    uint32_t result = void_call_guest(void_thunk(shim->fn, name), count, args, 1);
    assert(g_esp == before);
    return result;
}

int main(void)
{
    assert(void_guest_reserve());
    void_threads_init(); void_dispatch_init(); void_machine_enter();
    uint32_t version = api("GetVersion", 0, NULL);
    assert(version == 0x23F00206u);
    uint32_t allocation = void_galloc(520), out = allocation + 4;
    const uint32_t sizes[] = {0, 4, 20, 147, 148, 149, 155, 156, 157, 275, 276, 277, 283, 284, 285, UINT32_MAX};
    for (int wide = 0; wide <= 1; ++wide) for (unsigned i = 0; i < sizeof sizes / sizeof *sizes; ++i) {
        uint32_t size = sizes[i], base = wide ? 276 : 148;
        memset(GPTR(allocation), 0xA5, 520);
        MEM32(out) = size;
        void_set_last_error(123);
        uint32_t ok = api(wide ? "GetVersionExW" : "GetVersionExA", 1, &out);
        assert(MEM32(out) == size && MEM32(allocation) == 0xA5A5A5A5);
        if (size == base || size == base + 8) {
            assert(ok && void_get_last_error() == 123);
            assert(MEM32(out + 4) == (version & 255) && MEM32(out + 8) == ((version >> 8) & 255));
            assert(MEM32(out + 12) == (version >> 16) && MEM32(out + 16) == 2);
            for (uint32_t byte = 20; byte < base; ++byte) assert(!MEM8(out + byte));
            if (size > base) {
                assert(!MEM16(out + base) && !MEM16(out + base + 2) && !MEM16(out + base + 4));
                assert(MEM8(out + base + 6) == 1 && !MEM8(out + base + 7));
            }
            for (uint32_t byte = size; byte < 516; ++byte) assert(MEM8(out + byte) == 0xA5);
        } else {
            assert(!ok && void_get_last_error() == ERROR_INSUFFICIENT_BUFFER_);
            for (uint32_t byte = 4; byte < 516; ++byte) assert(MEM8(out + byte) == 0xA5);
        }
    }
    void_gfree(allocation); void_machine_leave();
    puts("Version APIs PASS: A148/156 W276/284, GetVersion consistency, UTF-16 CSD, product fields, invalid-size guards, stdcall");
    return 0;
}
