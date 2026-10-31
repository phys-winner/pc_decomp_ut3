/* Real guest stdcall and explicit unavailable-service semantics. */
#include "../android/host/hooks.c"
#include <assert.h>
#include <unistd.h>

void void_process_exit(uint32_t code, int detach) { (void)detach; _exit((int)code); }

static uint32_t invoke(const char *name, unsigned count, const uint32_t *args)
{
    const void_shim *shim = void_shim_find("user32.dll", name, 0);
    assert(shim);
    uint32_t before = g_esp;
    void_set_last_error(0x1234);
    uint32_t result = void_call_guest(void_thunk(shim->fn, name), count, args, 1);
    assert(g_esp == before);
    return result;
}

int main(void)
{
    assert(void_guest_reserve());
    void_threads_init(); void_dispatch_init(); void_machine_enter();
    const uint32_t install[] = {13, 0x00A0A370, 0x00400000, 0};
    assert(!invoke("SetWindowsHookExA", 4, install) && void_get_last_error() == 50);
    assert(!invoke("SetWindowsHookExW", 4, install) && void_get_last_error() == 50);
    const uint32_t handles[] = {0, 0x12345678, UINT32_MAX};
    for (unsigned h = 0; h < 3; ++h) {
        assert(!invoke("UnhookWindowsHookEx", 1, handles + h));
        assert(void_get_last_error() == 1404);
        for (int code = -1; code <= 1; ++code) {
            const uint32_t next[] = {handles[h], (uint32_t)code, 0, 0};
            assert(!invoke("CallNextHookEx", 4, next));
            assert(void_get_last_error() == 0x1234);
        }
    }
    puts("PASS unavailable desktop hooks: no synthetic handles; native empty-chain/error semantics and guest stdcall");
    void_machine_leave();
    return 0;
}
