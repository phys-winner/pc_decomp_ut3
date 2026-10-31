/* Production monitor geometry, real user32 windows, shared metrics and x86 ABI. */
#include "../android/host/user32.c"
#include <assert.h>
#include <unistd.h>

void void_process_exit(uint32_t code, int detach) { (void)detach; _exit((int)code); }
static void test_wndproc(void) { g_eax = 1; g_esp += 20; }

static uint32_t api(const char *name, unsigned count, const uint32_t *args)
{
    const void_shim *shim = void_shim_find("user32.dll", name, 0); assert(shim);
    uint32_t before = g_esp;
    uint32_t result = void_call_guest(void_thunk(shim->fn, name), count, args, 1);
    assert(g_esp == before);
    return result;
}

static uint32_t rect_query(uint32_t rect, int32_t l, int32_t t, int32_t r, int32_t b, uint32_t flags)
{
    MEM32(rect) = l; MEM32(rect + 4) = t; MEM32(rect + 8) = r; MEM32(rect + 12) = b;
    uint32_t args[2] = {rect, flags};
    return api("MonitorFromRect", 2, args);
}

int main(void)
{
    assert(void_guest_reserve());
    void_threads_init(); void_dispatch_init(); void_machine_enter();
    void_input_set_screen_size(1280, 720);
    const uint32_t monitor = 0x00070001; /* Existing D3D9 GetAdapterMonitor contract. */
    static const int32_t points[][3] = {{0,0,1}, {1279,719,1}, {1280,0,0}, {0,720,0}, {-1,1,0}, {INT32_MIN,INT32_MAX,0}};
    for (unsigned i = 0; i < sizeof points / sizeof *points; ++i) {
        for (uint32_t flags = 0; flags < 3; ++flags) {
            uint32_t args[3] = {points[i][0], points[i][1], flags};
            void_set_last_error(0x1234);
            assert(api("MonitorFromPoint", 3, args) == (points[i][2] || flags ? monitor : 0));
            assert(void_get_last_error() == 0x1234);
        }
    }
    uint32_t rect = void_galloc(16);
    assert(rect_query(rect, -10, 10, 1, 20, 0) == monitor);
    assert(!rect_query(rect, -10, 10, 0, 20, 0));
    assert(!rect_query(rect, 1280, 10, 1400, 20, 0));
    assert(rect_query(rect, 1279, 10, 1400, 20, 0) == monitor);
    assert(rect_query(rect, 10, 10, 10, 10, 0) == monitor);
    assert(rect_query(rect, 20, 20, 10, 10, 0) == monitor);
    assert(!rect_query(rect, 2000, 2000, 10, 10, 0));
    assert(!rect_query(rect, -10, 10, 10, 10, 0));
    assert(rect_query(rect, 2000, 2000, 2100, 2100, 2) == monitor);
    assert(!rect_query(rect, 0, 0, 10, 10, 3) && void_get_last_error() == 1004);

    uint32_t name = void_gstrdup("UT3MonitorTest"), wc = void_galloc(40);
    void_dispatch_add(0x7FFE9000, test_wndproc);
    MEM32(wc + 4) = 0x7FFE9000; MEM32(wc + 36) = name;
    assert(api("RegisterClassA", 1, &wc));
    uint32_t create[12] = {0, name, name, 0, 20, 30, 100, 80, 0, 0, 0, 0};
    uint32_t hwnd = api("CreateWindowExA", 12, create); assert(hwnd);
    uint32_t from_window[2] = {hwnd, 0};
    assert(api("MonitorFromWindow", 2, from_window) == monitor);
    uint32_t move[7] = {hwnd, 0, 2000, 2000, 100, 80, 0};
    assert(api("SetWindowPos", 7, move));
    assert(!api("MonitorFromWindow", 2, from_window));
    from_window[1] = 1; assert(api("MonitorFromWindow", 2, from_window) == monitor);
    from_window[0] = 0; from_window[1] = 0; assert(!api("MonitorFromWindow", 2, from_window));
    from_window[1] = 2; assert(api("MonitorFromWindow", 2, from_window) == monitor);
    from_window[0] = 0x12345678;
    assert(!api("MonitorFromWindow", 2, from_window) && void_get_last_error() == 1400);
    from_window[1] = 3;
    assert(!api("MonitorFromWindow", 2, from_window) && void_get_last_error() == 1004);

    uint32_t allocation = void_galloc(116), out = allocation + 4;
    const char *names[] = {"GetMonitorInfoA", "GetMonitorInfoW"};
    static const char device[] = "\\\\.\\DISPLAY1";
    for (unsigned wide = 0; wide < 2; ++wide) {
        const uint32_t sizes[] = {40, wide ? 104 : 72};
        for (unsigned i = 0; i < 2; ++i) {
            memset(GPTR(allocation), 0xA5, 116); MEM32(out) = sizes[i];
            uint32_t args[2] = {monitor, out};
            void_set_last_error(0x1234);
            assert(api(names[wide], 2, args) && void_get_last_error() == 0x1234);
            assert(MEM32(out) == sizes[i] && MEM32(out + 36) == 1);
            for (unsigned r = 4; r <= 20; r += 16)
                assert(!MEM32(out + r) && !MEM32(out + r + 4) && MEM32(out + r + 8) == 1280 && MEM32(out + r + 12) == 720);
            uint32_t written = 40;
            if (i) {
                for (unsigned c = 0; c < sizeof device; ++c)
                    assert((wide ? MEM16(out + 40 + 2*c) : MEM8(out + 40 + c)) == (uint8_t)device[c]);
                written += sizeof device * (wide ? 2 : 1);
            }
            for (uint32_t byte = written; byte < 108; ++byte) assert(MEM8(out + byte) == 0xA5);
            assert(MEM32(allocation) == 0xA5A5A5A5 && MEM32(allocation + 112) == 0xA5A5A5A5);
        }
        const uint32_t bad_sizes[] = {0, 39, 41, wide ? 72 : 104, 108};
        for (unsigned i = 0; i < 5; ++i) {
            memset(GPTR(out), 0xA5, 108); MEM32(out) = bad_sizes[i];
            uint8_t before[108]; memcpy(before, GPTR(out), 108);
            uint32_t args[2] = {monitor, out};
            assert(!api(names[wide], 2, args) && void_get_last_error() == 87);
            assert(!memcmp(before, GPTR(out), 108));
        }
        uint32_t args[2] = {0, out};
        assert(!api(names[wide], 2, args) && void_get_last_error() == 1461);
        args[0] = monitor; args[1] = 0;
        assert(!api(names[wide], 2, args) && void_get_last_error() == 998);
    }
    void_input_set_screen_size(1920, 1080);
    MEM32(out) = 40; uint32_t args[2] = {monitor, out};
    assert(api("GetMonitorInfoW", 2, args) && MEM32(out + 12) == 1920 && MEM32(out + 16) == 1080);
    uint32_t metric = 0; assert(api("GetSystemMetrics", 1, &metric) == 1920);
    metric = 1; assert(api("GetSystemMetrics", 1, &metric) == 1080);
    metric = 80; assert(api("GetSystemMetrics", 1, &metric) == 1);
    void_gfree(allocation); void_gfree(wc); void_gfree(name); void_gfree(rect);
    printf("PASS single monitor D3D9-token consistency, real-window/point/rect geometry, defaults/edges/invalid flags, A/W40/72/104, dynamic metrics, guards and POINT stdcall\n");
    return 0;
}
