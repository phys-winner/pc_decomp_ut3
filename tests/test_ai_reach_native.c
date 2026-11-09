#include "host.h"
#include "ai_reach_native.c"
#include <assert.h>
#include <sys/mman.h>

VOID_HIDDEN recomp_ehframe_t *g_eh_frames;
VOID_HIDDEN uint32_t g_eh_obj, g_eh_info;
static uint32_t seed = 0xA6C98E13u;
static unsigned cases, failures, yields, yield_phase, recorded_count;
static void_regs recorded[128];
extern void raw_sub_01079C10(void);

void void_log(const char *format, ...) { (void)format; }
const char *void_data_dir(void) { return "/nonexistent-ut3-ai-test"; }
void void_dispatch_add(uint32_t address, recomp_func_t fn) { (void)address; (void)fn; }
static uint32_t random_u32(void) { seed = seed * 1664525u + 1013904223u; return seed; }
static float random_float(void) { return (float)((int32_t)(random_u32() % 65537u) - 32768) * 0.03125f; }

static void reset(void)
{
    g_eax = random_u32(); g_ecx = 0x10000; g_edx = random_u32(); g_ebx = random_u32();
    g_esi = random_u32(); g_edi = random_u32(); g_ebp = random_u32(); g_esp = 0x3F004;
    g_flag_k = FK_CMP; g_flag_a = random_u32(); g_flag_b = random_u32(); g_flag_cf = random_u32() & 1;
    g_backedges = 0; recomp_yield_hook = NULL;
    g_fp_top = random_u32() % 5;
    for (unsigned i = 0; i < 8; ++i) {
        g_st[i] = (double)random_float(); g_st_i64[i] = (int64_t)random_u32() << 16;
        for (unsigned j = 0; j < 4; ++j) g_xmm[i].u32[j] = random_u32();
    }
    for (unsigned i = 0; i < 64; i += 4) MEM32(g_esp - 16 + i) = random_u32();
    MEM32(g_esp) = RECOMP_RETADDR;
}

static void on_yield(void)
{
    void_regs state = {0};
    void_regs_save(&state);
    ++yields;
    if (!yield_phase) {
        assert(recorded_count < 128);
        recorded[recorded_count++] = state;
    } else {
        assert(yield_phase <= recorded_count);
        assert(!memcmp(&recorded[yield_phase++ - 1], &state, sizeof state));
    }
    /* A real VM handover may alter the next list entry. Both versions must reread it. */
    if (g_edx < 64) MEM32(0x12000 + g_edx * 4) = 0x14000;
}

static void compare(void (*native)(void), void (*original)(void), const char *label)
{
    void_regs before = {0}, expected = {0}, actual = {0};
    unsigned char stack[64], expected_stack[64];
    uint32_t list[64];
    const uint32_t frame = g_esp - 16;
    memcpy(stack, GPTR(frame), sizeof stack);
    memcpy(list, GPTR(0x12000), sizeof list);
    void_regs_save(&before);
    recorded_count = yield_phase = 0;
    native();
    void_regs_save(&expected);
    memcpy(expected_stack, GPTR(frame), sizeof stack);
    memcpy(GPTR(frame), stack, sizeof stack);
    memcpy(GPTR(0x12000), list, sizeof list);
    void_regs_load(&before);
    yield_phase = 1;
    original();
    void_regs_save(&actual);
    if (original == raw_sub_01079C10) {
        /* Raw push/pop clears four inactive slots; production x87-slots leaves them untouched. */
        assert(before.fp_top <= 4);
        memcpy(actual.st + 4, expected.st + 4, 4 * sizeof(double));
        memcpy(actual.st_i64 + 4, expected.st_i64 + 4, 4 * sizeof(int64_t));
    }
    assert(yield_phase == recorded_count + 1);
    ++cases;
    if (memcmp(&expected, &actual, sizeof actual) || memcmp(expected_stack, GPTR(frame), sizeof stack)) {
        if (++failures <= 5) {
            fprintf(stderr, "%s case %u differs\n", label, cases);
            for (unsigned i = 0; i < sizeof actual / 4; ++i)
                if (((uint32_t *)&expected)[i] != ((uint32_t *)&actual)[i])
                    fprintf(stderr, "state +%u: %08x != %08x\n", i * 4,
                        ((uint32_t *)&expected)[i], ((uint32_t *)&actual)[i]);
            for (unsigned i = 0; i < sizeof stack / 4; ++i)
                if (((uint32_t *)expected_stack)[i] != MEM32(frame + i * 4))
                    fprintf(stderr, "stack +%u: %08x != %08x\n", i * 4,
                        ((uint32_t *)expected_stack)[i], MEM32(frame + i * 4));
        }
    }
}

static void supports_cases(void)
{
    const uint32_t limits[] = {0, 1, 2, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF};
    for (unsigned n = 0; n < 50000; ++n) {
        reset();
        for (unsigned i = 0; i < 4; ++i) {
            MEM32(0x10068 + i * 4) = n < 1296 ? limits[(n / (i == 0 ? 1 : i == 1 ? 6 : i == 2 ? 36 : 216)) % 6] : random_u32();
            ARG(i) = n & 1 ? MEM32(0x10068 + i * 4) : random_u32();
        }
        if (!(n % 4)) ARG(2) |= MEM32(0x10070);
        compare(reach_supports, __real_sub_006A97C0, "supports");
    }
}

