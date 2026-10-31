/* Exercise the production checked dispatch wrapper and the real machine
 * release/save/restore while another guest thread changes every register. */
#include "../android/host/threads.c"
#include <assert.h>
static unsigned waits_begun, waits_ended;
static void fixture_block_begin(void) { ++waits_begun; void_blocking_begin(); }
static void fixture_block_end(void) { void_blocking_end(); ++waits_ended; }
#define void_blocking_begin fixture_block_begin
#define void_blocking_end fixture_block_end
#include "../android/host/loading.c"
#undef void_blocking_begin
#undef void_blocking_end

void void_process_exit(uint32_t code, int detach) { (void)detach; _exit((int)code); }
static uint32_t fixture_result, output;
static unsigned original_calls, stage;
static void_regs expected;
static uint8_t expected_bytes[32];
static void original_fixture(void)
{
    ++original_calls;
    g_eax = fixture_result; g_ebx = 0x23232323; g_ecx = 0x34343434;
    g_edx = 0x45454545; g_esi = 0x56565656; g_edi = 0x67676767; g_ebp = 0x78787878;
    g_esp += 12;
    g_flag_k = FK_CMP; g_flag_a = 0x76543210; g_flag_b = 0x12345678; g_flag_cf = 1;
    g_mxcsr = 0x1f80; g_mm[3] = 0xfedcba9876543210ull;
    g_xmm[5].u64[0] = 0x1122334455667788ull; g_xmm[5].u64[1] = 0x99aabbccddeeff00ull;
    g_backedges = 312; g_cur_func = ASYNC_PRECACHE_VA;
    MEM32(output + 4) = 0x11223344;
    memset(&expected, 0, sizeof expected); void_regs_save(&expected);
    memcpy(expected_bytes, GPTR(output), sizeof expected_bytes);
}
static void invoke(uint32_t value)
{
    fixture_result = value;
    g_esp -= 12; /* thiscall's return address and two stack arguments */
    void_dispatch_find(ASYNC_PRECACHE_VA)();
    void_regs actual; memset(&actual, 0, sizeof actual); void_regs_save(&actual);
    assert(!memcmp(&actual, &expected, sizeof actual));
    assert(!memcmp(GPTR(output), expected_bytes, sizeof expected_bytes));
}
static void *contender(void *unused)
{
    (void)unused;
    __atomic_store_n(&stage, 1, __ATOMIC_RELEASE);
    void_machine_enter();
    g_eax = g_ebx = g_ecx = g_edx = g_esi = g_edi = g_ebp = 0xdeadbeef;
    g_flag_k = FK_TEST; g_flag_a = 33; g_flag_b = 55; g_flag_cf = 0;
    g_mm[3] = 0; memset(&g_xmm[5], 0, sizeof g_xmm[5]); g_backedges = 99;
    __atomic_store_n(&stage, 2, __ATOMIC_RELEASE);
    void_machine_leave();
    return NULL;
}
int main(int argc, char **argv)
{
    alarm(10); assert(argc == 2); setenv("UT3_PRECACHE_WAIT_US", argv[1], 1);
    assert(void_guest_reserve()); void_threads_init(); void_dispatch_init(); void_machine_enter();
    uint32_t page = ASYNC_PRECACHE_VA & ~0xffffu;
    assert(void_vm_alloc(page, 65536, 0x3000, 0x40) == page);
    memcpy(GPTR(ASYNC_PRECACHE_VA), precache_preimage, sizeof precache_preimage);
    void_dispatch_add(ASYNC_PRECACHE_VA, original_fixture);
    MEM8(ASYNC_PRECACHE_VA) ^= 1;
    void_loading_init(); assert(void_dispatch_find(ASYNC_PRECACHE_VA) == original_fixture);
    MEM8(ASYNC_PRECACHE_VA) ^= 1;
    void_loading_init(); assert(void_dispatch_find(ASYNC_PRECACHE_VA) == precache_cooperative);
    void_loading_init(); assert(precache_original == original_fixture);
    output = void_galloc(32); assert(output); memset(GPTR(output), 0xa5, 32);
    invoke(1); invoke(0x12345601); assert(!waits_begun);
    for (unsigned i = 0; i < 8; ++i) invoke(0x12345600);
    assert(!waits_begun);
    if (precache_wait_us) {
        pthread_t thread; assert(!pthread_create(&thread, NULL, contender, NULL));
        while (!__atomic_load_n(&stage, __ATOMIC_ACQUIRE) || !machine_contended()) sched_yield();
        invoke(0x12345600);
        assert(__atomic_load_n(&stage, __ATOMIC_ACQUIRE) == 2);
        assert(!pthread_join(thread, NULL));
        assert(waits_begun == 1 && waits_ended == 1);
    } else {
        for (unsigned i = 0; i < 100; ++i) invoke(0x12345600);
        assert(!waits_begun && !waits_ended);
    }
    unsigned previous = waits_begun;
    invoke(0x12345601);
    for (unsigned i = 0; i < 8; ++i) invoke(0);
    assert(waits_begun == previous && waits_begun == waits_ended);
    assert(original_calls > 16);
    void_machine_leave();
    puts("PASS production Precache wrapper: exact code guard, original call/AL gate, unchanged whole register file/output/stack, balanced real VM handover, ready reset and wait-off control");
    return 0;
}
