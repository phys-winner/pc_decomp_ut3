/* Shared D3DX matrix math. Matrices are 16 row-major IEEE binary32 values;
 * use local storage so pOut may overlap pM. No renderer state is involved. */
#include "host.h"
#include "native_mode.inc"
#include <math.h>

/* Exact mode's inverse (and the only one before 2026-11-08): each cofactor from its
 * 3x3 minor, the determinant along the first row. */
static float inverse_cofactors(const float matrix[16], float adjugate[16])
{
    for (unsigned row = 0; row < 4; ++row) {
        for (unsigned column = 0; column < 4; ++column) {
            float minor[9];
            unsigned n = 0;
            for (unsigned r = 0; r < 4; ++r)
                if (r != row)
                    for (unsigned c = 0; c < 4; ++c)
                        if (c != column) minor[n++] = matrix[r * 4 + c];
            float cofactor = minor[0] * (minor[4] * minor[8] - minor[5] * minor[7])
                           - minor[1] * (minor[3] * minor[8] - minor[5] * minor[6])
                           + minor[2] * (minor[3] * minor[7] - minor[4] * minor[6]);
            adjugate[column * 4 + row] = (row + column) & 1u ? -cofactor : cofactor;
        }
    }
    return matrix[0] * adjugate[0] + matrix[1] * adjugate[4]
         + matrix[2] * adjugate[8] + matrix[3] * adjugate[12];
}

/* Optimal mode: the same adjugate from the twelve 2x2 minors of the upper and lower
 * row pairs (Laplace expansion by complementary minors), about half the operations and
 * no index loops. FMUL/FSUB/FADD only (-ffp-contract=off), so the results are the same
 * on every build; they differ from the cofactor form in the last bits, as both differ
 * from Microsoft's D3DX (tests/test_d3dx_matrix.c holds both to its native vectors).
 * UE3's FMatrix::Inverse calls D3DXMatrixInverse about 1300 times per frame in a 32-bot
 * WAR-Torlan match; the cofactor form was 0.7% of the game thread (2026-11-08). */
static float inverse_minors(const float m[16], float b[16])
{
    const float s0 = m[0] * m[5] - m[4] * m[1], s1 = m[0] * m[6] - m[4] * m[2];
    const float s2 = m[0] * m[7] - m[4] * m[3], s3 = m[1] * m[6] - m[5] * m[2];
    const float s4 = m[1] * m[7] - m[5] * m[3], s5 = m[2] * m[7] - m[6] * m[3];
    const float c5 = m[10] * m[15] - m[14] * m[11], c4 = m[9] * m[15] - m[13] * m[11];
    const float c3 = m[9] * m[14] - m[13] * m[10], c2 = m[8] * m[15] - m[12] * m[11];
    const float c1 = m[8] * m[14] - m[12] * m[10], c0 = m[8] * m[13] - m[12] * m[9];
    b[0] = m[5] * c5 - m[6] * c4 + m[7] * c3;
    b[1] = -m[1] * c5 + m[2] * c4 - m[3] * c3;
    b[2] = m[13] * s5 - m[14] * s4 + m[15] * s3;
    b[3] = -m[9] * s5 + m[10] * s4 - m[11] * s3;
    b[4] = -m[4] * c5 + m[6] * c2 - m[7] * c1;
    b[5] = m[0] * c5 - m[2] * c2 + m[3] * c1;
    b[6] = -m[12] * s5 + m[14] * s2 - m[15] * s1;
    b[7] = m[8] * s5 - m[10] * s2 + m[11] * s1;
    b[8] = m[4] * c4 - m[5] * c2 + m[7] * c0;
    b[9] = -m[0] * c4 + m[1] * c2 - m[3] * c0;
    b[10] = m[12] * s4 - m[13] * s2 + m[15] * s0;
    b[11] = -m[8] * s4 + m[9] * s2 - m[11] * s0;
    b[12] = -m[4] * c3 + m[5] * c1 - m[6] * c0;
    b[13] = m[0] * c3 - m[1] * c1 + m[2] * c0;
    b[14] = -m[12] * s3 + m[13] * s1 - m[14] * s0;
    b[15] = m[8] * s3 - m[9] * s1 + m[10] * s0;
    return s0 * c5 - s1 * c4 + s2 * c3 + s3 * c2 - s4 * c1 + s5 * c0;
}

/* Pointers into the calling thread's guest stack (the committed range its TIB names) need
 * no page-table walk; anything else is checked page by page. */
static int matrix_access(uint32_t address, uint32_t size, int write)
{
    const uint32_t tib = g_fs_base;
    if (tib) {
        const uint32_t base = MEM32(tib + 4), limit = MEM32(tib + 8);
        if (address >= limit && address < base && base - address >= size) return 1;
    }
    return write ? void_vm_writable(address, size) : void_vm_readable(address, size);
}

WINAPI_EXPORT("d3dx9_25.dll", D3DXMatrixInverse, 3)
{
    static int optimal = -1;
    if (__builtin_expect(optimal < 0, 0)) optimal = void_native_mode(1);
    uint32_t out = ARG(0), determinant = ARG(1), source = ARG(2);
    if (!out || !source || !matrix_access(source, 64, 0) || !matrix_access(out, 64, 1) ||
        (determinant && !matrix_access(determinant, 4, 1))) return 0;
    float matrix[16], adjugate[16];
    uint32_t bits[16], special = 0;
    memcpy(matrix, GPTR(source), sizeof matrix);
    memcpy(bits, matrix, sizeof bits);
    /* An infinity or NaN makes the 2x2 minors multiply it by zeros the cofactor form never
     * pairs with it (native D3DX: diag(inf,1,1,1) has det +inf, not NaN): keep that form. */
    for (unsigned i = 0; i < 16; ++i) special |= (bits[i] & 0x7F800000u) == 0x7F800000u;
    float det = optimal && !special ? inverse_minors(matrix, adjugate) : inverse_cofactors(matrix, adjugate);
    if (determinant) memcpy(GPTR(determinant), &det, sizeof det);
    if (det == 0.0f) return 0;
    float reciprocal = 1.0f / det;
    /* Native D3DX10_35 also rejects a nonfinite binary32 reciprocal. A tiny
     * determinant is accepted whenever its inverse is representable; there
     * is no arbitrary near-singular epsilon. Output stays intact on failure. */
    if (!isfinite(reciprocal)) return 0;
    for (unsigned i = 0; i < 16; ++i) adjugate[i] *= reciprocal;
    memcpy(GPTR(out), adjugate, sizeof adjugate);
    return out;
}
