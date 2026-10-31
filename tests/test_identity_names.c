/* Production identity API contracts and x86 stdcall, including short buffers. */
#include "../android/host/kernel32.c"
#include <assert.h>

void void_process_exit(uint32_t code, int detach) { (void)detach; _exit((int)code); }

static uint32_t api(const char *dll, const char *name, uint32_t out, uint32_t size)
{
    const void_shim *shim = void_shim_find(dll, name, 0);
    assert(shim);
    uint32_t before = g_esp, args[] = {out, size};
    uint32_t result = void_call_guest(void_thunk(shim->fn, name), 2, args, 1);
    assert(g_esp == before);
    return result;
}

int main(void)
{
    assert(void_guest_reserve());
    void_threads_init(); void_dispatch_init(); void_machine_enter();
    uint32_t allocation = void_galloc(128), out = allocation + 4, size = allocation + 100;
    for (int user = 0; user <= 1; ++user) for (int wide = 0; wide <= 1; ++wide) {
        const char *value = user ? "Player" : "ANDROID";
        const char *name = user ? (wide ? "GetUserNameW" : "GetUserNameA") :
                                  (wide ? "GetComputerNameW" : "GetComputerNameA");
        uint32_t length = (uint32_t)strlen(value), need = length + 1;
        for (uint32_t cap = 0; cap <= need + 2; ++cap) {
            memset(GPTR(allocation), 0xA5, 128);
            MEM32(size) = cap;
            void_set_last_error(123);
            uint32_t result = api(user ? "advapi32.dll" : "kernel32.dll", name, cap ? out : 0, size);
            assert(MEM32(allocation) == 0xA5A5A5A5 && MEM32(size - 4) == 0xA5A5A5A5 && MEM32(size + 4) == 0xA5A5A5A5);
            if (cap < need) {
                assert(!result && void_get_last_error() == (user ? ERROR_INSUFFICIENT_BUFFER_ : 111u));
                assert(MEM32(size) == (user && !wide ? 2 * need : need));
                for (unsigned i = 0; i < 96; ++i) assert(MEM8(out + i) == 0xA5);
            } else {
                assert(result && void_get_last_error() == 123);
                assert(MEM32(size) == length + (user ? 1u : 0u));
                for (uint32_t i = 0; i < length; ++i) {
                    if (wide) assert(MEM16(out + 2 * i) == (uint8_t)value[i]);
                    else assert(MEM8(out + i) == (uint8_t)value[i]);
                }
                if (wide) assert(!MEM16(out + 2 * length));
                else assert(!MEM8(out + length));
                for (uint32_t byte = need * (wide ? 2u : 1u); byte < 96; ++byte) assert(MEM8(out + byte) == 0xA5);
            }
        }
    }
    void_gfree(allocation); void_machine_leave();
    puts("Identity APIs PASS: ComputerName A/W and UserName A/W, exact/small capacities, count/NUL differences, errors111/122, guards, stdcall");
    return 0;
}
