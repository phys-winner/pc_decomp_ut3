/* Native versions of the PhysX 2.7.3 fluid collision kernels that dominate the PhysX Mod Pack maps
 * (PhysXCore.dll, CPU solver). On DM-HeatRay_PhysX the PhysX thread took 63% of all cycles of the
 * process, 72% of it in two lifted x87 functions: the particle test against a convex plane set (also
 * every box, sub_1E23D300 builds its six planes) and the move of a particle into a shape's frame.
 *
 * The arithmetic follows the lifted code exactly: x87 values are doubles there, stores round to
 * float, and every sum keeps the association of the original instruction sequence (taken from the
 * lifted C with a symbolic interpreter of its x87 stack, not from a decompiler's expression). The
 * comparisons keep the original's treatment of NaN (the fnstsw/test ah/jp sequences).
 *
 * <data>/physx-native: "0" the lifted code only, "2" both and every result compared byte for byte
 * (mismatches in ut3.log), anything else or no file: the native kernels. The rest of PhysXCore keeps
 * its x87 stack in C locals (tools/x87_slots.py, applied by tools/build_android.py).
 *
 * The second PhysX thread spends most of its time in the pair manager's removal of a pair from the
 * hash chains (sub_1E213B70 and its helper sub_1E213AC0, integer code): native too, under the same
 * switch, its register results and memory writes checked in mode 2.
 *
 * PHYSX_NATIVE_KERNELS_ONLY leaves only the kernels, for tests/test_physx_box.c. */
#ifndef PHYSX_NATIVE_KERNELS_ONLY
#include "host.h"
#include "native_mode.inc"
#endif
#include <float.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern void __real_sub_1E23D3A0(void);
extern void __real_sub_1E23A700(void);

static inline float ld(const uint8_t *p, uint32_t off) { float v; memcpy(&v, p + off, 4); return v; }
static inline void stf(uint8_t *p, uint32_t off, double v) { float f = (float)v; memcpy(p + off, &f, 4); }
static inline void or16(uint8_t *p, uint32_t off, uint16_t bits)
{
    uint16_t v;
    memcpy(&v, p + off, 2);
    v |= bits;
    memcpy(p + off, &v, 2);
}

#ifndef PHYSX_NATIVE_KERNELS_ONLY
static int native_mode = -1;

static __attribute__((noinline, cold)) int physx_native_mode_load(void)
{
    char path[1100];
    snprintf(path, sizeof path, "%s/physx-native", void_data_dir());
    FILE *f = fopen(path, "rb");
    int mode = 1;
    if (f) {
        int c = fgetc(f);
        mode = c == '0' ? 0 : c == '2' ? 2 : 1;
        fclose(f);
    }
    mode = void_native_mode(mode);
    native_mode = mode;
    void_log("[physx] native fluid collision kernels: %s", mode == 0 ? "off (lifted x87 code)" :
        mode == 2 ? "on, every result checked against the lifted code" : "on");
    return mode;
}

/* Called for every particle and shape: one load once the file has been read. */
static inline int physx_native_mode(void)
{
    const int mode = native_mode;
    return __builtin_expect(mode >= 0, 1) ? mode : physx_native_mode_load();
}
#endif

/* sub_1E23D3A0, __thiscall (collision distance at this+0x20), particle in shape space, plane count,
 * planes {nx, ny, nz, d}: the particle's move from its old (+8) to its new position (+0x14) against
 * a convex shape. Writes the contact time (+0), flags (+4), normal (+0x20) and point (+0x2C). */
struct convex {
    float cd, cd2, ox, oy, oz, px, py, pz, dx, dy, dz;
    float old_max, exit_min, face_dist, new_max;
    float old_n[3], face_n[3], new_n[3];
    double entry, hit_n[3], zero;
    unsigned flags;
};