static void lookup_cases(void)
{
    for (unsigned n = 0; n < 30000; ++n) {
        reset();
        const int32_t count = (int32_t)(n % 67) - 2;
        MEM32(0x101F0) = 0x12000; MEM32(0x101F4) = (uint32_t)count;
        ARG(0) = n % 7 ? 0x16000 : 0;
        for (unsigned i = 0; i < 64; ++i) {
            const uint32_t spec = 0x14000 + i * 0x80;
            MEM32(0x12000 + i * 4) = (random_u32() >> 24) % 4 ? spec : 0;
            MEM32(spec + 0x54) = (random_u32() >> 24) % 8 ? 0x18000 : ARG(0);
        }
        if (!(n % 3)) { recomp_yield_hook = on_yield; g_backedges = RECOMP_YIELD_EVERY - 1; }
        compare(nav_reach_spec, __real_sub_00EA3C60, "nav lookup");
    }
}

static void cost_fixture(unsigned n)
{
    reset();
    MEM32(0x10054) = 0x11000;
    MEM32(0x10040) = random_u32();
    ARG(0) = random_u32(); ARG(1) = n % 5 ? 0x13000 : 0x10044; ARG(2) = 0x15000; ARG(3) = random_u32();
    for (unsigned i = 0; i < 3; ++i) {
        MEMF(0x10044 + i * 4) = random_float() * 0.001f;
        MEMF(0x13000 + i * 4) = random_float() * 0.001f;
        MEMF(0x110E4 + i * 4) = random_float();
        MEMF(0x150E4 + i * 4) = random_float();
    }
    if (n % 5 == 1) {
        /* Direction and destination can alias the argument area overwritten by the original. */
        ARG(1) = g_esp + 4;
    }
}

static void cost_cases(void)
{
    const uint32_t fields[] = {0x10044,0x10048,0x1004C,0x13000,0x13004,0x13008,
        0x110E4,0x110E8,0x110EC,0x150E4,0x150E8,0x150EC};
    const uint32_t special[] = {0,0x80000000,1,0x80000001,0x7F7FFFFF,0xFF7FFFFF,
        0x7F800000,0xFF800000,0x7FC00123,0xFFC00456,0x7F800001};
    for (unsigned kind = 0; kind < 2; ++kind) {
        void (*native)(void) = kind ? reach_cost_xy : reach_cost_3d;
        void (*original)(void) = kind ? __real_sub_01079F80 : __real_sub_01079C10;
        for (unsigned n = 0; n < 60000; ++n) {
            cost_fixture(n);
            if (n >= 30000) for (unsigned i = 0; i < 12; ++i) {
                const uint32_t raw = random_u32();
                MEM32(fields[i]) = n % 3 ? raw : special[(raw >> 16) % (sizeof special / 4)];
            }
            if (n < 132) MEM32(fields[n % 12]) = special[n / 12];
            compare(native, original, kind ? "cost XY" : "cost 3D");
        }
        /* Clamp equality and adjacent values, coincident nodes, signed-zero coordinates. */
        for (unsigned n = 0; n < 16; ++n) {
            cost_fixture(0); ARG(1) = 0x13000;
            for (unsigned i = 0; i < 3; ++i) {
                MEMF(0x10044 + i * 4) = i ? 0.0f : 1.0f;
                MEMF(0x13000 + i * 4) = i ? 0.0f : (float)((int)n - 8) * 0.25f;
                MEM32(0x110E4 + i * 4) = n & 1 ? 0x80000000 : 0;
                MEM32(0x150E4 + i * 4) = 0;
            }
            compare(native, original, "clamp boundary");
        }
    }
}

static void raw_cost_cases(void)
{
    for (unsigned n = 0; n < 10000; ++n) {
        cost_fixture(n);
        compare(reach_cost_3d, raw_sub_01079C10, "raw x87 cost");
    }
}

int main(void)
{
    void *window = mmap((void *)VOID_GUEST_BASE, 0x2000000, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    assert(window == (void *)VOID_GUEST_BASE);
    MEMF(0x17343D8) = 1.0f; MEMF(0x1750298) = 0.1f; MEMF(0x175251C) = 0.01f; MEMF(0x1752570) = 2.0f;
    supports_cases(); lookup_cases(); cost_cases(); raw_cost_cases();
    ai_reach_mode = 2;
    reset(); for (unsigned i = 0; i < 4; ++i) ARG(i) = MEM32(0x10068 + i * 4);
    __wrap_sub_006A97C0();
    reset(); MEM32(0x101F4) = 0; __wrap_sub_00EA3C60();
    cost_fixture(0); __wrap_sub_01079C10(); cost_fixture(0); __wrap_sub_01079F80();
    for (unsigned i = 0; i < 4; ++i) assert(ai_reach_checked[i] == 1 && !ai_reach_mismatches[i]);
    assert(!failures);
    printf("PASS native AI reach: %u differential cases, %u yields; output/GPR/XMM/x87/flags/stack exact\n", cases, yields);
    munmap(window, 0x2000000);
    return 0;
}
