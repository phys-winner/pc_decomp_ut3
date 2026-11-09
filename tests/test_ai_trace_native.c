#include "host.h"
#include "ai_trace_native.c"
#include <assert.h>
#include <sys/mman.h>

VOID_HIDDEN recomp_ehframe_t *g_eh_frames;
VOID_HIDDEN uint32_t g_eh_obj, g_eh_info;
static uint32_t seed = 0x156630AAu;
static unsigned cases, failures, hits, misses, yields, yield_phase, recorded_count;
static void_regs recorded[3];
void void_log(const char *format, ...) { (void)format; }
const char *void_data_dir(void) { return "/nonexistent-ut3-ai-test"; }
void void_dispatch_add(uint32_t address, recomp_func_t fn) { (void)address; (void)fn; }
static uint32_t random_u32(void) { seed = seed * 1664525u + 1013904223u; return seed; }
static float random_float(void) { return (float)((int32_t)(random_u32() >> 16) - 32768) / 32.0f; }

static void reset(void)
{
    g_eax = 0x10000; g_ecx = random_u32(); g_edx = 0x10300; g_ebx = random_u32();
    g_esi = 0x10200; g_edi = 0x10400; g_ebp = random_u32(); g_esp = 0x3F004;
    g_flag_k = FK_CMP; g_flag_a = random_u32(); g_flag_b = random_u32(); g_flag_cf = random_u32() & 1;
    g_backedges = random_u32() % RECOMP_YIELD_EVERY; recomp_yield_hook = NULL;
    g_fp_top = random_u32() % 9;
    for (unsigned i = 0; i < 8; ++i) {
        g_st[i] = (double)random_float(); g_st_i64[i] = (int64_t)random_u32() << 16;
        for (unsigned j = 0; j < 4; ++j) g_xmm[i].u32[j] = random_u32();
    }
    for (unsigned i = 0; i < 96; i += 4) MEM32(g_esp - 32 + i) = random_u32();
    MEM32(g_esp) = RECOMP_RETADDR; ARG(0) = 0x10100;
}