static inline __attribute__((always_inline)) void convex_begin(struct convex *s, float cd, const uint8_t *p)
{
    s->cd = cd;
    s->ox = ld(p, 0x08); s->oy = ld(p, 0x0C); s->oz = ld(p, 0x10);
    s->px = ld(p, 0x14); s->py = ld(p, 0x18); s->pz = ld(p, 0x1C);
    s->dx = (float)((double)s->px - s->ox);
    s->dy = (float)((double)s->py - s->oy);
    s->dz = (float)((double)s->pz - s->oz);
    s->cd2 = (float)((double)cd + cd);
    s->old_max = -FLT_MAX; s->exit_min = FLT_MAX; s->face_dist = -FLT_MAX; s->new_max = -FLT_MAX;
    for (int i = 0; i < 3; ++i)
        s->old_n[i] = s->face_n[i] = s->new_n[i] = 0;
    s->entry = -FLT_MAX;
    s->hit_n[0] = s->dx; s->hit_n[1] = s->dy; s->hit_n[2] = s->dz;
    s->zero = 0.0;
    s->flags = 0;
}

/* One plane: new_dist = n.p - d, old_dot = n.o and along = n.(p - o), each summed in the original's
 * order by the caller. */
static inline __attribute__((always_inline)) void convex_plane(struct convex *s, float nx, float ny, float nz,
    float d, double new_dist, double old_dot, double along)
{
    const double old_dist = old_dot - d;
    if (!(old_dist <= 0.0))
        s->flags |= 8;
    else if ((double)s->old_max < old_dist) {
        s->old_max = (float)old_dist;
        s->old_n[0] = nx; s->old_n[1] = ny; s->old_n[2] = nz;
    }
    if (!(new_dist <= (double)s->cd))
        s->flags |= 1;
    else if ((double)s->new_max < new_dist) {
        s->new_max = (float)new_dist;
        s->new_n[0] = nx; s->new_n[1] = ny; s->new_n[2] = nz;
    }
    if (!((double)s->cd2 >= new_dist))
        s->flags |= 4;
    else if (new_dist > 0.0) {
        if ((double)s->face_dist == -(double)FLT_MAX) {
            s->face_dist = (float)new_dist;
            s->face_n[0] = nx; s->face_n[1] = ny; s->face_n[2] = nz;
        } else
            s->flags |= 4;
    }
    s->zero = 0.0;
    if (along == 0.0) {
        if (new_dist > 0.0) s->flags |= 2;
    } else {
        const double t = ((double)d - old_dot) / along;
        if (!(along < 0.0)) {
            if ((double)s->exit_min > t) s->exit_min = (float)t;
        } else if (t > s->entry) {
            s->entry = t;
            s->hit_n[0] = nx; s->hit_n[1] = ny; s->hit_n[2] = nz;
        }
    }
}

static inline __attribute__((always_inline)) void convex_end(const struct convex *s, uint8_t *p)
{
    const float ox = s->ox, oy = s->oy, oz = s->oz, px = s->px, py = s->py, pz = s->pz;
    const double cd = s->cd, entry = s->entry;
    if (!(s->flags & 8)) {
        /* The old position is inside every plane: pushed out along the nearest one. */
        const double k = cd - s->old_max;
        or16(p, 4, 4);
        stf(p, 0x00, s->zero);
        stf(p, 0x20, s->old_n[0]); stf(p, 0x24, s->old_n[1]); stf(p, 0x28, s->old_n[2]);
        stf(p, 0x2C, ((double)s->old_n[0] * k) + ox);
        stf(p, 0x30, ((double)s->old_n[1] * k) + oy);
        stf(p, 0x34, ((double)s->old_n[2] * k) + oz);
        return;
    }
    if (s->zero > entry || entry >= 1.0 || (double)s->exit_min < entry || (s->flags & 2)) {
        if (!(p[4] & 2)) {
            if (!(s->flags & 4)) {
                const double k = cd - s->face_dist;
                stf(p, 0x20, s->face_n[0]); stf(p, 0x24, s->face_n[1]); stf(p, 0x28, s->face_n[2]);
                stf(p, 0x2C, ((double)s->face_n[0] * k) + px);
                stf(p, 0x30, ((double)s->face_n[1] * k) + py);
                stf(p, 0x34, ((double)s->face_n[2] * k) + pz);
                or16(p, 4, 0x10);
            }
            if (!(s->flags & 1)) {
                const double k = cd - s->new_max;
                stf(p, 0x20, s->new_n[0]); stf(p, 0x24, s->new_n[1]); stf(p, 0x28, s->new_n[2]);
                stf(p, 0x2C, ((double)s->new_n[0] * k) + px);
                stf(p, 0x30, ((double)s->new_n[1] * k) + py);
                stf(p, 0x34, ((double)s->new_n[2] * k) + pz);
                or16(p, 4, 8);
            }
        }
        return;
    }
    /* The move enters the shape at `entry` through the plane with the latest entry. */
    stf(p, 0x20, s->hit_n[0]); stf(p, 0x24, s->hit_n[1]); stf(p, 0x28, s->hit_n[2]);
    stf(p, 0x2C, (s->hit_n[0] * cd) + (((double)s->dx * entry) + ox));
    stf(p, 0x30, (s->hit_n[1] * cd) + (((double)s->dy * entry) + oy));
    stf(p, 0x34, (s->hit_n[2] * cd) + (((double)s->dz * entry) + oz));
    or16(p, 4, 4);
    stf(p, 0x00, entry);
}

