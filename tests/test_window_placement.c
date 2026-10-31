/* Production WINDOWPLACEMENT44 state, real user32 windows and related W APIs. */
#include "../android/host/user32.c"
#include <assert.h>
#include <unistd.h>
typedef struct { uint32_t length, flags, show, minx, miny, maxx, maxy, left, top, right, bottom; } placement32;
_Static_assert(sizeof(placement32) == 44, "Win32 WINDOWPLACEMENT");

void void_process_exit(uint32_t code, int detach) { (void)detach; _exit((int)code); }
static uint32_t last_size_type, last_size_dims, paints;
static void test_wndproc(void)
{
    if (ARG(1) == WM_SIZE) { last_size_type = ARG(2); last_size_dims = ARG(3); }
    if (ARG(1) == WM_PAINT) ++paints;
    g_eax = 1; g_esp += 20;
}

static uint32_t api(const char *name, unsigned count, const uint32_t *args)
{
    const void_shim *shim = void_shim_find("user32.dll", name, 0); assert(shim);
    uint32_t before = g_esp;
    uint32_t result = void_call_guest(void_thunk(shim->fn, name), count, args, 1);
    assert(g_esp == before);
    return result;
}

static placement32 get(uint32_t hwnd, uint32_t buffer)
{
    uint32_t args[2] = {hwnd, buffer};
    assert(api("GetWindowPlacement", 2, args));
    placement32 result; memcpy(&result, GPTR(buffer), sizeof result); return result;
}

static void rect_matches(uint32_t hwnd, uint32_t buffer, int x, int y, int width, int height)
{
    uint32_t args[2] = {hwnd, buffer};
    assert(api("GetWindowRect", 2, args));
    assert((int32_t)MEM32(buffer) == x && (int32_t)MEM32(buffer + 4) == y);
    assert((int32_t)MEM32(buffer + 8) == x + width && (int32_t)MEM32(buffer + 12) == y + height);
}

