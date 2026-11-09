/* UT3.exe 00BF8DA0: lookup of a baked distribution table (particle parameters), in native code.
 *
 * thiscall, `ret 0x10`, arguments (float Time, float **Entry1, float **Entry2, float *Alpha).
 * The table object holds the entry stride in floats at +3 (byte), the table {Data, Num} at +4/+8,
 * the time scale at +0x10 and the time bias at +0x14. The original:
 *   t = (Time - Bias) * Scale, kept if t >= 0 (COMISS + JAE), else 0 (NaN too);
 *   i = CVTTSS2SI(t) (0x80000000 when out of range);
 *   cur = Stride * i + 2, next = cur + Stride, last = Num - Stride, both clamped to last with
 *   the branch-free sign-mask minimum of the wrapped difference;
 *   *Entry1 = &Data[cur], *Entry2 = &Data[next], *Alpha = t - i through the x87 (FILD, a 2^32
 *   correction for negative i, FSUBR, FSTP), which the lifted code evaluates in double.
 * The argument slots it overwrites are dead after `ret 0x10`. EAX = i as in the original; ECX/EDX,
 * the flags and XMM0/XMM1 scratch are not reproduced (build/call-protocol/volatile.json), and the
 * x87 stack is balanced in both. Output pointers that overlap the inputs run the lifted original.
 * particle-native: absent/1 = native, 0 = lifted, 2 = compare EAX, ESP and the three outputs. */
#include "host.h"
#include "native_mode.inc"

extern void __real_sub_00BF8DA0(void);

static int particle_mode = -1;
static unsigned long long particle_checked, particle_mismatches, particle_fallbacks;

static __attribute__((cold, noinline)) int particle_mode_load(void)
{
    char path[1100];
    snprintf(path, sizeof path, "%s/particle-native", void_data_dir());
    FILE *file = fopen(path, "rb");
    int mode = file ? fgetc(file) : '1';
    if (file) fclose(file);
    particle_mode = void_native_mode(mode == '0' ? 0 : mode == '2' ? 2 : 1);
    void_log("[native-particle] distribution lookup 00BF8DA0: mode %d", particle_mode);
    return particle_mode;
}

static inline int particle_overlaps(uint32_t a, uint32_t a_size, uint32_t b, uint32_t b_size)
{
    return (uint64_t)a < (uint64_t)b + b_size && (uint64_t)b < (uint64_t)a + a_size;
}

struct lookup { uint32_t entry1, entry2, alpha_bits, index; };

static inline int distribution_lookup(uint32_t table, uint32_t stack, struct lookup *r)
{
    const uint32_t out1 = MEM32(stack + 8), out2 = MEM32(stack + 12), out3 = MEM32(stack + 16);
    /* The original reads Data twice and its argument slots after storing: keep it for aliases. */
    if (particle_overlaps(out1, 4, table, 0x18) || particle_overlaps(out1, 4, stack, 0x14) ||
        particle_overlaps(out2, 4, table, 0x18) || particle_overlaps(out2, 4, stack, 0x14) || particle_overlaps(out3, 4, stack, 0x14))
        return 0;
    const float scaled = (MEMF(stack + 4) - MEMF(table + 0x14)) * MEMF(table + 0x10);
    const float t = scaled >= 0.0f ? scaled : 0.0f;
    const int32_t index = t < 2147483648.0f ? (int32_t)t : INT32_MIN;   /* t >= 0 here; CVTTSS2SI */
    const uint32_t stride = MEM8(table + 3), data = MEM32(table + 4), num = MEM32(table + 8);
    const uint32_t cur = stride * (uint32_t)index + 2u, next = stride + cur, last = num - stride;
    const int32_t dc = (int32_t)(last - cur), dn = (int32_t)(last - next);
    const uint32_t cur_clamped = (uint32_t)((dc >> 31) & dc) + cur;
    const uint32_t next_clamped = (uint32_t)((dn >> 31) & dn) + next;
    double whole = (double)index;
    if (index < 0) whole += (double)MEMF(0x173D9B8u);   /* FADD dword [0x173D9B8] = 2^32 in UT3.exe */
    const float alpha = (float)((double)t - whole);
    r->entry1 = data + cur_clamped * 4u;
    r->entry2 = data + next_clamped * 4u;
    memcpy(&r->alpha_bits, &alpha, 4);
    r->index = (uint32_t)index;
    return 1;
}

static __attribute__((cold, noinline)) void particle_compare(const struct lookup *r)
{
    const uint32_t saved_esp = g_esp, out1 = ARG(1), out2 = ARG(2), out3 = ARG(3);
    __real_sub_00BF8DA0();
    const int same = g_eax == r->index && g_esp == saved_esp + 0x14u && MEM32(out1) == r->entry1 &&
        MEM32(out2) == r->entry2 && MEM32(out3) == r->alpha_bits;
    ++particle_checked;
    if (!same && particle_mismatches++ < 16)
        void_log("[native-particle] mismatch: native %08X %08X %08X lifted %08X %08X %08X", r->entry1, r->entry2,
                 r->alpha_bits, MEM32(out1), MEM32(out2), MEM32(out3));
    if ((particle_checked & 0xFFFFFu) == 0)
        void_log("[native-particle] checked %llu calls, %llu different, %llu fallbacks", particle_checked,
                 particle_mismatches, particle_fallbacks);
}

void __wrap_sub_00BF8DA0(void)
{
    const int mode = __builtin_expect(particle_mode >= 0, 1) ? particle_mode : particle_mode_load();
    struct lookup r;
    if (mode == 0) { __real_sub_00BF8DA0(); return; }
    if (!distribution_lookup(g_ecx, g_esp, &r)) { ++particle_fallbacks; __real_sub_00BF8DA0(); return; }
    if (mode == 2) { particle_compare(&r); return; }
    const uint32_t out1 = ARG(1), out2 = ARG(2), out3 = ARG(3);
    MEM32(out1) = r.entry1;
    MEM32(out2) = r.entry2;
    MEM32(out3) = r.alpha_bits;
    g_eax = r.index;
    g_esp += 0x14u;
}

void void_particle_native_init(void)
{
    void_dispatch_add(0x00BF8DA0u, __wrap_sub_00BF8DA0);
}
