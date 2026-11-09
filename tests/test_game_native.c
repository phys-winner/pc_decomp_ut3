#include "host.h"
#include "../android/host/game_native.c"
#include <assert.h>
#include <sys/mman.h>

VOID_HIDDEN recomp_ehframe_t *g_eh_frames;
VOID_HIDDEN uint32_t g_eh_obj, g_eh_info;
static unsigned checks, failures, yields;
static int yield_phase, yield_snap_valid;
static void_regs yield_snapshot;
static uint32_t seed = 0x517AC139u;

void void_log(const char *format, ...) { (void)format; }
const char *void_data_dir(void) { return "/nonexistent-ut3-native-test"; }
void void_dispatch_add(uint32_t address, recomp_func_t fn) { (void)address; (void)fn; }
static uint32_t random_u32(void) { seed = seed * 1664525u + 1013904223u; return seed; }
static float random_float(void) { return (float)((int32_t)(random_u32() % 65537u) - 32768) * 0.03125f; }

static void reset(uint32_t self, uint32_t arg)
{
    g_eax = random_u32(); g_ecx = self; g_edx = random_u32(); g_ebx = random_u32();
    g_esi = random_u32(); g_edi = random_u32(); g_ebp = random_u32(); g_esp = 0x3F004u;
    g_flag_k = FK_CMP; g_flag_a = random_u32(); g_flag_b = random_u32(); g_flag_cf = 1;
    g_backedges = 0; recomp_yield_hook = NULL;
    for (unsigned i = 0; i < 8; ++i)
        for (unsigned j = 0; j < 4; ++j) g_xmm[i].u32[j] = random_u32();
    MEM32(g_esp) = RECOMP_RETADDR; MEM32(g_esp + 4) = arg;
}

static void compare(void (*native)(void), void (*original)(void), uint32_t out, const char *name)
{
    void_regs before = {0}, a = {0}, b = {0};
    const size_t output_size = native == actor_next ? 20 : 64;
    uint8_t saved[64], expected[64];
    void_regs_save(&before);
    if (out) memcpy(saved, GPTR(out), output_size);
    yield_phase = 0; yield_snap_valid = 0;
    native(); void_regs_save(&a);
    if (out) { memcpy(expected, GPTR(out), output_size); memcpy(GPTR(out), saved, output_size); }
    void_regs_load(&before);
    yield_phase = 1;
    original(); void_regs_save(&b);
    ++checks;
    if (memcmp(&a, &b, sizeof a) || (out && memcmp(expected, GPTR(out), output_size))) {
        ++failures;
        if (failures <= 5) {
            fprintf(stderr, "%s case %u differs\n", name, checks);
            for (unsigned i = 0; i < sizeof a / 4; ++i)
                if (((uint32_t *)&a)[i] != ((uint32_t *)&b)[i])
                    fprintf(stderr, "state +%u: %08x != %08x\n", i * 4, ((uint32_t *)&a)[i], ((uint32_t *)&b)[i]);
            if (out) for (unsigned i = 0; i < output_size / 4; ++i)
                if (((uint32_t *)expected)[i] != MEM32(out + i * 4))
                    fprintf(stderr, "matrix[%u]: %08x != %08x\n", i, ((uint32_t *)expected)[i], MEM32(out + i * 4));
        }
    }
}

