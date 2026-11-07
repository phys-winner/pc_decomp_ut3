/* fluid_box (android/host/physx_native.c) against the general convex test with sub_1E23D300's six
 * planes (+x, -x, +y, -y, +z, -z, d = the half extent): the whole 0x38-byte particle record must come
 * out identical. Inputs: ordinary coordinates at several scales, particles on and next to the faces,
 * no move, zeros of both signs, subnormals, FLT_MAX, infinities and NaNs in every field, odd extents
 * and collision distances, random flag bits. The general path itself is compared with the version
 * that was checked against the lifted code in the game, on the box planes and on random plane sets.
 * Built and run on the ARM64 device by test_physx_box.py with the production floating-point flags. */
#define PHYSX_NATIVE_KERNELS_ONLY 1
#include "../android/host/physx_native.c"
#include <math.h>
#include <stdlib.h>

/* fluid_convex as it was checked against the lifted sub_1E23D3A0 in the game (16.7 million calls,
 * none different), before the plane step moved into convex_plane for fluid_box. */
static void fluid_convex_checked(float cd, uint8_t *p, uint32_t count, const uint8_t *plane)
{
    const float ox = ld(p, 0x08), oy = ld(p, 0x0C), oz = ld(p, 0x10);
    const float px = ld(p, 0x14), py = ld(p, 0x18), pz = ld(p, 0x1C);
    const float dx = (float)((double)px - ox), dy = (float)((double)py - oy), dz = (float)((double)pz - oz);
    const float cd2 = (float)((double)cd + cd);
    float old_max = -FLT_MAX, exit_min = FLT_MAX, face_dist = -FLT_MAX, new_max = -FLT_MAX;
    float old_n[3] = { 0, 0, 0 }, face_n[3] = { 0, 0, 0 }, new_n[3] = { 0, 0, 0 };
    double entry = -FLT_MAX, hit_n[3] = { dx, dy, dz }, zero = 0.0;
    unsigned flags = 0;
    for (uint32_t i = 0; i < count; ++i, plane += 16) {
        const float nx = ld(plane, 0), ny = ld(plane, 4), nz = ld(plane, 8), d = ld(plane, 12);
        const double new_dist = ((((double)nz * pz) + ((double)py * ny)) + ((double)px * nx)) - d;
        const double old_dot = (((double)oy * ny) + ((double)oz * nz)) + ((double)ox * nx);
        const double old_dist = old_dot - d;
        if (!(old_dist <= 0.0))
            flags |= 8;
        else if ((double)old_max < old_dist) {
            old_max = (float)old_dist;
            old_n[0] = nx; old_n[1] = ny; old_n[2] = nz;
        }
        if (!(new_dist <= (double)cd))
            flags |= 1;
        else if ((double)new_max < new_dist) {
            new_max = (float)new_dist;
            new_n[0] = nx; new_n[1] = ny; new_n[2] = nz;
        }
        if (!((double)cd2 >= new_dist))
            flags |= 4;
        else if (new_dist > 0.0) {
            if ((double)face_dist == -(double)FLT_MAX) {
                face_dist = (float)new_dist;
                face_n[0] = nx; face_n[1] = ny; face_n[2] = nz;
            } else
                flags |= 4;
        }
        const double along = (((double)nz * dz) + ((double)nx * dx)) + ((double)ny * dy);
        zero = 0.0;
        if (along == 0.0) {
            if (new_dist > 0.0) flags |= 2;
        } else {
            const double t = ((double)d - old_dot) / along;
            if (!(along < 0.0)) {
                if ((double)exit_min > t) exit_min = (float)t;
            } else if (t > entry) {
                entry = t;
                hit_n[0] = nx; hit_n[1] = ny; hit_n[2] = nz;
            }
        }
    }
    if (!(flags & 8)) {
        const double k = (double)cd - old_max;
        or16(p, 4, 4);
        stf(p, 0x00, zero);
        stf(p, 0x20, old_n[0]); stf(p, 0x24, old_n[1]); stf(p, 0x28, old_n[2]);
        stf(p, 0x2C, ((double)old_n[0] * k) + ox);
        stf(p, 0x30, ((double)old_n[1] * k) + oy);
        stf(p, 0x34, ((double)old_n[2] * k) + oz);
        return;
    }
    if (zero > entry || entry >= 1.0 || (double)exit_min < entry || (flags & 2)) {
        if (!(p[4] & 2)) {
            if (!(flags & 4)) {
                const double k = (double)cd - face_dist;
                stf(p, 0x20, face_n[0]); stf(p, 0x24, face_n[1]); stf(p, 0x28, face_n[2]);
                stf(p, 0x2C, ((double)face_n[0] * k) + px);
                stf(p, 0x30, ((double)face_n[1] * k) + py);
                stf(p, 0x34, ((double)face_n[2] * k) + pz);
                or16(p, 4, 0x10);
            }
            if (!(flags & 1)) {
                const double k = (double)cd - new_max;
                stf(p, 0x20, new_n[0]); stf(p, 0x24, new_n[1]); stf(p, 0x28, new_n[2]);
                stf(p, 0x2C, ((double)new_n[0] * k) + px);
                stf(p, 0x30, ((double)new_n[1] * k) + py);
                stf(p, 0x34, ((double)new_n[2] * k) + pz);
                or16(p, 4, 8);
            }
        }
        return;
    }
    stf(p, 0x20, hit_n[0]); stf(p, 0x24, hit_n[1]); stf(p, 0x28, hit_n[2]);
    stf(p, 0x2C, (hit_n[0] * cd) + (((double)dx * entry) + ox));
    stf(p, 0x30, (hit_n[1] * cd) + (((double)dy * entry) + oy));
    stf(p, 0x34, (hit_n[2] * cd) + (((double)dz * entry) + oz));
    or16(p, 4, 4);
    stf(p, 0x00, entry);
}