static void fluid_convex(float cd, uint8_t *p, uint32_t count, const uint8_t *plane)
{
    struct convex s;
    convex_begin(&s, cd, p);
    const double ox = s.ox, oy = s.oy, oz = s.oz, px = s.px, py = s.py, pz = s.pz;
    const double dx = s.dx, dy = s.dy, dz = s.dz;
    for (uint32_t i = 0; i < count; ++i, plane += 16) {
        const float nx = ld(plane, 0), ny = ld(plane, 4), nz = ld(plane, 8), d = ld(plane, 12);
        convex_plane(&s, nx, ny, nz, d,
            ((((double)nz * pz) + (py * ny)) + (px * nx)) - d,
            ((oy * ny) + (oz * nz)) + (ox * nx),
            (((double)nz * dz) + ((double)nx * dx)) + ((double)ny * dy));
    }
    convex_end(&s, p);
}

/* A box (sub_1E23D300's six planes +x, -x, +y, -y, +z, -z with d = the half extent) without the
 * products by 0 and 1. With finite coordinates and a nonzero finite extent every product by 0 is a
 * zero that the sum then drops (x + 0 = x for x != 0; a zero sum minus d != 0 is -d), so new_dist
 * and old_dist are the same doubles as the general sums, old_dot feeds only d - old_dot (= d when it
 * is a zero) and a zero `along` only its == 0 test. Anything else takes the general path. */
static void fluid_box(float cd, uint8_t *p, const float e[3])
{
    struct convex s;
    convex_begin(&s, cd, p);
    const float values[12] = { s.ox, s.oy, s.oz, s.px, s.py, s.pz, s.dx, s.dy, s.dz, e[0], e[1], e[2] };
    int general = e[0] == 0.0f || e[1] == 0.0f || e[2] == 0.0f;
    for (int i = 0; i < 12; ++i)
        general |= !__builtin_isfinite(values[i]);
    if (__builtin_expect(general, 0)) {
        const float planes[24] = { 1, 0, 0, e[0],  -1, 0, 0, e[0],  0, 1, 0, e[1],  0, -1, 0, e[1],
                                   0, 0, 1, e[2],  0, 0, -1, e[2] };
        fluid_convex(cd, p, 6, (const uint8_t *)planes);
        return;
    }
    convex_plane(&s, 1, 0, 0, e[0], (double)s.px - e[0], (double)s.ox, (double)s.dx);
    convex_plane(&s, -1, 0, 0, e[0], -(double)s.px - e[0], -(double)s.ox, -(double)s.dx);
    convex_plane(&s, 0, 1, 0, e[1], (double)s.py - e[1], (double)s.oy, (double)s.dy);
    convex_plane(&s, 0, -1, 0, e[1], -(double)s.py - e[1], -(double)s.oy, -(double)s.dy);
    convex_plane(&s, 0, 0, 1, e[2], (double)s.pz - e[2], (double)s.oz, (double)s.dz);
    convex_plane(&s, 0, 0, -1, e[2], -(double)s.pz - e[2], -(double)s.oz, -(double)s.dz);
    convex_end(&s, p);
}

