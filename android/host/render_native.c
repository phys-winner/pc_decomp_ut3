/* UE3 render math, recovered from UT3.exe 00486030 / 0117C3B0 / 0117D890. Keep the SSE
 * operation order and outgoing registers: callers still use the guest ABI.
 * render-native: 0 = lifted, 2 = compare every call, absent/1 = native. */
#include "host.h"
#include "native_mode.inc"

typedef float render_vec4 __attribute__((vector_size(16)));
typedef int32_t render_mask4 __attribute__((vector_size(16)));

extern void __real_sub_00486030(void);
extern void __real_sub_0117C3B0(void);
extern void __real_sub_0117D890(void);

static int render_mode = -1;
static unsigned long long render_checked[2], render_mismatches[2];

static __attribute__((cold, noinline)) int render_mode_load(void)
{
    char path[1100];
    snprintf(path, sizeof path, "%s/render-native", void_data_dir());
    FILE *file = fopen(path, "rb");
    int mode = file ? fgetc(file) : '1';
    if (file) fclose(file);
    render_mode = void_native_mode(mode == '0' ? 0 : mode == '2' ? 2 : 1);
    void_log("[native-render] matrix and sphere culling: mode %d", render_mode);
    return render_mode;
}

static inline int render_native_mode(void)
{
    return __builtin_expect(render_mode >= 0, 1) ? render_mode : render_mode_load();
}

static inline render_vec4 render_load(const float *p)
{
    render_vec4 v;
    memcpy(&v, p, sizeof v);
    return v;
}

static inline render_vec4 render_splat(float f)
{
    return (render_vec4){f, f, f, f};
}

static inline void render_xmm(unsigned index, render_vec4 value)
{
    memcpy(&g_xmm[index], &value, sizeof value);
}

static void render_matrix(void)
{
    const uint32_t left = g_ecx, out = ARG(0), right = ARG(1);
    float a[16];
    memcpy(a, GPTR(left), sizeof a);
    const float *b = (const float *)GPTR(right);
    const render_vec4 b0 = render_load(b), b1 = render_load(b + 4);
    const render_vec4 b2 = render_load(b + 8), b3 = render_load(b + 12);
    render_vec4 rows[4];
    for (unsigned row = 0; row < 3; ++row) {
        const float *r = a + row * 4;
        rows[row] = ((b0 * render_splat(r[0]) + b1 * render_splat(r[1])) +
            b2 * render_splat(r[2])) + b3 * render_splat(r[3]);
        render_xmm(4 + row, rows[row]);
    }
    const render_vec4 z = b2 * render_splat(a[14]);
    const render_vec4 partial = (b0 * render_splat(a[12]) + b1 * render_splat(a[13])) + z;
    rows[3] = b3 * render_splat(a[15]) + partial;
    render_xmm(0, partial);
    render_xmm(1, render_splat(a[15]));
    render_xmm(2, z);
    render_xmm(3, rows[3]);
    render_xmm(7, render_splat(a[14]));
    /* Both operands are consumed before the first store, including in-place products. */
    memcpy(GPTR(out), rows, sizeof rows);
    g_eax = out;
    g_edx = right;
    g_flag_k = FK_CMP;
    g_flag_a = (g_esp - 4u) & ~15u;
    g_flag_b = 64;
    g_flag_cf = g_flag_a < g_flag_b;
    g_esp += 12;
}

static void render_sphere(void)
{
    const uint32_t sphere = ARG(0), planes = MEM32(g_ecx + 12);
    const int32_t count = (int32_t)MEM32(g_ecx + 16);
    const render_vec4 center = render_load((const float *)GPTR(sphere));
    float radius;
    const uint32_t radius_bits = ARG(1);
    memcpy(&radius, &radius_bits, 4);
    const render_vec4 x = render_splat(center[0]), y = render_splat(center[1]);
    const render_vec4 z = render_splat(center[2]), r = render_splat(radius);
    render_xmm(0, z);
    render_xmm(4, r);
    render_xmm(5, x);
    render_xmm(6, y);
    render_xmm(7, z);
    g_eax = 1;
    g_ecx = (uint32_t)count;
    g_edx = planes;
    g_flag_k = FK_TEST;
    g_flag_a = g_flag_b = (uint32_t)count;
    g_flag_cf = 0;
    /* The engine packs four planes per 64-byte block and tests padded lanes too. */
    for (int32_t i = 0; i < count; i += 4) {
        const float *p = (const float *)GPTR(g_edx);
        const render_vec4 pz = render_load(p + 8) * z, pd = render_load(p + 12);
        const render_vec4 distance = ((render_load(p + 4) * y + render_load(p) * x) + pz) - pd;
        const render_mask4 outside = r < distance;
        render_xmm(0, distance);
        memcpy(&g_xmm[1], &outside, sizeof outside);
        render_xmm(2, pz);
        render_xmm(3, pd);
        g_edx += 64;
        if (outside[0] | outside[1] | outside[2] | outside[3]) {
            g_eax = 0;
            g_flag_k = FK_TEST;
            g_flag_a = g_flag_b = g_flag_cf = 0;
            break;
        }
        g_flag_k = FK_CMP;
        g_flag_a = (uint32_t)i + 4u;
        g_flag_b = (uint32_t)count;
        g_flag_cf = g_flag_a < g_flag_b;
        RECOMP_BACKEDGE();
    }
    g_esp += 12;
}

