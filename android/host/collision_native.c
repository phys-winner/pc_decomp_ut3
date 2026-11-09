/* UT3.exe 0116C250: segment against axis-aligned box for collision checks, in native code.
 *
 * thiscall: ECX = the box {Min.xyz, Max.xyz}; arguments: the check record (start at +0xA8,
 * direction at +0xC0, reciprocal direction at +0xCC) and float *Time; `ret 8`; EAX = hit.
 * The original stores 0 to *Time, then picks, per axis, the slab plane the start lies outside
 * of (rejecting a direction that points away), takes the latest entry time, stores it, and accepts
 * it if 0 <= t <= 1 and the hit point lies inside the box grown by the constant at 0x01750298.
 * A start inside all three slabs is a hit at time 0.
 *
 * Every SSE comparison keeps its COMISS + branch meaning ("jbe" = not greater, "jae" = greater or
 * equal, "jb" = not greater or equal, "ja" = greater; unordered operands take the jbe/jb branches) and
 * every arithmetic step keeps the original single-precision operations and operand order. A computed
 * value that is not finite, and a Time pointer overlapping the inputs, run the lifted original, so
 * NaN payloads never differ.
 *
 * Outputs: EAX, ESP (+12), *Time; ECX/ESI/EBX/EDI/EBP keep their values as in the original. The
 * original's EDX (inside flag), flags and XMM0-5 scratch are not reproduced: no caller reads them
 * (build/call-protocol/volatile.json). collision-native: absent/1 = native, 0 = lifted,
 * 2 = compare EAX, ESP and *Time with the original on every call (no other side effects). */
#include "host.h"
#include "native_mode.inc"

extern void __real_sub_0116C250(void);

static int collision_mode = -1;
static unsigned long long collision_checked, collision_mismatches, collision_fallbacks;

static __attribute__((cold, noinline)) int collision_mode_load(void)
{
    char path[1100];
    snprintf(path, sizeof path, "%s/collision-native", void_data_dir());
    FILE *file = fopen(path, "rb");
    int mode = file ? fgetc(file) : '1';
    if (file) fclose(file);
    collision_mode = void_native_mode(mode == '0' ? 0 : mode == '2' ? 2 : 1);
    void_log("[native-collision] segment/box slab test 0116C250: mode %d", collision_mode);
    return collision_mode;
}

static inline int finite_bits(uint32_t bits) { return (bits & 0x7F800000u) != 0x7F800000u; }
static inline int finite_f(float value) { uint32_t bits; memcpy(&bits, &value, 4); return finite_bits(bits); }

static inline void load_floats(uint32_t address, float *out, unsigned count)
{
    for (unsigned i = 0; i < count; ++i) out[i] = MEMF(address + 4u * i);
}

static inline int overlaps(uint32_t a, uint32_t a_size, uint32_t b, uint32_t b_size)
{
    return (uint64_t)a < (uint64_t)b + b_size && (uint64_t)b < (uint64_t)a + a_size;
}

/* One slab. Returns 0 to reject, else 1 with *time / *inside updated. */
static inline int slab(float mn, float mx, float s, float d, float inv, float *time, int *inside, int *finite)
{
    if (mn > s) {                       /* comiss Min, Start; jbe skip */
        if (0.0f >= d) return 0;        /* comiss 0, Dir; jae reject */
        const float delta = mn - s;
        *time = delta * inv;
        *finite &= finite_f(delta) & finite_f(*time);
        *inside = 0;
    } else if (s > mx) {                /* comiss Start, Max; jbe skip */
        if (d >= 0.0f) return 0;        /* comiss Dir, 0; jae reject */
        const float delta = mx - s;
        *time = delta * inv;
        *finite &= finite_f(delta) & finite_f(*time);
        *inside = 0;
    }
    return 1;
}

