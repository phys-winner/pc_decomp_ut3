/* Native segment/AABB intersection used by UE3 collision-octree line traces. */
#include "host.h"
#include "native_mode.inc"

extern void __real_sub_01156630(void);
static int ai_trace_mode = -1;
static unsigned long long ai_trace_checked, ai_trace_mismatches;

static __attribute__((noinline, cold)) int ai_trace_load_mode(void)
{
    char path[1100];
    snprintf(path, sizeof path, "%s/ai-trace-native", void_data_dir());
    FILE *f = fopen(path, "rb");
    int mode = 1;
    if (f) {
        const int c = fgetc(f);
        mode = c == '0' ? 0 : c == '2' ? 2 : 1;
        fclose(f);
    }
    mode = void_native_mode(mode);
    ai_trace_mode = mode;
    void_log("[ai-trace-native] segment/AABB slab test: mode %d", mode);
    return mode;
}

static inline xmm_t trace_scalar(float value)
{
    xmm_t result = {0};
    result.f32[0] = value;
    return result;
}

/* Guest pointers stay in the adapter; the kernel clips one interval against three slabs.
 * The x axis was compiled with float arithmetic; y/z compare the double lower endpoint
 * before rounding it. Keeping that distinction preserves grazing-edge decisions. */
static __attribute__((always_inline)) inline int trace_slab_kernel(const float offset[3], const float direction[3],
    const float inverse[3], const float extent[3], xmm_t xmm[6], float scratch[7], unsigned *writes,
    unsigned *backedges)
{
    float lower = 0.0f, upper = 1.0f;
    xmm[1] = trace_scalar(offset[0]);
    xmm[3] = trace_scalar(upper); xmm[4] = trace_scalar(0.0f); xmm[5] = trace_scalar(0.0f);
    scratch[2] = offset[0]; scratch[3] = offset[1]; scratch[4] = offset[2]; scratch[6] = lower;
    *writes = (1u << 2) | (1u << 3) | (1u << 4) | (1u << 6);
    *backedges = 0;
    for (unsigned axis = 0; axis < 3; ++axis) {
        xmm[0] = trace_scalar(direction[axis]);
        if (direction[axis] == 0.0f) {
            ++*backedges;
            if ((double)extent[axis] < fabs((double)offset[axis])) return 0;
            continue;
        }
        if (!axis) {
            const float center = inverse[0] * offset[0];
            const float radius = (float)(fabs((double)inverse[0]) * (double)extent[0]);
            const float enter = (-0.0f - center) - radius;
            const float leave = radius - center;
            scratch[1] = radius; *writes |= 1u << 1;
            xmm[0] = trace_scalar(enter); xmm[1] = trace_scalar(leave); xmm[2] = trace_scalar(center);
            if (enter > lower) { lower = enter; xmm[5] = xmm[0]; scratch[6] = lower; }
            if (upper > leave) { upper = leave; xmm[3] = xmm[1]; }
        } else {
            const double center = (double)inverse[axis] * (double)offset[axis];
            const double radius = fabs((double)inverse[axis]) * (double)extent[axis];
            const double enter = -center - radius;
            const float leave = (float)(radius - center);
            scratch[0] = (float)enter; scratch[1] = leave; *writes |= 3u;
            if (enter > (double)lower) {
                lower = (float)enter; xmm[5] = trace_scalar(lower);
                if (axis == 1) scratch[6] = lower;
            }
            xmm[0] = trace_scalar(leave);
            if (upper > leave) { upper = leave; xmm[3] = xmm[0]; }
        }
        if (lower > upper) return 0;
    }
    return 1;
}

static int trace_input(uint32_t pointer, uint32_t stack, float out[3])
{
    /* shortcut: unusual scratch-frame aliases and nonfinite inputs use the exact original. */
    if (pointer < stack + 8u && pointer + 12u > stack - 20u) return 0;
    for (unsigned i = 0; i < 3; ++i) {
        const uint32_t bits = MEM32(pointer + i * 4u);
        if ((bits & 0x7F800000u) == 0x7F800000u) return 0;
        out[i] = sse_f32(bits);
    }
    return 1;
}

