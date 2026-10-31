/* Production COM mode enumeration -> fullscreen PP -> real GLES backbuffer. */
#include "../android/host/d3d9_device.c"
#include <assert.h>
#include <unistd.h>

void void_process_exit(uint32_t code, int detach) { (void)detach; _exit((int)code); }
int void_android_display_size(int *w, int *h) { *w = 2780; *h = 1264; return 1; }

static uint32_t method(uint32_t object, unsigned slot, unsigned count, const uint32_t *args)
{
    uint32_t before = g_esp;
    uint32_t result = void_call_guest(MEM32(MEM32(object) + slot * 4), count, args, 1);
    assert(g_esp == before);
    return result;
}

static void display(uint32_t parent, uint32_t output, uint32_t width, uint32_t height, uint32_t rate)
{
    uint32_t args[3] = {parent, 0, output};
    assert(method(parent, 8, 3, args) == D3D_OK);
    assert(MEM32(output) == width && MEM32(output + 4) == height);
    assert(MEM32(output + 8) == rate && MEM32(output + 12) == 22);
}

int main(int argc, char **argv)
{
    (void)argv;
    alarm(10);
    assert(void_guest_reserve()); void_threads_init(); void_dispatch_init(); void_machine_enter();
    unsetenv("UT3_RENDERER"); unsetenv("UT3_RENDER_WIDTH"); unsetenv("UT3_RENDER_HEIGHT"); unsetenv("UT3_FPS_LIMIT");
    void_input_set_screen_size(640, 480);
    const void_shim *create = void_shim_find("d3d9.dll", "Direct3DCreate9", 0); assert(create);
    uint32_t sdk = 32, parent = void_call_guest(void_thunk(create->fn, create->name), 1, &sdk, 1); assert(parent);
    uint32_t output = void_galloc(64), pp = void_galloc(56), slot = void_galloc(4);
    display(parent, output, 2780, 1264, 60);
    assert(!setenv("UT3_RENDER_WIDTH", "1056", 1) && !setenv("UT3_RENDER_HEIGHT", "480", 1));
    assert(!setenv("UT3_FPS_LIMIT", "120", 1));
    display(parent, output, 1056, 480, 120);
    const char *invalid[] = {"", "0", "-1", "1056x", "99999999999999999999999999", "8193"};
    for (unsigned i = 0; i < sizeof invalid / sizeof *invalid; ++i) {
        assert(!setenv("UT3_RENDER_WIDTH", invalid[i], 1)); display(parent, output, 2780, 1264, 120);
    }
    assert(!setenv("UT3_RENDER_WIDTH", "1056", 1));
    /* A configured mode already in the table must not be enumerated twice. */
    uint32_t count_args[3] = {parent, 0, 22};
    assert(!setenv("UT3_RENDER_WIDTH", "640", 1));
    assert(method(parent, 6, 3, count_args) == MODE_COUNT);
    assert(!setenv("UT3_RENDER_WIDTH", "1056", 1));
    uint32_t count = method(parent, 6, 3, count_args); assert(count == MODE_COUNT + 1);
    uint32_t matches = 0, chosen_w = 0, chosen_h = 0, dx = UINT32_MAX, dy = UINT32_MAX;
    for (uint32_t i = 0; i < count; ++i) {
        memset(GPTR(output), 0xA5, 32);
        uint32_t args[5] = {parent, 0, 22, i, output + 4};
        assert(method(parent, 7, 5, args) == D3D_OK);
        assert(MEM32(output) == 0xA5A5A5A5 && MEM32(output + 20) == 0xA5A5A5A5);
        uint32_t w = MEM32(output + 4), h = MEM32(output + 8);
        assert(MEM32(output + 12) == 120 && MEM32(output + 16) == 22);
        if (w == 1056 && h == 480) ++matches;
        uint32_t x = (uint32_t)abs((int)w - 1056), y = (uint32_t)abs((int)h - 480);
        /* Exact original 0158B74B/753 + 0158B775/777 admission comparisons. */
        if (x <= dx && y <= dy) { chosen_w = w; chosen_h = h; dx = x; dy = y; }
    }
    assert(matches == 1 && chosen_w == 1056 && chosen_h == 480);
    uint32_t invalid_enum[5] = {parent, 0, 22, count, output};
    memset(GPTR(output), 0xA5, 32);
    assert(method(parent, 7, 5, invalid_enum) == D3DERR_INVALIDCALL && MEM32(output) == 0xA5A5A5A5);
    count_args[1] = 1; assert(!method(parent, 6, 3, count_args));
    count_args[1] = 0; count_args[2] = 99; assert(!method(parent, 6, 3, count_args));
    count_args[2] = 23; assert(method(parent, 6, 3, count_args) == count);
    puts("PASS production GLES COM configured/physical modes, 120Hz virtual rate, bounds, duplicate suppression and original chooser -> 1056x480 PP");
    if (argc > 1) {
        memset(GPTR(pp), 0, 56);
        MEM32(pp) = chosen_w; MEM32(pp + 4) = chosen_h; MEM32(pp + 8) = 22;
        MEM32(pp + 12) = 1; MEM32(pp + 24) = 1; MEM32(pp + 48) = 120; MEM32(pp + 52) = 0x80000000u;
        uint32_t args[7] = {parent, 0, 1, 0, 0x50, pp, slot};
        assert(method(parent, 16, 7, args) == D3D_OK && MEM32(slot));
        uint32_t device = MEM32(slot), back[5] = {device, 0, 0, 0, slot};
        assert(MEM32(pp) == 1056 && MEM32(pp + 4) == 480);
        int w, h; void_input_screen_size(&w, &h); assert(w == 1056 && h == 480);
        assert(method(device, 18, 5, back) == D3D_OK);
        uint32_t surface = MEM32(slot), desc[2] = {surface, output};
        assert(method(surface, 12, 2, desc) == D3D_OK);
        assert(MEM32(output + 24) == 1056 && MEM32(output + 28) == 480);
        method(surface, 2, 1, &surface);
        uint32_t mode[3] = {device, 0, output};
        assert(method(device, 8, 3, mode) == D3D_OK && MEM32(output + 8) == 120);
        assert(MEM32(output) == 1056 && MEM32(output + 4) == 480);
        /* A caller's later explicit resize is still honored, not forced. */
        MEM32(pp) = 800; MEM32(pp + 4) = 600;
        uint32_t reset[2] = {device, pp}; assert(method(device, 16, 2, reset) == D3D_OK);
        void_input_screen_size(&w, &h); assert(w == 800 && h == 600);
        assert(!method(device, 2, 1, &device));
        puts("PASS real GLES pbuffer CreateDevice 1056x480 backbuffer/Input/GetDisplayMode and explicit Reset800x600");
    }
    assert(!method(parent, 2, 1, &parent));
    void_gfree(output); void_gfree(pp); void_gfree(slot);
    return 0;
}