/* The records are equal word for word; a float word that is a NaN in both may differ in its sign and
 * payload: where two different NaNs meet (a NaN collision distance plus an inf - inf), the compiler
 * may commute the operation, and C leaves which NaN wins open. The flags word (+4) must be equal. */
static int same_record(const uint8_t *a, const uint8_t *b)
{
    for (int i = 0; i < 0x38; i += 4) {
        uint32_t x, y;
        memcpy(&x, a + i, 4);
        memcpy(&y, b + i, 4);
        if (x == y)
            continue;
        if (i != 4 && (x & 0x7FFFFFFFu) > 0x7F800000u && (y & 0x7FFFFFFFu) > 0x7F800000u)
            continue;
        return 0;
    }
    return 1;
}

static uint64_t state = 0x9E3779B97F4A7C15ull;

static uint64_t next(void)
{
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
}

static float uniform(float range) { return (float)((double)(next() >> 11) / 9007199254740992.0 * 2.0 - 1.0) * range; }

static float special(void)
{
    static const float values[] = { 0.0f, -0.0f, 1.0f, -1.0f, 0.5f, -0.5f, 1e-45f, -1e-45f, FLT_MIN, -FLT_MIN,
                                    FLT_MAX, -FLT_MAX, INFINITY, -INFINITY, NAN, -NAN, 1e30f, -1e30f, 3.0e38f };
    return values[next() % (sizeof values / sizeof values[0])];
}

static float nudge(float v)
{
    switch (next() % 4) {
    case 0: return nextafterf(v, INFINITY);
    case 1: return nextafterf(v, -INFINITY);
    default: return v;
    }
}

static float coordinate(int kind, float scale, const float e[3], int axis)
{
    switch (kind) {
    case 0: return uniform(scale);
    case 1: return special();
    case 2: return nudge(e[axis]);                           /* on or next to a face */
    default: return nudge(-e[axis]);
    }
}