/* sub_1E23A700, __stdcall (out, particle, shape), returns the particle: the particle's time, its
 * flag 2 and its old (+0x70) and new (+0x7C) positions in the shape's frame (two poses: rotation at
 * +0x08/+0x38, translation at +0x2C/+0x5C). */
static void fluid_to_shape(uint8_t *out, const uint8_t *p, const uint8_t *q)
{
    const double nx = (double)ld(p, 0x7C) - ld(q, 0x5C), ny = (double)ld(p, 0x80) - ld(q, 0x60);
    const double nz = (double)ld(p, 0x84) - ld(q, 0x64);
    const double ox = (double)ld(p, 0x70) - ld(q, 0x2C), oy = (double)ld(p, 0x74) - ld(q, 0x30);
    const double oz = (double)ld(p, 0x78) - ld(q, 0x34);
    const double n0 = (((double)ld(q, 0x3C) * ny) + ((double)ld(q, 0x40) * nz)) + (nx * ld(q, 0x38));
    const double n1 = ((nz * ld(q, 0x4C)) + (ny * ld(q, 0x48))) + ((double)ld(q, 0x44) * nx);
    const double n2 = ((nz * ld(q, 0x58)) + (ny * ld(q, 0x54))) + (nx * ld(q, 0x50));
    const double o0 = ((double)ld(q, 0x08) * ox) + (((double)ld(q, 0x0C) * oy) + ((double)ld(q, 0x10) * oz));
    const double o1 = ((oz * ld(q, 0x1C)) + (oy * ld(q, 0x18))) + (ox * ld(q, 0x14));
    const double o2 = ((oz * ld(q, 0x28)) + (oy * ld(q, 0x24))) + (ox * ld(q, 0x20));
    const uint16_t flags = (uint16_t)(p[0] & 2);
    memcpy(out, p + 0x2C, 4);
    memcpy(out + 4, &flags, 2);
    stf(out, 0x14, n0); stf(out, 0x18, n1); stf(out, 0x1C, n2);
    stf(out, 0x08, o0); stf(out, 0x0C, o1); stf(out, 0x10, o2);
}

#ifndef PHYSX_NATIVE_KERNELS_ONLY
static unsigned long long checked_calls, checked_mismatches;

static void check_report(const char *kernel, const uint8_t *native, const uint8_t *lifted, uint32_t size,
    uint32_t skip_from, uint32_t skip_to)
{
    ++checked_calls;
    for (uint32_t i = 0; i < size; i += 2) {
        if (i >= skip_from && i < skip_to) continue;
        if (memcmp(native + i, lifted + i, 2)) {
            if (checked_mismatches++ < 24) {
                uint32_t field = i & ~3u;
                float a, b;
                memcpy(&a, native + field, 4);
                memcpy(&b, lifted + field, 4);
                void_log("[physx] %s mismatch at +0x%02X: native %08X (%g) lifted %08X (%g)", kernel, field,
                    *(const uint32_t *)(native + field), (double)a, *(const uint32_t *)(lifted + field), (double)b);
            }
            break;
        }
    }
    if ((checked_calls & 0xFFFFF) == 0)
        void_log("[physx] native kernels checked: %llu calls, %llu with a different result",
            checked_calls, checked_mismatches);
}