static void trace_slab(void)
{
    /* Three backward branches can hand over the VM, including untaken reject branches.
     * The exact original supplies its scratch frame and partial registers at a handover. */
    if (recomp_yield_hook && (uint64_t)g_backedges + 3 >= RECOMP_YIELD_EVERY) {
        __real_sub_01156630(); return;
    }
    const uint32_t stack = g_esp, center_pointer = ARG(0);
    float start[3], center[3], direction[3], inverse[3], extent[3], offset[3];
    if (!trace_input(g_eax, stack, start) || !trace_input(center_pointer, stack, center) ||
        !trace_input(g_esi, stack, direction) || !trace_input(g_edx, stack, inverse) ||
        !trace_input(g_edi, stack, extent)) {
        __real_sub_01156630(); return;
    }
    for (unsigned i = 0; i < 3; ++i) offset[i] = start[i] - center[i];
    if (!isfinite(offset[0]) || !isfinite(offset[1]) || !isfinite(offset[2]) ||
        !isfinite(inverse[0] * offset[0]) || !isfinite((float)(fabs((double)inverse[0]) * extent[0]))) {
        __real_sub_01156630(); return;
    }
    xmm_t xmm[6];
    memcpy(xmm, g_xmm, sizeof xmm);
    float scratch[7]; unsigned writes, backedges;
    const int hit = trace_slab_kernel(offset, direction, inverse, extent, xmm, scratch, &writes, &backedges);
    for (unsigned i = 0; i < 7; ++i)
        if (writes & (1u << i)) MEMF(stack - 20u + i * 4u) = scratch[i];
    memcpy(g_xmm, xmm, sizeof xmm);
    g_backedges += backedges;
    g_eax = (uint32_t)hit; g_ecx = center_pointer;
    g_flag_k = FK_ADD; g_flag_a = stack - 20u; g_flag_b = 20; g_flag_cf = stack < stack - 20u;
    g_esp = stack + 4u;
    /* The production x87-slots oracle leaves the guest x87 window untouched. */
}

static __attribute__((noinline, cold)) void trace_compare(void)
{
    if (recomp_yield_hook && (uint64_t)g_backedges + 3 >= RECOMP_YIELD_EVERY) {
        __real_sub_01156630(); return;
    }
    void_regs before = {0}, expected = {0}, actual = {0};
    uint8_t saved[28], changed[28];
    const uint32_t frame = g_esp - 20;
    memcpy(saved, GPTR(frame), sizeof saved); void_regs_save(&before);
    trace_slab(); void_regs_save(&expected); memcpy(changed, GPTR(frame), sizeof changed);
    memcpy(GPTR(frame), saved, sizeof saved); void_regs_load(&before);
    __real_sub_01156630(); void_regs_save(&actual); expected.cur = actual.cur;
    ++ai_trace_checked;
    if ((memcmp(&expected, &actual, sizeof actual) || memcmp(changed, GPTR(frame), sizeof changed)) &&
        ai_trace_mismatches++ < 8)
        void_log("[ai-trace-native] slab mismatch, call %llu", ai_trace_checked);
    if (ai_trace_checked == 1 || !(ai_trace_checked & 0xFFFFFu))
        void_log("[ai-trace-native] checked: %llu calls, %llu different", ai_trace_checked, ai_trace_mismatches);
}

void __wrap_sub_01156630(void)
{
    const int mode = __builtin_expect(ai_trace_mode >= 0, 1) ? ai_trace_mode : ai_trace_load_mode();
    if (mode == 0) __real_sub_01156630();
    else if (mode == 2) trace_compare();
    else trace_slab();
}

void void_ai_trace_native_init(void)
{
    void_dispatch_add(0x01156630u, __wrap_sub_01156630);
}
