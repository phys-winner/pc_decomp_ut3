/* Shared D3DX matrix math. Matrices are 16 row-major IEEE binary32 values;
 * use local storage so pOut may overlap pM. No renderer state is involved. */
#include "host.h"
#include <math.h>

WINAPI_EXPORT("d3dx9_25.dll", D3DXMatrixInverse, 3)
{
    uint32_t out = ARG(0), determinant = ARG(1), source = ARG(2);
    if (!out || !source || !void_vm_readable(source, 64) || !void_vm_writable(out, 64) ||
        (determinant && !void_vm_writable(determinant, 4))) return 0;
    float matrix[16], adjugate[16];
    memcpy(matrix, GPTR(source), sizeof matrix);
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
    float det = matrix[0] * adjugate[0] + matrix[1] * adjugate[4]
              + matrix[2] * adjugate[8] + matrix[3] * adjugate[12];
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
