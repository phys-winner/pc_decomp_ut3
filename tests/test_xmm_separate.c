/* Real runtime contexts/machine; synthetic bytes only, no game or GPU. */
#include "../android/host/threads.c"
#include <assert.h>
#include <inttypes.h>

void fixture_xmm_probe(void);
void fixture_xmm_guest_callback(void);
void sub_0117C3B0(void);
static xmm_t expected[8];
static unsigned phase_now, cases, callback_calls, yield_calls, handovers;
static void (*production_yield)(void);
static uint64_t checksum = 1469598103934665603ull;
static uint32_t rng = 0x7C519AB3u;
static unsigned contender_stage;
static uint32_t random32(void) { rng = rng * 1664525u + 1013904223u; return rng; }
static void mix_bytes(const void *data, size_t bytes)
{
    const uint8_t *p = data;
    for (size_t i = 0; i < bytes; ++i) checksum = (checksum ^ p[i]) * 1099511628211ull;
}
void void_process_exit(uint32_t code, int detach) { (void)detach; _exit((int)code); }
void fixture_xmm_check(unsigned phase)
{
    assert(phase == phase_now);
    assert(!memcmp(g_xmm, expected, sizeof expected));
}
static void assert_context(const void_regs *wanted)
{
    void_regs actual; memset(&actual, 0, sizeof actual); void_regs_save(&actual);
    assert(!memcmp(&actual, wanted, sizeof actual));
}
static void *contender(void *unused)
{
    (void)unused;
    __atomic_store_n(&contender_stage, 1, __ATOMIC_RELEASE);
    void_machine_enter();
    for (unsigned i = 0; i < 8; ++i)
        for (unsigned lane = 0; lane < 4; ++lane) g_xmm[i].u32[lane] = 0xDEADBEEFu ^ (i * 4 + lane);
    g_eax = g_ecx = g_edx = g_ebx = g_esi = g_edi = g_ebp = 0xC01DF00Du;
    g_flag_k = FK_CMP; g_flag_a = 37; g_flag_b = 41; g_flag_cf = 1;
    __atomic_store_n(&contender_stage, 2, __ATOMIC_RELEASE);
    void_machine_leave();
    return NULL;
}
static void actual_handover(int yield)
{
    void_regs before; memset(&before, 0, sizeof before); void_regs_save(&before);
    pthread_t thread; contender_stage = 0;
    assert(!pthread_create(&thread, NULL, contender, NULL));
    while (!__atomic_load_n(&contender_stage, __ATOMIC_ACQUIRE) || !machine_contended()) sched_yield();
    if (yield) production_yield();
    else {
        void_blocking_begin();
        while (__atomic_load_n(&contender_stage, __ATOMIC_ACQUIRE) != 2) sched_yield();
        void_blocking_end();
    }
    assert(!pthread_join(thread, NULL));
    assert(contender_stage == 2); assert_context(&before); ++handovers;
}
static void mutate(unsigned phase)
{
    for (unsigned i = 0; i < 8; ++i)
        for (unsigned lane = 0; lane < 4; ++lane)
            expected[i].u32[lane] ^= 0x5B6C7D8Eu + 0x01010101u * (phase + i * 4 + lane);
    memcpy(g_xmm, expected, sizeof expected); phase_now = phase;
}
void fixture_native_callback(void)
{
    fixture_xmm_check(0); ++callback_calls;
    void_regs saved; memset(&saved, 0, sizeof saved); void_regs_save(&saved);
    uint32_t arguments[] = {0xABCDEF12u, 0x76543210u};
    assert(void_call_guest(0x02000120u, 2, arguments, 1) == 0x33445566u);
    for (unsigned i = 0; i < 8; ++i)
        for (unsigned lane = 0; lane < 4; ++lane)
            assert(g_xmm[i].u32[lane] == (expected[i].u32[lane] ^ 0xA5A5A5A5u));
    void_regs_load(&saved); assert_context(&saved); fixture_xmm_check(0);
    if (!cases) actual_handover(0);
    mutate(1); g_esp += 4;
}
static void fixture_yield_hook(void)
{
    fixture_xmm_check(1); ++yield_calls;
    if (!cases) actual_handover(1);
    mutate(2);
}
static void reference(uint8_t *data, unsigned source, unsigned destination)
{
    memcpy(&expected[0], data + source + 1, 16);
    uint32_t word = 0x19283746u; memcpy(data + source + 5, &word, 4);
    memcpy(&expected[1], data + source + 3, 16);
    memcpy(data + destination + 2, &expected[0], 16);
    memcpy(&expected[2], data + source + 5, 16);
    data[destination + 7] ^= 0xB3u;
    memcpy(&expected[3], data + source + 7, 16);
    memcpy(data + destination + 11, &expected[1], 16);
    memcpy(&expected[4], data + source + 9, 16);
    uint16_t half = 0xD25Fu; memcpy(data + source + 15, &half, 2);
    memcpy(&expected[5], data + source + 11, 16);
    memcpy(data + destination + 1, &expected[2], 16);
    memcpy(&expected[6], data + source + 13, 16);
    memcpy(&expected[7], data + source + 15, 16);
}
static void actual_owner_cases(void)
{
    uint32_t data = void_galloc(512), point = data + 256, object = data + 320;
    assert(data); void_dispatch_add(0x0117C3B0u, sub_0117C3B0);
    for (unsigned test = 0; test < 4096; ++test) {
        float planes[64], center[4], radius = (float)(test % 33u) * 0.5f;
        unsigned count = (test % 5u) * 4u;
        for (unsigned i = 0; i < 4; ++i) center[i] = (float)((int)(random32() % 65u) - 32) * 0.25f;
        for (unsigned i = 0; i < 64; ++i) planes[i] = (float)((int)(random32() % 65u) - 32) * 0.25f;
        int visible = 1;
        for (unsigned i = 0; i < count; ++i) {
            unsigned block = (i / 4u) * 16u, lane = i % 4u;
            float value = center[1] * planes[block + 4u + lane] + center[0] * planes[block + lane];
            value = value + center[2] * planes[block + 8u + lane];
            value = value - planes[block + 12u + lane];
            if (radius < value) visible = 0;
        }
        memcpy(GPTR(data), planes, sizeof planes); memcpy(GPTR(point), center, sizeof center);
        MEM32(object + 12) = data; MEM32(object + 16) = count;
        for (unsigned i = 0; i < 8; ++i)
            for (unsigned lane = 0; lane < 4; ++lane) g_xmm[i].u32[lane] = random32();
        g_eax = 0x31415926u; g_ecx = object; g_edx = 0x27182818u;
        g_ebx = 0x11111111u; g_esi = 0x22222222u; g_edi = 0x33333333u; g_ebp = 0x44444444u;
        g_flag_k = FK_CMP; g_flag_a = test; g_flag_b = test; g_flag_cf = 0; g_backedges = 0;
        uint32_t args[2] = {point, 0}; memcpy(&args[1], &radius, 4);
        uint32_t sp = g_esp;
        assert(void_call_guest(0x0117C3B0u, 2, args, 1) == (uint32_t)visible && g_esp == sp);
        assert(g_ebx == 0x11111111u && g_esi == 0x22222222u && g_edi == 0x33333333u && g_ebp == 0x44444444u);
        mix_bytes(g_xmm, sizeof g_xmm);
        uint32_t scalar[] = {g_eax, g_ecx - object, g_edx - data, g_flag_k, g_flag_a, g_flag_b, g_flag_cf};
        mix_bytes(scalar, sizeof scalar);
    }
    void_gfree(data);
}
int main(void)
{
    alarm(20); assert(void_guest_reserve());
    uintptr_t native = (uintptr_t)g_xmm;
    assert(native + sizeof g_xmm <= VOID_GUEST_BASE || native >= VOID_GUEST_BASE + VOID_GUEST_SPAN);
    assert(!void_vm_readable(0, 1) && !void_vm_readable(UINT32_MAX, 1));
    void_threads_init(); void_dispatch_init(); void_machine_enter();
    production_yield = recomp_yield_hook; assert(production_yield); recomp_yield_hook = fixture_yield_hook;
    void_dispatch_add(0x02000100u, fixture_xmm_probe);
    void_dispatch_add(0x02000120u, fixture_xmm_guest_callback);
    uint32_t memory = void_galloc(512); assert(memory);
    const int offsets[] = {-17, -3, -1, 0, 1, 3, 17, 65};
    for (cases = 0; cases < 512; ++cases) {
        uint8_t bytes[512], wanted[512];
        for (unsigned i = 0; i < sizeof bytes; ++i) bytes[i] = (uint8_t)random32();
        memcpy(wanted, bytes, sizeof bytes);
        for (unsigned i = 0; i < 8; ++i)
            for (unsigned lane = 0; lane < 4; ++lane) g_xmm[i].u32[lane] = random32();
        unsigned source = 64 + (cases % 16), destination = source + offsets[(cases / 16) % 8];
        reference(wanted, source, destination); memcpy(GPTR(memory), bytes, sizeof bytes);
        g_ecx = memory + source; g_edx = memory + destination; g_ebx = memory + 256;
        phase_now = 0; uint32_t sp = g_esp;
        (void)void_call_guest(0x02000100u, 0, NULL, 1);
        assert(g_esp == sp && phase_now == 2);
        memcpy(wanted + 257, expected, sizeof expected);
        assert(!memcmp(GPTR(memory), wanted, sizeof wanted));
        assert(!memcmp(g_xmm, expected, sizeof expected));
        mix_bytes(expected, sizeof expected); mix_bytes(wanted, sizeof wanted);
    }
    assert(callback_calls == 512 && yield_calls == 512 && handovers == 2);
    recomp_yield_hook = production_yield; actual_owner_cases(); void_gfree(memory); void_machine_leave();
    printf("PASS XMM separation: cases=512 all8_bitexact=1 unaligned16=1 guest_overlap8=1 callbacks=512 yields=512 real_handover=2 reservation4GiB=1 actual_owner_cases=4096 checksum=%016" PRIx64 "\n", checksum);
    return 0;
}
