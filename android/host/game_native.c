/* Native UE3 helpers used by actor updates and frame statistics. Guest data/ABI stay 32-bit. */
#include "host.h"
#include "native_mode.inc"

extern void __real_sub_006044F0(void);
extern void __real_sub_00BF8120(void);
extern void __real_sub_0088B010(void);

static int game_native_mode = -1;

static __attribute__((noinline, cold)) int game_native_load_mode(void)
{
    char path[1100];
    snprintf(path, sizeof path, "%s/game-native", void_data_dir());
    FILE *f = fopen(path, "rb");
    int mode = 1;
    if (f) {
        const int c = fgetc(f);
        mode = c == '0' ? 0 : c == '2' ? 2 : 1;
        fclose(f);
    }
    mode = void_native_mode(mode);
    game_native_mode = mode;
    void_log("[game-native] actor transform, iterator and bit test: mode %d", mode);
    return mode;
}

static inline int game_mode(void)
{
    return __builtin_expect(game_native_mode >= 0, 1) ? game_native_mode : game_native_load_mode();
}

/* AActor's scale/rotator/prepivot/location transform. Keep each original SSE rounding step. */
static void actor_transform(void)
{
    const uint32_t self = g_ecx, out = ARG(0);
    union { float f[10]; uint32_t u[10]; } values;
    memcpy(values.u, GPTR(self + 0xE4), 3 * sizeof(float));
    memcpy(values.u + 3, GPTR(self + 0x144), 7 * sizeof(float));
    unsigned nonfinite = 0;
    for (unsigned i = 0; i < 10; ++i) nonfinite |= (values.u[i] & 0x7F800000u) == 0x7F800000u;
    /* shortcut: nonfinite actors keep exact NaN payloads via the original; extend with a bit-exact kernel. */
    if (nonfinite) {
        __real_sub_006044F0();
        return;
    }
    const uint32_t pitch = MEM32(self + 0xF0), yaw = MEM32(self + 0xF4), roll = MEM32(self + 0xF8);
    const float *sine = (const float *)GPTR(0x1DBDF78u);
    const uint32_t cp_index = ((pitch + 0x4000u) >> 2) & 0x3FFFu;
    const uint32_t cy_shift = (uint32_t)((int32_t)(yaw + 0x4000u) >> 2);
    const float sp = sine[(pitch >> 2) & 0x3FFFu], cp = sine[cp_index];
    const float sy = sine[(yaw >> 2) & 0x3FFFu], cy = sine[cy_shift & 0x3FFFu];
    const float sr = sine[(roll >> 2) & 0x3FFFu], cr = sine[((roll + 0x4000u) >> 2) & 0x3FFFu];
    const float scale = values.f[3];
    const float sx = values.f[4] * scale;
    const float sy_scale = values.f[5] * scale;
    const float sz = values.f[6] * scale;
    const float px = values.f[7], py = values.f[8], pz = values.f[9];
    const float lx = values.f[0], ly = values.f[1], lz = values.f[2];
    const float zero = MEMF(0x173D5BCu), one = MEMF(0x17343D8u);
    float matrix[16];
    matrix[0] = (sx * cy) * cp;
    matrix[1] = (sx * cp) * sy;
    matrix[2] = sx * sp;
    matrix[3] = 0.0f;
    const float cr_sy = cr * sy, cy_cr = cy * cr;
    matrix[4] = ((cy * sp) * sr - cr_sy) * sy_scale;
    matrix[5] = ((sy * sp) * sr + cy_cr) * sy_scale;
    matrix[6] = zero - (sy_scale * cp) * sr;
    matrix[7] = 0.0f;
    matrix[8] = zero - (cy_cr * sp + sy * sr) * sz;
    matrix[9] = (cy * sr - cr_sy * sp) * sz;
    matrix[10] = (sz * cp) * cr;
    matrix[11] = 0.0f;
    const float x = sx * px, y = sy_scale * py, z = sz * pz;
    const float yz = y * cr + z * sr;
    matrix[12] = ((lx - (x * cy) * cp) + ((z * cy) * cr) * sp) - ((y * cy) * sp) * sr + yz * sy;
    const float tail_y = ((y * sy) * sp) * sr;
    const float shift_y = (((x * cp) * sy + yz * cy) - ((z * cr) * sy) * sp) + tail_y;
    matrix[13] = ly - shift_y;
    const float tail_z = (y * cp) * sr;
    const float shift_z = ((z * cp) * cr + x * sp) - tail_z;
    matrix[14] = lz - shift_z;
    matrix[15] = one;
    memcpy(GPTR(out), matrix, sizeof matrix);

    /* These are caller-visible SSE values too, not only the returned matrix. */
    memset(g_xmm, 0, sizeof g_xmm);
    g_xmm[0].f32[0] = one;
    g_xmm[1].f32[0] = sr;
    g_xmm[2].f32[0] = shift_z;
    g_xmm[3].f32[0] = x * sp;
    g_xmm[4].f32[0] = cp;
    g_xmm[5].f32[0] = tail_z;
    g_xmm[6].f32[0] = tail_y;
    g_xmm[7].f32[0] = shift_y;
    g_eax = out;
    g_edx = cp_index;
    g_flag_k = FK_TEST; g_flag_a = cy_shift; g_flag_b = 0x3FFFu; g_flag_cf = 0;
    g_esp += 8;
}