void __wrap_sub_1E23D3A0(void)
{
    const uint32_t self = g_ecx, particle = ARG(0), count = ARG(1), planes = ARG(2);
    const int mode = physx_native_mode();
    /* The lifted original reads and writes these without checks too; a plane count beyond any shape
     * PhysX builds keeps the original path. */
    if (mode == 0 || count > 4096) {
        __real_sub_1E23D3A0();
        return;
    }
    const float cd = MEMF(self + 0x20);
    if (mode == 2) {
        uint8_t copy[0x38];
        memcpy(copy, GPTR(particle), sizeof copy);
        fluid_convex(cd, copy, count, (const uint8_t *)GPTR(planes));
        __real_sub_1E23D3A0();                      /* pops its own arguments */
        check_report("convex", copy, (const uint8_t *)GPTR(particle), sizeof copy, 6, 8);
        return;
    }
    fluid_convex(cd, (uint8_t *)GPTR(particle), count, (const uint8_t *)GPTR(planes));
    g_ecx = 4;                                       /* as the original leaves them */
    g_edx = particle;
    g_esp += 16;                                     /* ret 0Ch */
}

void __wrap_sub_1E23A700(void)
{
    const uint32_t out = ARG(0), particle = ARG(1), shape = ARG(2);
    const int mode = physx_native_mode();
    if (mode == 0) {
        __real_sub_1E23A700();
        return;
    }
    if (mode == 2) {
        uint8_t copy[0x20];
        memcpy(copy, GPTR(out), sizeof copy);
        fluid_to_shape(copy, (const uint8_t *)GPTR(particle), (const uint8_t *)GPTR(shape));
        __real_sub_1E23A700();
        check_report("to-shape", copy, (const uint8_t *)GPTR(out), sizeof copy, 6, 8);
        return;
    }
    fluid_to_shape((uint8_t *)GPTR(out), (const uint8_t *)GPTR(particle), (const uint8_t *)GPTR(shape));
    g_eax = particle;
    g_esp += 16;                                     /* ret 0Ch */
}

extern void __real_sub_1E23D300(void);

/* sub_1E23D300, __thiscall (particle, half extents; ret 8): a box as six planes for sub_1E23D3A0, in
 * the original's order +x, -x, +y, -y, +z, -z (the plane order decides ties): fluid_box. */
void __wrap_sub_1E23D300(void)
{
    const uint32_t self = g_ecx, particle = ARG(0), extents = ARG(1);
    const int mode = physx_native_mode();
    if (mode == 0) {
        __real_sub_1E23D300();
        return;
    }
    const float e[3] = { MEMF(extents), MEMF(extents + 4), MEMF(extents + 8) };
    if (mode == 2) {
        uint8_t copy[0x38];
        memcpy(copy, GPTR(particle), sizeof copy);
        fluid_box(MEMF(self + 0x20), copy, e);
        __real_sub_1E23D300();
        check_report("box", copy, (const uint8_t *)GPTR(particle), sizeof copy, 6, 8);
        return;
    }
    fluid_box(MEMF(self + 0x20), (uint8_t *)GPTR(particle), e);
    g_ecx = 4;
    g_edx = particle;
    g_esp += 12;                                     /* ret 8 */
}

/* sub_1E213AC0 (__thiscall, ret 8; only sub_1E213B70 calls it): find `key` in the hash chain from
 * `node`. Nodes are 12 bytes at [this+0x30]: four u16 keys, the u16 next node at +8. A slot whose bit
 * (node * 4 + slot) is set in the bitmap [this+0x10] (of [this+0x14] dwords) is skipped; a found
 * slot's bit is cleared in the bitmaps [this] and [this+8] (no size check there, as in the
 * original). al = found; eax, ecx and edx come back as the original leaves them (it saves and
 * restores the other registers). The writes are returned instead of made, for the check mode. */
struct pair_regs { uint32_t eax, ecx, edx; };
struct pair_write { uint32_t first, second, mask; };

