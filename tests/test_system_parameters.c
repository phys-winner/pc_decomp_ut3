/* Production shared A/W state, native size quirks, focus lifecycle and bounds. */
#include "../android/host/system_parameters.c"
#include <assert.h>
#include <unistd.h>

void void_process_exit(uint32_t code, int detach) { (void)detach; _exit((int)code); }

static uint32_t spi(const char *name, uint32_t action, uint32_t size, uint32_t buffer, uint32_t flags)
{
    const void_shim *shim = void_shim_find("user32.dll", name, 0); assert(shim);
    uint32_t args[4] = {action, size, buffer, flags}, before = g_esp;
    void_set_last_error(0x1234);
    uint32_t result = void_call_guest(void_thunk(shim->fn, name), 4, args, 1);
    assert(g_esp == before);
    return result;
}

static void guards(uint32_t allocation, uint32_t size)
{
    assert(MEM32(allocation) == 0xA5A5A5A5 && MEM32(allocation + size + 4) == 0xA5A5A5A5);
}

int main(void)
{
    assert(void_guest_reserve());
    void_threads_init(); void_dispatch_init(); void_machine_enter();
    static const uint32_t actions[] = {0x3A, 0x34, 0x32}, sizes[] = {8, 8, 24};
    static const char *names[] = {"SystemParametersInfoA", "SystemParametersInfoW"};
    for (unsigned feature = 0; feature < 3; ++feature) {
        uint32_t size = sizes[feature], get = actions[feature], set = get + 1;
        uint32_t allocation = void_galloc(size + 8), buffer = allocation + 4;
        for (unsigned api = 0; api < 2; ++api) {
            memset(GPTR(allocation), 0xA5, size + 8); MEM32(buffer) = size;
            assert(spi(names[api], get, size, buffer, 0) && void_get_last_error() == 0x1234);
            assert(MEM32(buffer) == size && !MEM32(buffer + 4)); guards(allocation, size);
            /* uiParam zero is accepted. GET ignores fWinIni. */
            assert(spi(names[api], get, 0, buffer, 0xFFFFFFFF)); guards(allocation, size);
            const uint32_t bad_ui[] = {1, size - 1, size + 1, 512};
            for (unsigned i = 0; i < 4; ++i) {
                memset(GPTR(buffer), 0xA5, size); MEM32(buffer) = size;
                assert(!spi(names[api], get, bad_ui[i], buffer, 0) && void_get_last_error() == 0x1234);
                assert(MEM32(buffer) == size);
                for (uint32_t byte = 4; byte < size; ++byte) assert(!MEM8(buffer + byte));
                guards(allocation, size);
            }
            const uint32_t bad_cb[] = {0, 1, 4, size - 1, size + 4};
            for (unsigned i = 0; i < 5; ++i) {
                memset(GPTR(buffer), 0xA5, size); MEM32(buffer) = bad_cb[i];
                assert(!spi(names[api], get, size, buffer, 0) && void_get_last_error() == 0x1234);
                assert(MEM32(buffer) == bad_cb[i]);
                for (uint32_t byte = 4; byte < size; ++byte)
                    assert(MEM8(buffer + byte) == (bad_cb[i] && bad_cb[i] < size ? 0 : 0xA5));
                uint8_t before[24]; memcpy(before, GPTR(buffer), size);
                assert(!spi(names[api], set, size, buffer, 0) && void_get_last_error() == 0x1234);
                assert(!memcmp(before, GPTR(buffer), size)); guards(allocation, size);
            }
            assert(!spi(names[api], get, size, 0, 0) && void_get_last_error() == 998);
            assert(!spi(names[api], set, size, 0, 0) && void_get_last_error() == 998);
        }
        /* A writes disabled configuration; W observes it, focus clears its
         * hotkey/confirmation preferences, then restores the original state. */
        memset(GPTR(buffer), 0, size); MEM32(buffer) = size; MEM32(buffer + 4) = 0xC | 2;
        if (size == 24) { MEM32(buffer + 8) = 100; MEM32(buffer + 12) = 200; MEM32(buffer + 16) = 30; MEM32(buffer + 20) = 40; }
        assert(spi(names[0], set, 0, buffer, 0) && void_get_last_error() == 0x1234);
        memset(GPTR(buffer), 0, size); MEM32(buffer) = size;
        assert(spi(names[1], get, size, buffer, 0) && MEM32(buffer + 4) == 0xC);
        uint8_t saved[24]; memcpy(saved, GPTR(buffer), size);
        MEM32(buffer + 4) &= ~0xCu;
        assert(spi(names[1], set, size, buffer, 0));
        assert(spi(names[0], get, size, buffer, 0) && !MEM32(buffer + 4));
        memcpy(GPTR(buffer), saved, size); assert(spi(names[1], set, size, buffer, 0));
        assert(spi(names[0], get, size, buffer, 0) && !memcmp(saved, GPTR(buffer), size));
        MEM32(buffer + 4) |= 1;
        assert(!spi(names[1], set, size, buffer, 0) && void_get_last_error() == 50);
        assert(spi(names[0], get, size, buffer, 0) && !memcmp(saved, GPTR(buffer), size));
        MEM32(buffer + 4) |= 0x8000;
        assert(!spi(names[1], set, size, buffer, 0) && void_get_last_error() == 87);
        memcpy(GPTR(buffer), saved, size);
        assert(!spi(names[1], set, size, buffer, 3) && void_get_last_error() == 50);
        assert(spi(names[0], get, size, buffer, 0) && !memcmp(saved, GPTR(buffer), size));
        /* Reset this process-local feature for any subsequent test cases. */
        memset(GPTR(buffer + 4), 0, size - 4); assert(spi(names[0], set, size, buffer, 0));
        guards(allocation, size); void_gfree(allocation);
    }
    uint32_t page = void_vm_alloc(0, 0x1000, 0x3000, 4); assert(page);
    uint32_t end_buffer = page + 0x1000 - 24;
    MEM32(end_buffer) = 24;
    assert(spi(names[1], 0x32, 24, end_buffer, 0));
    MEM32(page + 0xFFC) = 24;
    assert(!spi(names[1], 0x32, 24, page + 0xFFC, 0) && void_get_last_error() == 998);
    assert(!spi(names[1], 0x33, 24, page + 0xFFC, 0) && void_get_last_error() == 998);
    assert(MEM32(page + 0xFFC) == 24);
    assert(!spi(names[1], 0xDEADBEEF, 0, 0, 0) && void_get_last_error() == 87);
    printf("PASS shared SPI A/W Sticky/Toggle/Filter 8/8/24, native ui/cb quirks, disabled preferences/focus restore, unsupported enable, bounds/guards and stdcall\n");
    return 0;
}