/* 00B65BD0 (TMap<int, V>::Find) moved to core_native.c with the proven call contract. */

static void bit_test(void)
{
    g_edx = MEM32(g_ecx);
    g_eax = (MEM32(g_ecx + 4) & MEM32(g_edx)) != 0;
    g_flag_k = FK_CMP; g_flag_a = 0; g_flag_b = 0u - g_eax; g_flag_cf = g_eax;
    g_esp += 4;
}

/* Advance the world's actor iterator, skipping hidden levels, empty slots and deleted actors. */
static void actor_next(void)
{
    g_eax = MEM32(g_ecx + 4);
    PUSH32(g_esp, g_esi);
    g_esi = 0;
    uint32_t flag_a = MEM32(g_ecx + 8), flag_b = 0;
    MEM32(g_ecx + 16) = 0;
    g_edx = MEM32(MEM32(0x1DEE5ECu) + 0x44);
    g_eax = MEM32(g_edx + g_eax * 4u);
    if (!flag_a) {
        PUSH32(g_esp, g_ebx);
        g_ebx = 1;
        PUSH32(g_esp, g_edi);
        for (;;) {
            flag_a = MEM32(g_ecx + 16); flag_b = g_esi;
            if (flag_a != flag_b) break;
            int candidate = 0;
            if (MEM32(g_eax + 0x15C) == g_esi || MEM32(0x1DBDD2Cu) != g_esi) {
                MEM32(g_ecx) += g_ebx;
                g_edx = MEM32(g_ecx);
                candidate = (int32_t)g_edx < (int32_t)MEM32(g_eax + 0x40);
            }
            if (!candidate) {
                MEM32(g_ecx + 4) += g_ebx;
                g_eax = MEM32(g_ecx + 4);
                g_edx = MEM32(0x1DEE5ECu);
                flag_a = g_eax; flag_b = MEM32(g_edx + 0x48);
                if ((int32_t)flag_a >= (int32_t)flag_b) {
                    MEM32(g_ecx) = g_esi;
                    MEM32(g_ecx + 4) = g_esi;
                    MEM32(g_ecx + 8) = g_ebx;
                    break;
                }
                g_edx = MEM32(g_edx + 0x44);
                g_eax = MEM32(g_edx + g_eax * 4u);
                if (MEM32(g_eax + 0x15C) == g_esi || MEM32(0x1DBDD2Cu) != g_esi) {
                    g_edx = MEM32(g_eax + 0xD8);
                    MEM32(g_ecx) = g_edx;
                    candidate = (int32_t)g_edx < (int32_t)MEM32(g_eax + 0x40);
                }
            }
            if (candidate) {
                MEM32(g_ecx + 12) += g_ebx;
                g_edi = MEM32(g_eax + 0x3C);
                g_edx = MEM32(g_edi + MEM32(g_ecx) * 4u);
                MEM32(g_ecx + 16) = g_edx;
                if (g_edx == g_esi || (MEM8(g_edx + 0x74) & (uint8_t)g_ebx))
                    MEM32(g_ecx + 16) = g_esi;
            }
            flag_a = MEM32(g_ecx + 8); flag_b = g_esi;
            RECOMP_BACKEDGE();
            if (flag_a != flag_b) break;
        }
        g_edi = POP32_VAL(g_esp);
        g_ebx = POP32_VAL(g_esp);
    }
    g_esi = POP32_VAL(g_esp);
    g_flag_k = FK_CMP; g_flag_a = flag_a; g_flag_b = flag_b; g_flag_cf = flag_a < flag_b;
    g_esp += 4;
}

