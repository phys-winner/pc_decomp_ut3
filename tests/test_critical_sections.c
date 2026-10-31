/* Production guest layout, ABI cleanup, recursion and contended handoff. */
#include "../android/host/threads.c"
#include <assert.h>

void void_process_exit(uint32_t code, int detach)
{
    (void)detach;
    _exit((int)code);
}

static uint32_t api(const char *name, int count, const uint32_t *args)
{
    const void_shim *shim = void_shim_find("kernel32.dll", name, 0);
    assert(shim);
    uint32_t before = g_esp;
    uint32_t result = void_call_guest(void_thunk(shim->fn, name), count, args, 1);
    assert(g_esp == before);
    return result;
}

static unsigned contender_stage;
static void *contender(void *arg)
{
    uint32_t cs = *(uint32_t *)arg;
    void_machine_enter();
    __atomic_store_n(&contender_stage, 1, __ATOMIC_RELEASE);
    cs_enter(cs);
    assert(MEM32(cs + 12) == void_thread_id() && MEM32(cs + 8) == 1);
    __atomic_store_n(&contender_stage, 2, __ATOMIC_RELEASE);
    cs_leave(cs);
    void_machine_leave();
    return NULL;
}

int main(void)
{
    alarm(10);
    assert(void_guest_reserve());
    void_threads_init();
    void_dispatch_init();
    void_machine_enter();
    uint32_t cs = void_galloc(32), args[] = {cs, 4000};
    assert(cs);
    MEM32(cs + 24) = 0x11223344;
    api("InitializeCriticalSection", 1, args);
    assert(!MEM32(cs + 20) && MEM32(cs + 4) == UINT32_MAX);
    uint8_t before[20];
    memcpy(before, GPTR(cs), sizeof before);
    assert(api("SetCriticalSectionSpinCount", 2, args) == 0);
    assert(MEM32(cs + 20) == cs_effective_spin(4000));
    assert(!memcmp(before, GPTR(cs), sizeof before));
    args[1] = 5000;
    assert(api("SetCriticalSectionSpinCount", 2, args) == cs_effective_spin(4000));
    api("EnterCriticalSection", 1, args);
    assert(MEM32(cs + 12) == void_thread_id() && MEM32(cs + 8) == 1 && MEM32(cs + 4) == 0);
    memcpy(before, GPTR(cs), sizeof before);
    args[1] = 33;
    assert(api("SetCriticalSectionSpinCount", 2, args) == cs_effective_spin(5000));
    assert(!memcmp(before, GPTR(cs), sizeof before));
    api("EnterCriticalSection", 1, args);
    assert(MEM32(cs + 8) == 2 && MEM32(cs + 4) == 1);
    api("LeaveCriticalSection", 1, args);
    assert(MEM32(cs + 8) == 1 && MEM32(cs + 12) == void_thread_id());
    pthread_t other;
    assert(!pthread_create(&other, NULL, contender, &cs));
    void_blocking_begin();
    while (!__atomic_load_n(&contender_stage, __ATOMIC_ACQUIRE)) usleep(1000);
    void_blocking_end();
    assert(__atomic_load_n(&cs_waiters, __ATOMIC_ACQUIRE) == 1);
    assert(__atomic_load_n(&contender_stage, __ATOMIC_ACQUIRE) == 1);
    api("LeaveCriticalSection", 1, args);
    void_blocking_begin();
    assert(!pthread_join(other, NULL));
    void_blocking_end();
    assert(__atomic_load_n(&contender_stage, __ATOMIC_ACQUIRE) == 2);
    assert(!MEM32(cs + 8) && !MEM32(cs + 12) && MEM32(cs + 4) == UINT32_MAX);
    assert(MEM32(cs + 20) == cs_effective_spin(33) && MEM32(cs + 24) == 0x11223344);
    assert(api("TryEnterCriticalSection", 1, args));
    api("LeaveCriticalSection", 1, args);
    api("DeleteCriticalSection", 1, args);
    args[1] = 512;
    assert(api("InitializeCriticalSectionAndSpinCount", 2, args));
    args[1] = 0;
    assert(api("SetCriticalSectionSpinCount", 2, args) == cs_effective_spin(512));
    assert(!MEM32(cs + 20));
    api("DeleteCriticalSection", 1, args);
    void_gfree(cs);
    void_machine_leave();
    puts("Critical sections PASS: spin/previous value, x86 ABI, untouched ownership, recursive enter/leave, contended handoff, delete/reinit");
    return 0;
}
