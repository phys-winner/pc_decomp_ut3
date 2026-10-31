/* Production virtual affinity model, CPUID identity and physical-policy separation. */
#include "../android/host/threads.c"
#include <assert.h>

void void_process_exit(uint32_t code, int detach) { (void)detach; _exit((int)code); }
static uint32_t worker_stage, worker_previous, worker_a, worker_b;

static uint32_t api(const char *name, unsigned count, const uint32_t *args)
{
    const void_shim *shim = void_shim_find("kernel32.dll", name, 0); assert(shim);
    uint32_t before = g_esp, result = void_call_guest(void_thunk(shim->fn, name), count, args, 1);
    assert(g_esp == before); return result;
}

static void *worker(void *ignored)
{
    (void)ignored; void_machine_enter();
    uint32_t args[2] = {0xFFFFFFFE, worker_a};
    assert(api("SetThreadAffinityMask", 2, args) == (worker_a | worker_b));
    __atomic_store_n(&worker_stage, 1, __ATOMIC_RELEASE);
    void_blocking_begin();
    while (__atomic_load_n(&worker_stage, __ATOMIC_ACQUIRE) < 2) usleep(1000);
    void_blocking_end();
    args[1] = worker_b; worker_previous = api("SetThreadAffinityMask", 2, args);
    void_cpuid(1, 0); assert((g_ebx >> 24) == (uint32_t)__builtin_ctz(worker_b));
    __atomic_store_n(&worker_stage, 3, __ATOMIC_RELEASE);
    void_machine_leave(); return NULL;
}