static void actor_cases(void)
{
    float *sine = GPTR(0x1DBDF78u);
    for (unsigned i = 0; i < 16384; ++i) sine[i] = sinf((float)i * 0.0003834951969714103f);
    MEMF(0x173D5BCu) = 0.0f; MEMF(0x17343D8u) = 1.0f;
    for (unsigned test = 0; test < 100000; ++test) {
        for (unsigned i = 0; i < 0x200; i += 4) MEMF(0x10000u + i) = random_float();
        MEM32(0x100F0) = random_u32(); MEM32(0x100F4) = random_u32(); MEM32(0x100F8) = random_u32();
        if (!(test % 7)) MEM32(0x10144) = test & 1 ? 0x80000000u : 0;
        const uint32_t out = test % 3 ? 0x11000u : 0x100E4u;
        reset(0x10000, out);
        compare(actor_transform, __real_sub_006044F0, out, "actor");
    }
    /* IEEE edge inputs, including overflow, denormals, infinities and both NaN signs. */
    const uint32_t special[] = {0, 0x80000000, 1, 0x80000001, 0x7F7FFFFF, 0xFF7FFFFF,
        0x7F800000, 0xFF800000, 0x7FC00123, 0xFFC00456, 0x7F800001};
    for (unsigned slot = 0; slot < 10; ++slot) for (unsigned i = 0; i < sizeof special / 4; ++i) {
        const uint32_t offsets[] = {0xE4,0xE8,0xEC,0x144,0x148,0x14C,0x150,0x154,0x158,0x15C};
        for (unsigned j = 0; j < 10; ++j) MEMF(0x10000u + offsets[j]) = random_float();
        MEM32(0x10000u + offsets[slot]) = special[i];
        reset(0x10000, 0x11000);
        compare(actor_transform, __real_sub_006044F0, 0x11000, "actor-edge");
    }
    const uint32_t saved_seed = seed;
    seed = 0xA8C514EFu;
    for (unsigned test = 0; test < 30000; ++test) {
        const uint32_t offsets[] = {0xE4,0xE8,0xEC,0x144,0x148,0x14C,0x150,0x154,0x158,0x15C};
        const uint32_t outputs[] = {0x11000, 0x100D8, 0x100E4, 0x100F8, 0x10120, 0x10144, 0x10154};
        for (unsigned j = 0; j < 10; ++j) {
            uint32_t bits = random_u32();
            if (!(test % 4) && (j % 3)) bits |= 0x7F800000u;
            else if (test % 4 == 1 && (j % 3)) bits = special[(bits >> 16) % (sizeof special / 4)];
            MEM32(0x10000u + offsets[j]) = bits;
        }
        MEM32(0x100F0) = random_u32(); MEM32(0x100F4) = random_u32(); MEM32(0x100F8) = random_u32();
        const uint32_t out = outputs[test % (sizeof outputs / 4)];
        reset(0x10000, out);
        compare(actor_transform, __real_sub_006044F0, out, "actor-raw-bits-alias");
    }
    seed = saved_seed;
}

static void on_yield(void)
{
    ++yields;
    if (!yield_phase) {
        memset(&yield_snapshot, 0, sizeof yield_snapshot);
        void_regs_save(&yield_snapshot);
        yield_snap_valid = 1;
    } else {
        void_regs actual = {0};
        void_regs_save(&actual);
        assert(yield_snap_valid && !memcmp(&yield_snapshot, &actual, sizeof actual));
    }
}

static void lookup_cases(void)
{
    /* 00B65BD0 (TMap<int, V>::Find) moved to core_native.c: tests/test_contract_native.c. */
    for (unsigned test = 0; test < 20000; ++test) {
        MEM32(0x10000) = 0x11000; MEM32(0x10004) = random_u32(); MEM32(0x11000) = random_u32();
        reset(0x10000, 0);
        compare(bit_test, __real_sub_00BF8120, 0, "bit-test");
    }
}

static void compare_mode_cases(void)
{
    const uint32_t offsets[] = {0xE4,0xE8,0xEC,0x144,0x148,0x14C,0x150,0x154,0x158,0x15C};
    for (unsigned i = 0; i < 10; ++i) MEMF(0x10000u + offsets[i]) = random_float();
    reset(0x10000, 0x100E4);
    game_native_mode = 2;
    __wrap_sub_006044F0();
    MEM32(0x10000) = 0x11000; MEM32(0x10004) = 1; MEM32(0x1000C) = 0x12000;
    MEM32(0x10010) = 1; MEM32(0x12000) = 0; MEM32(0x11000) = UINT32_MAX;
    MEM32(0x11004) = 42; MEM32(0x11008) = 0x12345678; MEM32(0x13000) = 42;
    reset(0x10000, 0); __wrap_sub_00BF8120();
    assert(checked[0] == 1 && !mismatches[0] && checked[2] == 1 && !mismatches[2]);
}

static void stop_iterator_on_yield(void)
{
    ++yields;
    MEM32(0x19008) = 1;
}