int main(int argc, char **argv)
{
    const unsigned long long cases = argc > 1 ? strtoull(argv[1], 0, 10) : 20000000ull;
    unsigned long long mismatches = 0, general = 0, nan_only = 0;
    static const float scales[] = { 0.01f, 1.0f, 100.0f, 10000.0f };
    for (unsigned long long n = 0; n < cases; ++n) {
        uint8_t a[0x38], b[0x38];
        for (int i = 0; i < 0x38; i += 8) {
            uint64_t r = next();
            memcpy(a + i, &r, 8);
        }
        const int odd = (next() % 16) == 0;                  /* one case in 16 with special fields */
        const float scale = scales[next() % 4];
        float e[3];
        for (int i = 0; i < 3; ++i)
            e[i] = odd && (next() % 4) == 0 ? special() : (float)(0.001 + (double)(next() >> 11) / 9007199254740992.0 * scale);
        float cd = odd && (next() % 4) == 0 ? special() : (float)((double)(next() >> 11) / 9007199254740992.0 * 0.5);
        float o[3], p[3];
        for (int i = 0; i < 3; ++i) {
            const int kind = odd ? (int)(next() % 4) : (next() % 8) == 0 ? 2 + (int)(next() % 2) : 0;
            o[i] = coordinate(kind == 1 ? 1 : kind, scale * 1.5f, e, i);
            switch (next() % 6) {
            case 0: p[i] = o[i]; break;                      /* no move along this axis */
            case 1: p[i] = coordinate(2 + (int)(next() % 2), scale, e, i); break;
            default: p[i] = odd && (next() % 3) == 0 ? special() : o[i] + uniform(scale * 0.2f); break;
            }
        }
        memcpy(a + 0x08, o, 12);
        memcpy(a + 0x14, p, 12);
        memcpy(b, a, sizeof a);
        const float planes[24] = { 1, 0, 0, e[0],  -1, 0, 0, e[0],  0, 1, 0, e[1],  0, -1, 0, e[1],
                                   0, 0, 1, e[2],  0, 0, -1, e[2] };
        /* The refactored general path against the checked one, on these planes and on a random
         * plane set (unit-ish normals, any d, sometimes special components). */
        {
            uint8_t c[0x38], d1[0x38], d2[0x38];
            memcpy(c, a, sizeof c);
            fluid_convex_checked(cd, c, 6, (const uint8_t *)planes);
            float random_planes[48];
            const uint32_t count = 1 + (uint32_t)(next() % 12);
            for (uint32_t i = 0; i < count * 4; ++i)
                random_planes[i] = odd && (next() % 8) == 0 ? special() : (i % 4 == 3 ? uniform(scale) : uniform(1.0f));
            memcpy(d1, a, sizeof d1);
            memcpy(d2, a, sizeof d2);
            fluid_convex_checked(cd, d1, count, (const uint8_t *)random_planes);
            fluid_convex(cd, d2, count, (const uint8_t *)random_planes);
            fluid_convex(cd, a, 6, (const uint8_t *)planes);
            if (!same_record(a, c) || !same_record(d1, d2)) {
                if (mismatches++ < 16) {
                    const int box_planes = !same_record(a, c);
                    printf("MISMATCH case %llu: the refactored general path differs from the checked one (%s) "
                        "cd %a o %a %a %a p %a %a %a\n", n, box_planes ? "box planes" : "random planes", cd,
                        o[0], o[1], o[2], p[0], p[1], p[2]);
                    const uint8_t *x = box_planes ? c : d1, *y = box_planes ? a : d2;
                    for (int i = 0; i < 0x38; i += 4) {
                        uint32_t u, v;
                        memcpy(&u, x + i, 4);
                        memcpy(&v, y + i, 4);
                        if (u != v) printf("  +0x%02X checked %08X refactored %08X\n", i, u, v);
                    }
                    if (!box_planes)
                        for (uint32_t i = 0; i < count; ++i)
                            printf("  plane %u: %a %a %a %a\n", i, random_planes[4 * i], random_planes[4 * i + 1],
                                random_planes[4 * i + 2], random_planes[4 * i + 3]);
                }
            }
        }
        fluid_box(cd, b, e);
        int finite = e[0] != 0.0f && e[1] != 0.0f && e[2] != 0.0f;
        const float all[9] = { o[0], o[1], o[2], p[0], p[1], p[2], e[0], e[1], e[2] };
        for (int i = 0; i < 9; ++i)
            finite &= isfinite(all[i]);
        general += !finite;
        nan_only += memcmp(a, b, sizeof a) != 0 && same_record(a, b);
        if (!same_record(a, b)) {
            if (mismatches++ < 16) {
                printf("MISMATCH case %llu cd %a e %a %a %a o %a %a %a p %a %a %a\n", n, cd, e[0], e[1], e[2],
                    o[0], o[1], o[2], p[0], p[1], p[2]);
                for (int i = 0; i < 0x38; i += 4) {
                    uint32_t x, y;
                    memcpy(&x, a + i, 4);
                    memcpy(&y, b + i, 4);
                    if (x != y) printf("  +0x%02X general %08X box %08X\n", i, x, y);
                }
            }
        }
    }
    printf("%s: %llu cases (%llu through the general path), %llu mismatches, %llu box results that differ "
        "only in a NaN both have\n", mismatches ? "FAIL" : "PASS", cases, general, mismatches, nan_only);
    return mismatches != 0;
}