static unsigned long long checked[4], mismatches[4];
static int game_compare_yielded;
static void game_compare_yield(void) { game_compare_yielded = 1; }

static void check_native(unsigned kind, void (*native)(void), void (*original)(void), uint32_t out)
{
    /* A comparison must not release the VM twice while other threads can change the map. */
    if (kind == 1 && recomp_yield_hook && (int32_t)MEM32(g_ecx + 4) > 0 &&
        (uint64_t)g_backedges + MEM32(g_ecx + 4) >= RECOMP_YIELD_EVERY) {
        original();
        return;
    }
    const size_t output_size = kind == 3 ? 20 : 64;
    void_regs before = {0}, expected = {0}, actual = {0};
    uint8_t old_output[64], expected_output[64];
    void_regs_save(&before);
    if (out) memcpy(old_output, GPTR(out), output_size);
    void (*yield_hook)(void) = recomp_yield_hook;
    game_compare_yielded = 0;
    if (kind == 3 && yield_hook) recomp_yield_hook = game_compare_yield;
    native();
    recomp_yield_hook = yield_hook;
    const int yielded = game_compare_yielded;
    void_regs_save(&expected);
    if (out) {
        memcpy(expected_output, GPTR(out), output_size);
        memcpy(GPTR(out), old_output, output_size);
    }
    void_regs_load(&before);
    original();
    if (yielded) return;
    void_regs_save(&actual);
    /* The diagnostic caller label is outside the x86 function's observable state. */
    expected.cur = actual.cur;
    const int same = !memcmp(&expected, &actual, sizeof expected) &&
        (!out || !memcmp(expected_output, GPTR(out), output_size));
    ++checked[kind];
    if (!same && mismatches[kind]++ < 8)
        void_log("[game-native] kind %u mismatch, call %llu", kind, checked[kind]);
    if (checked[kind] == 1 || !(checked[kind] & 0xFFFFFu))
        void_log("[game-native] kind %u checked: %llu calls, %llu different", kind, checked[kind], mismatches[kind]);
}

void __wrap_sub_006044F0(void)
{
    const int mode = game_mode();
    if (mode == 0) __real_sub_006044F0();
    else if (mode == 2) check_native(0, actor_transform, __real_sub_006044F0, ARG(0));
    else actor_transform();
}

void __wrap_sub_00BF8120(void)
{
    const int mode = game_mode();
    if (mode == 0) __real_sub_00BF8120();
    else if (mode == 2) check_native(2, bit_test, __real_sub_00BF8120, 0);
    else bit_test();
}

void __wrap_sub_0088B010(void)
{
    const int mode = game_mode();
    if (mode == 0) __real_sub_0088B010();
    else if (mode == 2) check_native(3, actor_next, __real_sub_0088B010, g_ecx);
    else actor_next();
}

void void_game_native_init(void)
{
    void_dispatch_add(0x006044F0u, __wrap_sub_006044F0);
    void_dispatch_add(0x00BF8120u, __wrap_sub_00BF8120);
    void_dispatch_add(0x0088B010u, __wrap_sub_0088B010);
}
