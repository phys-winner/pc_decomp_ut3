/* Native ReachSpec predicates, directional costs and NavigationPoint edge lookup. */
#include "host.h"
#include "native_mode.inc"

extern void __real_sub_006A97C0(void);
extern void __real_sub_00EA3C60(void);
extern void __real_sub_01079C10(void);
extern void __real_sub_01079F80(void);

static int ai_reach_mode = -1;
static unsigned long long ai_reach_checked[4], ai_reach_mismatches[4];

static __attribute__((noinline, cold)) int ai_reach_load_mode(void)
{
    char path[1100];
    snprintf(path, sizeof path, "%s/ai-reach-native", void_data_dir());
    FILE *f = fopen(path, "rb");
    int mode = 1;
    if (f) {
        const int c = fgetc(f);
        mode = c == '0' ? 0 : c == '2' ? 2 : 1;
        fclose(f);
    }
    mode = void_native_mode(mode);
    ai_reach_mode = mode;
    void_log("[ai-reach-native] reach support, edge lookup and directional costs: mode %d", mode);
    return mode;
}

static inline int reach_mode(void)
{
    return __builtin_expect(ai_reach_mode >= 0, 1) ? ai_reach_mode : ai_reach_load_mode();
}

static void reach_supports(void)
{
    g_eax = MEM32(g_ecx + 0x68);
    if ((int32_t)g_eax >= (int32_t)ARG(0)) {
        g_edx = MEM32(g_ecx + 0x6C);
        if ((int32_t)g_edx >= (int32_t)ARG(1)) {
            g_eax = MEM32(g_ecx + 0x70);
            g_edx = g_eax & ARG(2);
            if (g_edx == g_eax) {
                g_eax = MEM32(g_ecx + 0x74);
                const uint32_t maximum = ARG(3);
                if ((int32_t)g_eax <= (int32_t)maximum) {
                    g_flag_k = FK_CMP; g_flag_a = g_eax; g_flag_b = maximum;
                    g_flag_cf = g_eax < maximum;
                    g_eax = 1;
                    g_esp += 20;
                    return;
                }
            }
        }
    }
    g_eax = 0;
    g_flag_k = FK_TEST; g_flag_a = g_flag_b = g_flag_cf = 0;
    g_esp += 20;
}

static void nav_reach_spec(void)
{
    PUSH32(g_esp, g_ebx);
    PUSH32(g_esp, g_esi);
    g_esi = MEM32(g_ecx + 0x1F4);
    g_edx = 0;
    PUSH32(g_esp, g_edi);
    if ((int32_t)g_esi > 0) {
        g_ecx = MEM32(g_ecx + 0x1F0);
        g_edi = MEM32(g_esp + 16);
        g_eax = g_ecx;
        do {
            const uint32_t spec = MEM32(g_eax);
            if (spec) {
                g_ebx = spec;
                if (MEM32(spec + 0x54) == g_edi) {
                    g_flag_k = FK_CMP; g_flag_a = g_flag_b = g_edi; g_flag_cf = 0;
                    g_eax = MEM32(g_ecx + g_edx * 4u);
                    g_edi = POP32_VAL(g_esp); g_esi = POP32_VAL(g_esp); g_ebx = POP32_VAL(g_esp);
                    g_esp += 8;
                    return;
                }
            }
            ++g_edx;
            g_eax += 4;
            /* Keep the original saved frame and register values at VM handovers. */
            RECOMP_BACKEDGE();
        } while ((int32_t)g_edx < (int32_t)g_esi);
    }
    g_edi = POP32_VAL(g_esp); g_esi = POP32_VAL(g_esp); g_ebx = POP32_VAL(g_esp);
    g_eax = 0;
    g_flag_k = FK_TEST; g_flag_a = g_flag_b = g_flag_cf = 0;
    g_esp += 8;
}

static inline void reach_scalar(unsigned xmm, float value)
{
    g_xmm[xmm] = (xmm_t){0};
    g_xmm[xmm].f32[0] = value;
}

static int reach_finite(uint32_t address, unsigned count)
{
    for (unsigned i = 0; i < count; ++i)
        if ((MEM32(address + i * 4u) & 0x7F800000u) == 0x7F800000u) return 0;
    return 1;
}

static inline void reach_cost_result(float cost)
{
    const uint32_t converted = (uint32_t)sse_cvt_i32((double)cost, 1);
    g_ecx = ARG(3);
    g_eax = converted + g_ecx;
    g_flag_k = FK_ADD; g_flag_a = converted; g_flag_b = g_ecx; g_flag_cf = g_eax < converted;
    g_esp += 20;
}