static __attribute__((cold, noinline)) void render_compare(unsigned kernel)
{
    void_regs entry = {0}, expected = {0}, actual = {0};
    const uint32_t out = kernel == 0 ? ARG(0) : 0;
    uint8_t before[64], after[64];
    if (kernel == 0) memcpy(before, GPTR(out), sizeof before);
    void_regs_save(&entry);
    if (kernel == 0) render_matrix(); else render_sphere();
    void_regs_save(&expected);
    if (kernel == 0) {
        memcpy(after, GPTR(out), sizeof after);
        memcpy(GPTR(out), before, sizeof before);
    }
    void_regs_load(&entry);
    if (kernel == 0) __real_sub_00486030(); else __real_sub_0117C3B0();
    void_regs_save(&actual);
    expected.cur = actual.cur;
    const int different = memcmp(&expected, &actual, sizeof actual) ||
        (kernel == 0 && memcmp(after, GPTR(out), sizeof after));
    ++render_checked[kernel];
    if (different && ++render_mismatches[kernel] <= 8)
        void_log("[native-render] %s mismatch at call %llu", kernel ? "sphere" : "matrix", render_checked[kernel]);
    if ((render_checked[kernel] & 65535u) == 0)
        void_log("[native-render] %s checked %llu, different %llu", kernel ? "sphere" : "matrix",
            render_checked[kernel], render_mismatches[kernel]);
}

void __wrap_sub_00486030(void)
{
    const int mode = render_native_mode();
    if (mode == 0) __real_sub_00486030();
    else if (mode == 2) render_compare(0);
    else render_matrix();
}

void __wrap_sub_0117C3B0(void)
{
    const int mode = render_native_mode();
    const int32_t count = (int32_t)MEM32(g_ecx + 16);
    /* Keep the original scratch frame/registers at VM handovers, also avoiding a double yield in check mode. */
    const int handover = count > 0 && recomp_yield_hook &&
        (uint64_t)g_backedges + ((uint32_t)count + 3u) / 4u >= RECOMP_YIELD_EVERY;
    if (mode == 0 || handover) __real_sub_0117C3B0();
    else if (mode == 2) render_compare(1);
    else render_sphere();
}

/* Only the callback-free sphere rejection of the light/primitive predicate is native.
 * All survivors run the original from its untouched entry state, including its other eligibility checks. */
void __wrap_sub_0117D890(void)
{
    if (render_native_mode() != 1) { __real_sub_0117D890(); return; }
    const uint32_t sp = g_esp, self = g_ecx, primitive = ARG(0);
    if (sp < 64u || sp > UINT32_MAX - 8u || !self || !primitive ||
        self > UINT32_MAX - 31u || primitive > UINT32_MAX - 43u) {
        __real_sub_0117D890(); return;
    }
    const uint32_t frame = ((sp - 4u) & ~15u) - 32u;
    /* The original pushes EBP before reading the primitive, and three saved registers before the light. */
    if ((self + 16u < sp && (uint64_t)self + 32u > frame) ||
        (primitive + 16u < sp && (uint64_t)primitive + 44u > frame)) {
        __real_sub_0117D890(); return;
    }
    const render_vec4 light = render_load((const float *)GPTR(self + 16u));
    const render_vec4 center = render_load((const float *)GPTR(primitive + 16u));
    float primitive_radius;
    memcpy(&primitive_radius, GPTR(primitive + 40u), sizeof primitive_radius);
    const render_vec4 delta = light - center, squared = delta * delta;
    const render_vec4 dy2 = render_splat(squared[1]);
    const render_vec4 distance = (render_splat(squared[2]) + dy2) + render_splat(squared[0]);
    const render_vec4 radius = render_splat(light[3]) + render_splat(primitive_radius);
    if (!(radius[0] * radius[0] < distance[0])) { __real_sub_0117D890(); return; }
    MEM32(sp - 4u) = g_ebp;
    MEM32(frame + 8u) = g_ebx;
    MEM32(frame + 4u) = g_esi;
    MEM32(frame) = g_edi;
    MEM32(frame + 12u) = primitive + 16u;
    const render_mask4 outside = { -1, -1, -1, -1 };
    memcpy(&g_xmm[0], &outside, sizeof outside);
    render_xmm(1, radius);
    render_xmm(2, distance);
    render_xmm(3, dy2);
    g_eax = 0;
    g_ecx = 15;
    g_flag_k = FK_TEST;
    g_flag_a = g_flag_b = g_flag_cf = 0;
    g_esp = sp + 8u;
}

void void_render_native_init(void)
{
    void_dispatch_add(0x00486030u, __wrap_sub_00486030);
    void_dispatch_add(0x0117C3B0u, __wrap_sub_0117C3B0);
    void_dispatch_add(0x0117D890u, __wrap_sub_0117D890);
}