int main(void)
{
    assert(void_guest_reserve());
    void_threads_init(); void_dispatch_init(); void_machine_enter(); void_input_set_screen_size(1280, 720);
    void_dispatch_add(0x7FFE9000, test_wndproc);
    uint32_t wc = void_galloc(40), name = void_gstrdup("UT3PlacementTest");
    MEM32(wc + 4) = 0x7FFE9000; MEM32(wc + 36) = name; assert(api("RegisterClassA", 1, &wc));
    uint32_t create[12] = {0, name, name, 0, 20, 30, 100, 80, 0, 0, 0, 0};
    uint32_t hwnd = api("CreateWindowExA", 12, create); assert(hwnd);
    uint32_t allocation = void_galloc(52), buffer = allocation + 4, scratch = void_galloc(36);
    static const uint32_t lengths[] = {0, 1, 43, 44, 45, 48, 0xFFFFFFFF};
    for (unsigned i = 0; i < sizeof lengths / sizeof *lengths; ++i) {
        memset(GPTR(allocation), 0xA5, 52); MEM32(buffer) = lengths[i];
        placement32 p = get(hwnd, buffer);
        assert(p.length == 44 && !p.flags && p.show == 1);
        assert(p.minx == 0xFFFFFFFF && p.miny == 0xFFFFFFFF && p.maxx == 0xFFFFFFFF && p.maxy == 0xFFFFFFFF);
        assert(p.left == 20 && p.top == 30 && p.right == 120 && p.bottom == 110);
        assert(MEM32(allocation) == 0xA5A5A5A5 && MEM32(allocation + 48) == 0xA5A5A5A5);
    }
    placement32 desired = {44, 0, 1, 7, 9, 50, 60, 100, 200, 740, 560};
    memcpy(GPTR(buffer), &desired, 44); uint32_t args[2] = {hwnd, buffer};
    assert(api("SetWindowPlacement", 2, args) && !memcmp(&desired, GPTR(buffer), 44));
    placement32 p = get(hwnd, buffer);
    assert(p.show == 1 && !p.flags && p.minx == 7 && p.miny == 9 && p.maxx == 0xFFFFFFFF && p.maxy == 0xFFFFFFFF);
    rect_matches(hwnd, scratch, 100, 200, 640, 360);
    uint32_t move[7] = {hwnd, 0, 11, 12, 320, 180, 0}; assert(api("SetWindowPos", 7, move));
    p = get(hwnd, buffer); assert(p.left == 11 && p.top == 12 && p.right == 331 && p.bottom == 192);
    uint32_t show[2] = {hwnd, 3}; api("ShowWindow", 2, show);
    p = get(hwnd, buffer); assert(p.show == 3 && p.flags == 2 && p.left == 11 && p.right == 331);
    assert(api("IsZoomed", 1, &hwnd) && !api("IsIconic", 1, &hwnd));
    assert(last_size_type == 2 && last_size_dims == ((720u << 16) | 1280));
    rect_matches(hwnd, scratch, 0, 0, 1280, 720);
    uint32_t style[2] = {hwnd, 0xFFFFFFF0};
    assert(api("GetWindowLongW", 2, style) == api("GetWindowLongA", 2, style));
    assert(api("GetWindowLongW", 2, style) & 0x01000000);
    show[1] = 6; api("ShowWindow", 2, show);
    p = get(hwnd, buffer); assert(p.show == 2 && p.flags == 2 && p.left == 11 && p.right == 331);
    assert(api("IsIconic", 1, &hwnd) && !api("IsZoomed", 1, &hwnd));
    assert(last_size_type == 1 && !last_size_dims);
    uint32_t client[2] = {hwnd, scratch}; assert(api("GetClientRect", 2, client));
    assert(!MEM32(scratch + 8) && !MEM32(scratch + 12));
    uint32_t from_window[2] = {hwnd, 0}; assert(api("MonitorFromWindow", 2, from_window) == 0x00070001);
    show[1] = 9; api("ShowWindow", 2, show); assert(get(hwnd, buffer).show == 3);
    show[1] = 1; api("ShowWindow", 2, show); p = get(hwnd, buffer); assert(p.show == 1 && !p.flags);
    rect_matches(hwnd, scratch, 11, 12, 320, 180);
    show[1] = 0; api("ShowWindow", 2, show);
    assert(get(hwnd, buffer).show == 1 && !api("IsWindowVisible", 1, &hwnd));
    assert(!(api("GetWindowLongW", 2, style) & 0x10000000));
    desired.show = 0; desired.flags = 0x80000000; desired.left = 25; desired.top = 35; desired.right = 225; desired.bottom = 135;
    memcpy(GPTR(buffer), &desired, 44); assert(api("SetWindowPlacement", 2, args));
    p = get(hwnd, buffer); assert(p.show == 1 && !p.flags && p.left == 25 && p.right == 225);
    assert(!api("IsWindowVisible", 1, &hwnd));
    desired.show = 1; desired.flags = 0; desired.left = 4000; desired.top = 4000; desired.right = 4200; desired.bottom = 4100;
    memcpy(GPTR(buffer), &desired, 44); assert(api("SetWindowPlacement", 2, args));
    p = get(hwnd, buffer); assert(p.left < 1280 && p.top < 720 && p.right > 0 && p.bottom > 0);
    /* Match the exact original Get -> rewrite flags/show/normal rect -> Set flow. */
    p.flags = 0; p.show = 1; p.left = 0; p.top = 0; p.right = 640; p.bottom = 360;
    memcpy(GPTR(buffer), &p, 44); assert(api("SetWindowPlacement", 2, args));
    rect_matches(hwnd, scratch, 0, 0, 640, 360);
    uint32_t post[4] = {hwnd, WM_PAINT, 0, 0}; assert(api("PostMessageW", 4, post));
    uint32_t peek[5] = {scratch, hwnd, WM_PAINT, WM_PAINT, 1}; assert(api("PeekMessageW", 5, peek));
    assert(MEM32(scratch) == hwnd && MEM32(scratch + 4) == WM_PAINT);
    uint32_t before_paints = paints; api("DispatchMessageW", 1, &scratch); assert(paints == before_paints + 1);
    for (unsigned i = 0; i < sizeof lengths / sizeof *lengths; ++i) if (lengths[i] != 44) {
        p.length = lengths[i]; memcpy(GPTR(buffer), &p, 44);
        assert(!api("SetWindowPlacement", 2, args) && void_get_last_error() == 87);
    }
    args[0] = 0x12345678; args[1] = 0;
    assert(!api("GetWindowPlacement", 2, args) && void_get_last_error() == 1400);
    assert(!api("SetWindowPlacement", 2, args) && void_get_last_error() == 1400);
    args[0] = hwnd;
    assert(!api("GetWindowPlacement", 2, args) && void_get_last_error() == 998);
    assert(!api("SetWindowPlacement", 2, args) && void_get_last_error() == 998);
    assert(api("DestroyWindow", 1, &hwnd));
    void_gfree(wc); void_gfree(name); void_gfree(allocation); void_gfree(scratch);
    printf("PASS WINDOWPLACEMENT44 native lengths/errors, normal/min/max/hide/restore, saved geometry/metrics, original Get/Set flow, GetWindowLongW/PostMessageW, guards and stdcall\n");
    return 0;
}