static void reach_cost_3d(void)
{
    const uint32_t spec = g_ecx, direction = ARG(1), end = MEM32(spec + 0x54), goal = ARG(2);
    /* shortcut: preserve original NaN payload selection outside finite navigation data. */
    if (!reach_finite(spec + 0x44, 3) || !reach_finite(direction, 3) ||
        !reach_finite(end + 0xE4, 3) || !reach_finite(goal + 0xE4, 3)) {
        __real_sub_01079C10();
        return;
    }
    const float dot = (MEMF(spec + 0x4C) * MEMF(direction + 8) +
        MEMF(spec + 0x48) * MEMF(direction + 4)) + MEMF(direction) * MEMF(spec + 0x44);
    const float raw = MEMF(0x17343D8u) - dot;
    if (!isfinite(raw)) { __real_sub_01079C10(); return; }
    float bound = MEMF(0x1750298u), weight;
    if (bound > raw) weight = bound;
    else {
        bound = MEMF(0x1752570u);
        weight = bound <= raw ? bound : raw;
    }
    reach_scalar(0, raw); reach_scalar(1, bound);
    MEMF(g_esp + 8) = weight;
    /* The original evaluates the distance in x87 doubles, rounding only the final cost to float. */
    g_edx = ARG(2);
    const uint32_t target = MEM32(spec + 0x54);
    const double x = (double)MEMF(target + 0xE4) - (double)MEMF(g_edx + 0xE4);
    const double y = (double)MEMF(target + 0xE8) - (double)MEMF(g_edx + 0xE8);
    const double z = (double)MEMF(target + 0xEC) - (double)MEMF(g_edx + 0xEC);
    const double distance = sqrt((y * y + z * z) + x * x);
    const float cost = (float)(distance * (double)weight + (double)weight * (double)(int32_t)MEM32(spec + 0x40));
    MEMF(g_esp + 8) = cost;
    /* The production x87-slots original uses locals and leaves the guest x87 window untouched. */
    reach_cost_result(cost);
}

static void reach_cost_xy(void)
{
    const uint32_t spec = g_ecx, direction = ARG(1), end = MEM32(spec + 0x54), goal = ARG(2);
    if (!reach_finite(spec + 0x44, 3) || !reach_finite(direction, 3) ||
        !reach_finite(end + 0xE4, 2) || !reach_finite(goal + 0xE4, 2)) {
        __real_sub_01079F80();
        return;
    }
    const float x = MEMF(end + 0xE4) - MEMF(goal + 0xE4);
    const float y = MEMF(end + 0xE8) - MEMF(goal + 0xE8);
    const float distance_squared = y * y + x * x;
    const float dot = (MEMF(spec + 0x4C) * MEMF(direction + 8) +
        MEMF(spec + 0x48) * MEMF(direction + 4)) + MEMF(direction) * MEMF(spec + 0x44);
    const float raw = MEMF(0x17343D8u) - dot;
    if (!isfinite(raw)) { __real_sub_01079F80(); return; }
    float bound = MEMF(0x175251Cu), weight;
    if (bound > raw) weight = bound;
    else {
        bound = MEMF(0x1752570u);
        weight = bound > raw ? raw : bound;
    }
    const float cost = weight * distance_squared;
    reach_scalar(0, cost); reach_scalar(1, distance_squared); reach_scalar(2, bound);
    g_edx = goal;
    MEMF(g_esp + 12) = cost;
    reach_cost_result(cost);
}

static void reach_compare(unsigned kind, void (*native)(void), void (*original)(void))
{
    if (kind == 1 && recomp_yield_hook && (int32_t)MEM32(g_ecx + 0x1F4) > 0 &&
        (uint64_t)g_backedges + MEM32(g_ecx + 0x1F4) >= RECOMP_YIELD_EVERY) {
        original();
        return;
    }
    void_regs before = {0}, expected = {0}, actual = {0};
    uint8_t saved_stack[32], expected_stack[32];
    const uint32_t stack = g_esp - 12;
    memcpy(saved_stack, GPTR(stack), sizeof saved_stack);
    void_regs_save(&before);
    native();
    void_regs_save(&expected);
    memcpy(expected_stack, GPTR(stack), sizeof expected_stack);
    memcpy(GPTR(stack), saved_stack, sizeof saved_stack);
    void_regs_load(&before);
    original();
    void_regs_save(&actual);
    expected.cur = actual.cur;
    ++ai_reach_checked[kind];
    if ((memcmp(&expected, &actual, sizeof actual) || memcmp(expected_stack, GPTR(stack), sizeof expected_stack)) &&
        ai_reach_mismatches[kind]++ < 8)
        void_log("[ai-reach-native] kind %u mismatch, call %llu", kind, ai_reach_checked[kind]);
    if (ai_reach_checked[kind] == 1 || !(ai_reach_checked[kind] & 0xFFFFFu))
        void_log("[ai-reach-native] kind %u checked: %llu calls, %llu different", kind,
            ai_reach_checked[kind], ai_reach_mismatches[kind]);
}

#define REACH_WRAPPER(address, kind, native) \
    void __wrap_sub_##address(void) { \
        const int mode = reach_mode(); \
        if (mode == 0) __real_sub_##address(); \
        else if (mode == 2) reach_compare(kind, native, __real_sub_##address); \
        else native(); \
    }
REACH_WRAPPER(006A97C0, 0, reach_supports)
REACH_WRAPPER(00EA3C60, 1, nav_reach_spec)
REACH_WRAPPER(01079C10, 2, reach_cost_3d)
REACH_WRAPPER(01079F80, 3, reach_cost_xy)
#undef REACH_WRAPPER

void void_ai_reach_native_init(void)
{
    void_dispatch_add(0x006A97C0u, __wrap_sub_006A97C0);
    void_dispatch_add(0x00EA3C60u, __wrap_sub_00EA3C60);
    void_dispatch_add(0x01079C10u, __wrap_sub_01079C10);
    void_dispatch_add(0x01079F80u, __wrap_sub_01079F80);
}