static void compare(const char *label)
{
    void_regs before = {0}, expected = {0}, actual = {0};
    unsigned char stack[96], expected_stack[96], inputs[0x500], expected_inputs[0x500];
    const uint32_t frame = g_esp - 32;
    memcpy(stack, GPTR(frame), sizeof stack); memcpy(inputs, GPTR(0x10000), sizeof inputs);
    void_regs_save(&before); recorded_count = yield_phase = 0;
    trace_slab(); void_regs_save(&expected); memcpy(expected_stack, GPTR(frame), sizeof stack);
    memcpy(expected_inputs, GPTR(0x10000), sizeof inputs);
    memcpy(GPTR(frame), stack, sizeof stack); memcpy(GPTR(0x10000), inputs, sizeof inputs);
    void_regs_load(&before); yield_phase = 1;
    __real_sub_01156630(); void_regs_save(&actual);
    assert(yield_phase == recorded_count + 1);
    ++cases; if (g_eax) ++hits; else ++misses;
    if (memcmp(&expected, &actual, sizeof actual) || memcmp(expected_stack, GPTR(frame), sizeof stack) ||
        memcmp(expected_inputs, GPTR(0x10000), sizeof inputs)) {
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

static void on_yield(void)
{
    void_regs state = {0}; void_regs_save(&state); ++yields;
    if (!yield_phase) { assert(recorded_count < 3); recorded[recorded_count++] = state; }
    else { assert(yield_phase <= recorded_count); assert(!memcmp(&recorded[yield_phase++ - 1], &state, sizeof state)); }
    /* Another guest thread may change geometry while this trace is suspended. */
    MEM32(0x10204) = 0; MEM32(0x10408) = 0x3F000000;
}

static void fixture(unsigned n)
{
    reset();
    for (unsigned axis = 0; axis < 3; ++axis) {
        const float direction = n & (1u << axis) ? random_float() : n & 8 ? -0.0f : 0.0f;
        const float center = random_float();
        MEMF(0x10000 + axis * 4) = center + random_float() * (n & 16 ? 0.01f : 1.0f);
        MEMF(0x10100 + axis * 4) = center;
        MEMF(0x10200 + axis * 4) = direction;
        MEMF(0x10300 + axis * 4) = direction ? 1.0f / direction : 0.0f;
        MEMF(0x10400 + axis * 4) = fabsf(random_float()) * (n & 32 ? 0.01f : 1.0f);
    }
}

static void random_cases(void)
{
    const uint32_t special[] = {0,0x80000000,1,0x80000001,0x7F7FFFFF,0xFF7FFFFF,
        0x7F800000,0xFF800000,0x7FC00123,0xFFC00456,0x7F800001};
    for (unsigned n = 0; n < 300000; ++n) {
        fixture(n);
        if (n >= 150000) for (unsigned vector = 0; vector < 5; ++vector)
            for (unsigned axis = 0; axis < 3; ++axis) {
                const uint32_t raw = random_u32();
                MEM32(0x10000 + vector * 0x100 + axis * 4) = n % 3 ? raw : special[(raw >> 16) % (sizeof special / 4)];
            }
        compare("random slab");
    }
}

static void boundary_cases(void)
{
    /* Each axis can independently enter before, at, or after either interval boundary. */
    const float point[] = {-2.0f,-1.0f,-0.0f,0.0f,1.0f,2.0f};
    for (unsigned n = 0; n < 216 * 8; ++n) {
        fixture(n);
        for (unsigned axis = 0, value = n / 8; axis < 3; ++axis, value /= 6) {
            const float direction = n & (1u << axis) ? 2.0f : 0.0f;
            MEMF(0x10000 + axis * 4) = point[value % 6]; MEMF(0x10100 + axis * 4) = 0.0f;
            MEMF(0x10200 + axis * 4) = direction; MEMF(0x10300 + axis * 4) = direction ? 0.5f : 0.0f;
            MEMF(0x10400 + axis * 4) = 1.0f;
        }
        compare("boundary slab");
    }
    for (unsigned axis = 0; axis < 3; ++axis) for (unsigned edge = 0; edge < 12; ++edge) {
        fixture(63);
        for (unsigned i = 0; i < 3; ++i) {
            MEMF(0x10000 + i * 4) = 0; MEMF(0x10100 + i * 4) = 0;
            MEMF(0x10200 + i * 4) = 1; MEMF(0x10300 + i * 4) = 1; MEMF(0x10400 + i * 4) = 1;
        }
        const float value = edge < 6 ? -2.0f : 1.0f;
        MEMF(0x10000 + axis * 4) = edge % 3 == 0 ? value : nextafterf(value, edge % 3 == 1 ? -INFINITY : INFINITY);
        if (edge & 1) MEMF(0x10200 + axis * 4) = MEMF(0x10300 + axis * 4) = 0;
        compare("grazing slab");
    }
}

static void alias_cases(void)
{
    for (unsigned n = 0; n < 10000; ++n) {
        fixture(n);
        uint32_t *pointers[] = {&g_eax,&g_esi,&g_edx,&g_edi};
        if (n & 1) *pointers[(n / 2) % 4] = g_esp - 28 + ((n / 8) % 18) * 4;
        else *pointers[(n / 2) % 4] = 0x10000 + ((n / 8) % 3) * 4;
        if (!(n % 7)) ARG(0) = g_esp - 12;
        compare("alias slab");
    }
}

static void yield_cases(void)
{
    for (unsigned n = 0; n < 12000; ++n) {
        fixture(n); recomp_yield_hook = on_yield;
        g_backedges = RECOMP_YIELD_EVERY - 1 - n % 4;
        compare("yield slab");
    }
}

int main(void)
{
    void *window = mmap((void *)VOID_GUEST_BASE, 0x2000000, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    assert(window == (void *)VOID_GUEST_BASE);
    MEMF(0x17343D8) = 1.0f; MEM32(0x173D5BC) = 0x80000000;
    random_cases(); boundary_cases(); alias_cases(); yield_cases();
    ai_trace_mode = 2; fixture(63); __wrap_sub_01156630();
    assert(ai_trace_checked == 1 && !ai_trace_mismatches);
    assert(!failures && hits && misses && yields);
    printf("PASS native AI trace: %u differential cases (%u hits/%u misses), %u yields; GPR/XMM/x87/flags/stack exact\n", cases, hits, misses, yields);
    munmap(window, 0x2000000);
    return 0;
}