int main(void)
{
    alarm(10);
    assert(void_guest_reserve()); void_threads_init(); void_dispatch_init(); void_machine_enter();
    cpu_set_t physical_before, physical_after;
    assert(!sched_getaffinity(0, sizeof physical_before, &physical_before));
    uint32_t out = void_galloc(16), info = void_galloc(36);
    memset(GPTR(out), 0xA5, 16);
    uint32_t get[3] = {0xFFFFFFFF, out + 4, out + 8};
    assert(api("GetProcessAffinityMask", 3, get));
    uint32_t system = MEM32(out + 8), original = MEM32(out + 4);
    assert(system && original == system);
    assert(api("GetSystemInfo", 1, &info) == 0);
    uint32_t count = MEM32(info + 20);
    assert(count >= 1 && count <= 32 && MEM32(info + 16) == system && __builtin_popcount(system) == count);
    assert(MEM32(MEM32(FS_BASE + 0x30) + 0x64) == count);
    worker_a = system & (0u - system);
    uint32_t remaining = system & ~worker_a;
    worker_b = remaining ? remaining & (0u - remaining) : worker_a;
    uint32_t pair = worker_a | worker_b, process[2] = {0xFFFFFFFF, pair}, thread[2] = {0xFFFFFFFE, worker_a};
    assert(api("SetThreadAffinityMask", 2, thread) == system);
    void_cpuid(1, 0); assert(((g_ebx >> 16) & 255) == count && (g_ebx >> 24) == (uint32_t)__builtin_ctz(worker_a));
    assert(api("SetProcessAffinityMask", 2, process));
    assert(api("SetThreadAffinityMask", 2, thread) == pair); /* Equal/wider process mask reset. */
    assert(api("SetProcessAffinityMask", 2, process));
    pthread_t other; assert(!pthread_create(&other, NULL, worker, NULL));
    void_blocking_begin(); while (!__atomic_load_n(&worker_stage, __ATOMIC_ACQUIRE)) usleep(1000); void_blocking_end();
    process[1] = worker_b; assert(api("SetProcessAffinityMask", 2, process));
    thread[1] = worker_b; assert(api("SetThreadAffinityMask", 2, thread) == worker_b);
    assert(api("GetProcessAffinityMask", 3, get) && MEM32(out + 4) == worker_b && MEM32(out + 8) == system);
    get[2] = get[1]; assert(api("GetProcessAffinityMask", 3, get) && MEM32(out + 4) == system); get[2] = out + 8;
    if (worker_b != worker_a) {
        thread[1] = worker_a; assert(!api("SetThreadAffinityMask", 2, thread) && void_get_last_error() == 87);
    }
    __atomic_store_n(&worker_stage, 2, __ATOMIC_RELEASE);
    void_blocking_begin(); assert(!pthread_join(other, NULL)); void_blocking_end();
    assert(worker_previous == worker_b && worker_stage == 3);
    process[1] = system; assert(api("SetProcessAffinityMask", 2, process));
    uint32_t previous = system;
    for (uint32_t bits = system; bits; bits &= bits - 1) {
        uint32_t one = bits & (0u - bits); thread[1] = one;
        assert(api("SetThreadAffinityMask", 2, thread) == previous); previous = one;
        void_cpuid(1, 0); assert((g_ebx >> 24) == (uint32_t)__builtin_ctz(one) && ((g_ebx >> 16) & 255) == count);
    }
    thread[1] = system; assert(api("SetThreadAffinityMask", 2, thread) == previous);
    void_cpuid(0x80000008, 0);
    uint32_t width = (g_ecx >> 12) & 15, cores = width ? 1u << width : (g_ecx & 255) + 1;
    assert(g_ecx == count - 1 && width == 0);
    assert(cores == count); /* Original tier0_s 21002529/2100252C fallback. */
    assert(g_ecx + 1 == count); /* Original UE3 015A3C7A/015A3C80, no NC mask. */
    uint32_t duplicate = void_galloc(4), dup[7] = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, duplicate, 0, 0, 2};
    assert(api("DuplicateHandle", 7, dup)); get[0] = MEM32(duplicate);
    assert(api("GetProcessAffinityMask", 3, get) && MEM32(out + 4) == system);
    assert(api("CloseHandle", 1, &get[0]));
    get[0] = 0; assert(!api("GetProcessAffinityMask", 3, get) && void_get_last_error() == 6);
    get[0] = 0xFFFFFFFF; get[1] = 0;
    assert(!api("GetProcessAffinityMask", 3, get) && void_get_last_error() == 998);
    get[1] = out + 4; get[2] = 0x1000;
    memset(GPTR(out + 4), 0xA5, 8);
    assert(!api("GetProcessAffinityMask", 3, get) && void_get_last_error() == 998);
    assert(MEM32(out + 4) == 0xA5A5A5A5 && MEM32(out + 8) == 0xA5A5A5A5);
    process[0] = 0; process[1] = 0; assert(!api("SetProcessAffinityMask", 2, process) && void_get_last_error() == 87);
    process[1] = worker_a; assert(!api("SetProcessAffinityMask", 2, process) && void_get_last_error() == 6);
    thread[0] = 0; thread[1] = 0; assert(!api("SetThreadAffinityMask", 2, thread) && void_get_last_error() == 6);
    thread[0] = 0xFFFFFFFE; assert(!api("SetThreadAffinityMask", 2, thread) && void_get_last_error() == 87);
    if (~system) {
        process[0] = 0xFFFFFFFF; process[1] = ~system & (0u - ~system);
        assert(!api("SetProcessAffinityMask", 2, process) && void_get_last_error() == 87);
    }
    assert(!sched_getaffinity(0, sizeof physical_after, &physical_after));
    assert(!memcmp(&physical_before, &physical_after, sizeof physical_before));
    assert(MEM32(out) == 0xA5A5A5A5 && MEM32(out + 12) == 0xA5A5A5A5);
    void_gfree(out); void_gfree(info); void_gfree(duplicate);
    printf("PASS virtual CPU affinity: count=%u system=%08X; process/thread reset/inherit/prior returns, CPUID count/APIC IDs/core count, PEB, bounds/handles/ABI; physical pin unchanged\n", count, system);
    return 0;
}