static inline __attribute__((always_inline)) int pair_find(uint32_t self, uint32_t node, uint32_t key,
    struct pair_regs *r, struct pair_write *w)
{
    uint32_t a = r->eax, c = self, d = r->edx;
    if (node) {
        /* Read once: nothing in the loop writes memory. */
        const uint32_t words = MEM32(self + 0x14), skip = MEM32(self + 0x10), pool = MEM32(self + 0x30);
        do {
            a = node * 4u;
            for (uint32_t slot = 0; slot < 4; ++slot, ++a) {
                d = a >> 5;
                if (d < words) {
                    const uint32_t set = (MEM32(skip + d * 4u) & (1u << (a & 31))) != 0;
                    c = (skip & ~0xFFu) | set;
                    if (set) continue;
                }
                d = pool;
                c = MEM16(pool + (slot + node * 6u) * 2u);
                if (c == key) {
                    const uint32_t offset = (a >> 5) * 4u, second = MEM32(self + 8);
                    w->first = MEM32(self) + offset;
                    w->second = offset + second;
                    w->mask = ~(1u << (a & 31));
                    r->eax = ((offset + second) & ~0xFFu) | 1u;
                    r->ecx = second;
                    r->edx = w->mask;
                    return 1;
                }
            }
            a = pool;
            d = node * 3u;
            node = MEM16(pool + d * 4u + 8u);
        } while (node);
    }
    r->eax = a & ~0xFFu;
    r->ecx = c;
    r->edx = d;
    return 0;
}

/* sub_1E213B70 (__thiscall (a, b), ret 8): remove the pair from a's chain (key b), else from b's
 * chain (key a); a chain starts at the u16 +0x1C of the 32-byte entry [[this+0x40]] + index * 32.
 * Its two callers in sub_1E2129C0 use none of its results; the registers are kept exact anyway. */
static int pair_remove(uint32_t self, uint32_t a, uint32_t b, struct pair_regs *r, struct pair_write *w)
{
    r->eax = (a << 5) + MEM32(MEM32(self + 0x40));
    r->edx = MEM16(r->eax + 0x1Cu);
    if (pair_find(self, r->edx, b, r, w))
        return 1;
    const uint32_t holder = MEM32(self + 0x40);
    r->eax = holder;
    r->edx = MEM16((b << 5) + MEM32(holder) + 0x1Cu);
    return pair_find(self, r->edx, a, r, w);
}

extern void __real_sub_1E213B70(void);

static unsigned long long pair_checked, pair_mismatches;

void __wrap_sub_1E213B70(void)
{
    const uint32_t self = g_ecx, a = ARG(0), b = ARG(1), esp = g_esp;
    const int mode = physx_native_mode();
    if (mode == 0) {
        __real_sub_1E213B70();
        return;
    }
    struct pair_regs r;
    struct pair_write w;
    const int found = pair_remove(self, a, b, &r, &w);
    if (mode == 2) {
        const uint32_t first = found ? MEM32(w.first) & w.mask : 0;
        const uint32_t second = found ? (w.first == w.second ? first : MEM32(w.second) & w.mask) : 0;
        __real_sub_1E213B70();
        const int same = g_eax == r.eax && g_ecx == r.ecx && g_edx == r.edx && g_esp == esp + 12u &&
            (!found || (MEM32(w.first) == first && MEM32(w.second) == second));
        ++pair_checked;
        if (!same && pair_mismatches++ < 24)
            void_log("[physx] pair removal mismatch (%08X, %u, %u): native eax %08X ecx %08X edx %08X, "
                "lifted %08X %08X %08X, found %d", self, a, b, r.eax, r.ecx, r.edx, g_eax, g_ecx, g_edx, found);
        if ((pair_checked & 0xFFFFF) == 0)
            void_log("[physx] native pair removal checked: %llu calls, %llu with a different result",
                pair_checked, pair_mismatches);
        return;
    }
    if (found) {
        MEM32(w.first) &= w.mask;
        MEM32(w.second) &= w.mask;
    }
    g_eax = r.eax;
    g_ecx = r.ecx;
    g_edx = r.edx;
    g_esp = esp + 12u;                               /* ret 8 */
}

void void_physx_native_init(void)
{
    void_dispatch_add(0x1E23D3A0u, __wrap_sub_1E23D3A0);
    void_dispatch_add(0x1E23A700u, __wrap_sub_1E23A700);
    void_dispatch_add(0x1E23D300u, __wrap_sub_1E23D300);
    void_dispatch_add(0x1E213B70u, __wrap_sub_1E213B70);
}
#endif
