/* Shared production A/W queue ABI and a real blocked guest-thread wakeup. */
#include "../android/host/user32.c"
#include <assert.h>
#include <unistd.h>
_Static_assert(sizeof(qmsg_t) == 28, "Win32 guest MSG layout");

void void_process_exit(uint32_t code, int detach) { (void)detach; _exit((int)code); }
static uint32_t receiver_thread, receiver_stage;

static uint32_t api(const char *name, unsigned count, const uint32_t *args)
{
    const void_shim *shim = void_shim_find("user32.dll", name, 0); assert(shim);
    uint32_t before = g_esp;
    uint32_t result = void_call_guest(void_thunk(shim->fn, name), count, args, 1);
    assert(g_esp == before);
    return result;
}

static void *receiver(void *ignored)
{
    (void)ignored;
    void_machine_enter();
    uint32_t allocation = void_galloc(36), msg = allocation + 4;
    memset(GPTR(allocation), 0xA5, 36);
    uint32_t peek[5] = {msg, 0, 0, 0, 0}, get[4] = {msg, 0, 0, 0};
    assert(!api("PeekMessageW", 5, peek));
    __atomic_store_n(&receiver_thread, void_thread_id(), __ATOMIC_RELEASE);
    __atomic_store_n(&receiver_stage, 1, __ATOMIC_RELEASE);
    assert(api("GetMessageW", 4, get) == 1);
    assert(!MEM32(msg) && MEM32(msg + 4) == 0x8001 && MEM32(msg + 8) == 0xFEDCBA98 && MEM32(msg + 12) == 0x87654321);
    assert(MEM32(allocation) == 0xA5A5A5A5 && MEM32(allocation + 32) == 0xA5A5A5A5);
    __atomic_store_n(&receiver_stage, 2, __ATOMIC_RELEASE);
    assert(api("GetMessageA", 4, get) == 0);
    assert(!MEM32(msg) && MEM32(msg + 4) == WM_QUIT && MEM32(msg + 8) == 77 && MEM32(msg + 12) == 88);
    assert(MEM32(allocation) == 0xA5A5A5A5 && MEM32(allocation + 32) == 0xA5A5A5A5);
    void_gfree(allocation);
    __atomic_store_n(&receiver_stage, 3, __ATOMIC_RELEASE);
    void_machine_leave();
    return NULL;
}

static void wait_stage(uint32_t stage)
{
    void_blocking_begin();
    while (__atomic_load_n(&receiver_stage, __ATOMIC_ACQUIRE) < stage) usleep(1000);
    void_blocking_end();
}

int main(void)
{
    alarm(10);
    assert(void_guest_reserve());
    void_threads_init(); void_dispatch_init(); void_machine_enter();
    uint32_t post[4] = {void_thread_id(), 0x8001, 0, 0};
    /* Even a real thread cannot receive PostThreadMessage before creating
     * its USER queue. Invalid/zero target IDs must not get fabricated queues. */
    assert(!api("PostThreadMessageW", 4, post) && void_get_last_error() == 1444);
    post[0] = 0;
    assert(!api("PostThreadMessageA", 4, post) && void_get_last_error() == 1444);
    post[0] = 0xFFFFFFFF;
    assert(!api("PostThreadMessageW", 4, post) && void_get_last_error() == 1444);
    pthread_t thread; assert(!pthread_create(&thread, NULL, receiver, NULL));
    wait_stage(1);
    post[0] = __atomic_load_n(&receiver_thread, __ATOMIC_ACQUIRE);
    post[2] = 0xFEDCBA98; post[3] = 0x87654321;
    assert(api("PostThreadMessageA", 4, post));
    wait_stage(2);
    post[1] = WM_QUIT; post[2] = 77; post[3] = 88;
    assert(api("PostThreadMessageW", 4, post));
    wait_stage(3);
    void_blocking_begin(); assert(!pthread_join(thread, NULL)); void_blocking_end();

    uint32_t allocation = void_galloc(36), msg = allocation + 4;
    memset(GPTR(allocation), 0xA5, 36);
    uint32_t get[4] = {msg, 0x12345678, 0, 0};
    assert(api("GetMessageW", 4, get) == 0xFFFFFFFF && void_get_last_error() == 1400);
    get[0] = 0; get[1] = 0;
    assert(api("GetMessageA", 4, get) == 0xFFFFFFFF && void_get_last_error() == 998);
    uint32_t peek[5] = {msg, 0, 0, 0, 0};
    assert(!api("PeekMessageA", 5, peek));
    post[0] = void_thread_id(); post[1] = 0x8002; post[2] = 0x12345678; post[3] = 0xABCDEF01;
    for (unsigned i = 0; i < 1024; ++i) assert(api("PostThreadMessageW", 4, post));
    assert(!api("PostThreadMessageA", 4, post) && void_get_last_error() == 1816);
    get[0] = msg;
    for (unsigned i = 0; i < 1024; ++i) {
        assert(api(i & 1 ? "GetMessageA" : "GetMessageW", 4, get) == 1);
        assert(!MEM32(msg) && MEM32(msg + 4) == 0x8002 && MEM32(msg + 8) == 0x12345678 && MEM32(msg + 12) == 0xABCDEF01);
    }
    assert(!api("PeekMessageW", 5, peek));
    assert(MEM32(allocation) == 0xA5A5A5A5 && MEM32(allocation + 32) == 0xA5A5A5A5);
    void_gfree(allocation);
    printf("PASS message queue A/W shared MSG28 ABI, blocked cross-thread post/wakeup, WM_QUIT, invalid target/window/output, quota and stdcall\n");
    return 0;
}