/* Returns -1 to run the original, else the hit flag; *result_time is the final *Time. */
static int line_box(uint32_t box, uint32_t check, uint32_t time_out, float *result_time)
{
    float mn[3], mx[3], start[3], dir[3], inv[3], eps, one;
    if (overlaps(time_out, 4, box, 0x18) || overlaps(time_out, 4, check + 0xA8u, 0x30) ||
        overlaps(time_out, 4, 0x1750298u, 4) || overlaps(time_out, 4, 0x17343D8u, 4))
        return -1;
    /* Inputs are taken as they are: comparisons keep the COMISS meaning for NaN too, and only
     * computed values can carry a NaN payload or an infinity into *Time, so those are checked.
     * The reciprocal of a zero direction component (+-Inf) is never used: that axis rejects first. */
    load_floats(box, mn, 3); load_floats(box + 0xC, mx, 3); load_floats(check + 0xA8, start, 3);
    load_floats(check + 0xC0, dir, 3); load_floats(check + 0xCC, inv, 3);
    eps = MEMF(0x1750298u); one = MEMF(0x17343D8u);
    float t[3] = {0.0f, 0.0f, 0.0f};
    int inside = 1, finite = 1;
    *result_time = 0.0f;
    for (unsigned axis = 0; axis < 3; ++axis)
        if (!slab(mn[axis], mx[axis], start[axis], dir[axis], inv[axis], &t[axis], &inside, &finite))
            return finite ? 0 : -1;
    if (!finite) return -1;
    if (inside) return 1;               /* *Time stays 0 */
    float time = t[0] >= t[1] ? t[0] : t[1];   /* comiss tx, ty; jb -> ty */
    if (!(time >= t[2])) time = t[2];          /* comiss t, tz; jae keep */
    *result_time = time;
    if (!(time >= 0.0f)) return 0;             /* comiss t, 0; jb reject */
    if (!(one >= time)) return 0;              /* comiss 1, t; jb reject */
    const float px = dir[0] * time, py = dir[1] * time, pz = dir[2] * time;
    const float hx = start[0] + px, hy = start[1] + py, hz = start[2] + pz;
    const float lx = mn[0] - eps, ux = mx[0] + eps, ly = mn[1] - eps, uy = mx[1] + eps;
    const float lz = mn[2] - eps, uz = mx[2] + eps;
    if (!(finite_f(px) & finite_f(py) & finite_f(pz) & finite_f(hx) & finite_f(hy) & finite_f(hz) &
          finite_f(lx) & finite_f(ux) & finite_f(ly) & finite_f(uy) & finite_f(lz) & finite_f(uz)))
        return -1;
    if (!(hx > lx) || !(ux > hx)) return 0;    /* comiss h, Min-eps; jbe / comiss Max+eps, h; jbe */
    if (!(hy > ly) || !(uy > hy)) return 0;
    if (!(hz > lz)) return 0;
    return uz > hz;                            /* comiss Max+eps, h; ja hit */
}

static __attribute__((cold, noinline)) void collision_compare(int hit, float time)
{
    const uint32_t saved_esp = g_esp, out = ARG(1);
    uint32_t time_bits;
    memcpy(&time_bits, &time, 4);
    __real_sub_0116C250();
    const int same = (int)g_eax == hit && g_esp == saved_esp + 12u && MEM32(out) == time_bits;
    ++collision_checked;
    if (!same && collision_mismatches++ < 16)
        void_log("[native-collision] mismatch: native %d %08X lifted %u %08X", hit, time_bits, g_eax, MEM32(out));
    if ((collision_checked & 0xFFFFFu) == 0)
        void_log("[native-collision] checked %llu calls, %llu different, %llu fallbacks", collision_checked,
                 collision_mismatches, collision_fallbacks);
}

void __wrap_sub_0116C250(void)
{
    const int mode = __builtin_expect(collision_mode >= 0, 1) ? collision_mode : collision_mode_load();
    if (mode == 0) { __real_sub_0116C250(); return; }
    const uint32_t out = ARG(1);
    float time;
    const int hit = line_box(g_ecx, ARG(0), out, &time);
    if (hit < 0) { ++collision_fallbacks; __real_sub_0116C250(); return; }
    if (mode == 2) { collision_compare(hit, time); return; }
    MEMF(out) = time;
    g_eax = (uint32_t)hit;
    g_esp += 12;
}

void void_collision_native_init(void)
{
    void_dispatch_add(0x0116C250u, __wrap_sub_0116C250);
}