static void iterator_cases(void)
{
    enum { WORLD = 0x14000, LEVELS = 0x15000, LEVEL = 0x16000, SLOTS = 0x17000, ACTOR = 0x18000, ITER = 0x19000 };
    MEM32(0x1DEE5ECu) = WORLD; MEM32(WORLD + 0x44) = LEVELS; MEM32(WORLD + 0x48) = 2;
    for (unsigned i = 0; i < 2; ++i) {
        MEM32(LEVELS + i * 4) = LEVEL + i * 0x200;
        MEM32(LEVEL + i * 0x200 + 0x3C) = SLOTS + i * 8;
        MEM32(LEVEL + i * 0x200 + 0xD8) = 0;
    }
    for (unsigned lengths = 0; lengths < 9; ++lengths)
    for (unsigned hidden = 0; hidden < 4; ++hidden)
    for (unsigned editor = 0; editor < 2; ++editor)
    for (unsigned slots = 0; slots < 81; ++slots) {
        unsigned pattern = slots;
        for (unsigned i = 0; i < 4; ++i, pattern /= 3) {
            const uint32_t actor = ACTOR + i * 0x100;
            MEM32(SLOTS + i * 4) = pattern % 3 ? actor : 0;
            MEM8(actor + 0x74) = pattern % 3 == 2 ? 1 : 0;
        }
        MEM32(LEVEL + 0x40) = lengths % 3; MEM32(LEVEL + 0x240) = lengths / 3;
        MEM32(LEVEL + 0x15C) = hidden & 1; MEM32(LEVEL + 0x35C) = hidden & 2;
        MEM32(0x1DBDD2Cu) = editor;
        for (unsigned level = 0; level < 2; ++level)
        for (int index = -1; index < 2; ++index)
        for (unsigned stopped = 0; stopped < 2; ++stopped) {
            MEM32(ITER) = (uint32_t)index; MEM32(ITER + 4) = level; MEM32(ITER + 8) = stopped;
            MEM32(ITER + 12) = random_u32(); MEM32(ITER + 16) = random_u32();
            reset(ITER, 0);
            if (!(checks % 97)) { g_backedges = RECOMP_YIELD_EVERY - 1; recomp_yield_hook = on_yield; }
            compare(actor_next, __real_sub_0088B010, ITER, "actor-iterator");
        }
    }
    /* Skip the leading actor slots, accept unrelated actor flag bits, and exercise mode 2. */
    MEM32(LEVEL + 0x40) = 0; MEM32(LEVEL + 0x240) = 2; MEM32(LEVEL + 0x2D8) = 1;
    MEM32(SLOTS + 12) = ACTOR; MEM8(ACTOR + 0x74) = 0xFE;
    MEM32(0x1DBDD2Cu) = 1;
    MEM32(ITER) = UINT32_MAX; MEM32(ITER + 4) = 0; MEM32(ITER + 8) = 0;
    reset(ITER, 0);
    compare(actor_next, __real_sub_0088B010, ITER, "actor-start-index");
    assert(MEM32(ITER + 16) == ACTOR && MEM32(ITER) == 1 && MEM32(ITER + 4) == 1);
    MEM32(ITER) = UINT32_MAX; MEM32(ITER + 4) = 0; MEM32(ITER + 8) = 0;
    reset(ITER, 0); game_native_mode = 2; __wrap_sub_0088B010();
    assert(checked[3] == 1 && !mismatches[3]);
    /* A second guest thread can stop iteration at the cooperative handover. */
    MEM32(LEVEL + 0x40) = 2; MEM32(LEVEL + 0x15C) = 0;
    MEM32(SLOTS) = 0; MEM32(SLOTS + 4) = ACTOR;
    MEM32(ITER) = UINT32_MAX; MEM32(ITER + 4) = 0; MEM32(ITER + 8) = 0;
    reset(ITER, 0); g_backedges = RECOMP_YIELD_EVERY - 1; recomp_yield_hook = stop_iterator_on_yield;
    compare(actor_next, __real_sub_0088B010, ITER, "actor-stop-at-yield");
    MEM32(ITER) = UINT32_MAX; MEM32(ITER + 4) = 0; MEM32(ITER + 8) = 0;
    reset(ITER, 0); g_backedges = RECOMP_YIELD_EVERY - 1; recomp_yield_hook = stop_iterator_on_yield;
    const unsigned before_yields = yields;
    __wrap_sub_0088B010();
    assert(yields == before_yields + 1 && checked[3] == 1 && !mismatches[3]);
}

int main(void)
{
    assert(mmap((void *)VOID_GUEST_BASE, 0x2000000, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0) == (void *)VOID_GUEST_BASE);
    actor_cases(); lookup_cases(); compare_mode_cases(); iterator_cases();
    printf("%s %u differential cases, %u mismatches, %u yield callbacks\n", failures ? "FAIL" : "PASS", checks, failures, yields);
    return failures != 0;
}
