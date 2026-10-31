/* Production shared D3DX math against independent native Windows fixtures. */
#include "../android/host/d3dx_math.c"
#include "../android/host/pe.c"
#include "d3dx_matrix_vectors.h"
#include <assert.h>
#include <float.h>
#include <unistd.h>

void void_process_exit(uint32_t code, int detach) { (void)detach; _exit((int)code); }
static double max_normalized_error;

static float as_float(uint32_t bits) { float value; memcpy(&value, &bits, 4); return value; }

static void close_float(uint32_t actual_bits, uint32_t expected_bits)
{
    float actual = as_float(actual_bits), expected = as_float(expected_bits);
    if (isnan(expected)) { assert(isnan(actual)); return; }
    if (isinf(expected)) { assert(actual == expected); return; }
    assert(isfinite(actual));
    double error = fabs((double)actual - expected);
    double scale = fabs((double)expected);
    double normalized = error / fmax(scale, 1.0);
    if (normalized > max_normalized_error) max_normalized_error = normalized;
    if (expected == 0) assert(error <= 1e-7);
    else assert(error <= scale * 2e-5 + 2.0 * FLT_TRUE_MIN);
}

static uint32_t inverse_call(uint32_t target, uint32_t out, uint32_t determinant, uint32_t input)
{
    uint32_t args[3] = {out, determinant, input}, before = g_esp;
    void_set_last_error(0x1234);
    uint32_t result = void_call_guest(target, 3, args, 1);
    assert(g_esp == before && void_get_last_error() == 0x1234);
    return result;
}

static void compare_matrix(uint32_t matrix, const uint32_t expected[16])
{
    for (unsigned i = 0; i < 16; ++i) close_float(MEM32(matrix + i * 4), expected[i]);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    assert(void_guest_reserve());
    void_threads_init(); void_dispatch_init(); void_machine_enter(); add_system_pseudos();
    const char *libraries[] = {"d3dx10_35.dll", "d3dx9_35.dll", "d3dx9_25.dll"};
    uint32_t procedures[3], name = void_gstrdup("D3DXMatrixInverse");
    const void_shim *shared = void_shim_find(libraries[0], "D3DXMatrixInverse", 0);
    assert(shared);
    for (unsigned i = 0; i < 3; ++i) {
        uint32_t module = void_module_load(libraries[i]);
        assert(module && void_module_handle(libraries[i]) == module);
        procedures[i] = void_module_proc(module, name);
        assert(procedures[i] && void_shim_find(libraries[i], "D3DXMatrixInverse", 0) == shared);
    }
    void_gfree(name);
    uint32_t input = void_galloc(72), output = void_galloc(72), det = void_galloc(12);
    unsigned vectors = sizeof d3dx_matrix_vectors / sizeof *d3dx_matrix_vectors;
    for (unsigned p = 0; p < 3; ++p) {
        for (unsigned v = 0; v < vectors; ++v) {
            const d3dx_matrix_vector *vector = &d3dx_matrix_vectors[v];
            memset(GPTR(input), 0xA5, 72); memcpy(GPTR(input + 4), vector->input, 64);
            memset(GPTR(output), 0xA5, 72); memset(GPTR(det), 0xA5, 12);
            uint32_t result = inverse_call(procedures[p], output + 4, det + 4, input + 4);
            if (!!result != vector->success) printf("unexpected success %s: %08X\n", vector->name, result);
            assert(result == (vector->success ? output + 4 : 0));
            close_float(MEM32(det + 4), vector->determinant);
            if (vector->success) compare_matrix(output + 4, vector->output);
            else for (unsigned i = 0; i < 64; ++i) assert(MEM8(output + 4 + i) == 0xA5);
            assert(!memcmp(GPTR(input + 4), vector->input, 64));
            assert(MEM32(input) == 0xA5A5A5A5 && MEM32(input + 68) == 0xA5A5A5A5);
            assert(MEM32(output) == 0xA5A5A5A5 && MEM32(output + 68) == 0xA5A5A5A5);
            assert(MEM32(det) == 0xA5A5A5A5 && MEM32(det + 8) == 0xA5A5A5A5);
            if (vector->success) {
                memset(GPTR(output + 4), 0xA5, 64);
                assert(inverse_call(procedures[p], output + 4, 0, input + 4) == output + 4);
                compare_matrix(output + 4, vector->output);
            }
        }
    }
    /* Nonsymmetric matrix exposes transpose and row-vector layout mistakes. */
    const d3dx_matrix_vector *dense = &d3dx_matrix_vectors[3];
    uint32_t memory = void_galloc(88), source = memory + 8;
    const int offsets[] = {-4, 0, 4};
    for (unsigned i = 0; i < 3; ++i) {
        memset(GPTR(memory), 0xA5, 88); memcpy(GPTR(source), dense->input, 64);
        uint32_t out = source + offsets[i];
        assert(inverse_call(procedures[0], out, det + 4, source) == out);
        compare_matrix(out, dense->output); close_float(MEM32(det + 4), dense->determinant);
        assert(MEM32(memory) == 0xA5A5A5A5 && MEM32(memory + 80) == 0xA5A5A5A5);
    }
    memcpy(GPTR(input + 4), dense->input, 64);
    assert(inverse_call(procedures[0], output + 4, input + 4, input + 4) == output + 4);
    compare_matrix(output + 4, dense->output); close_float(MEM32(input + 4), dense->determinant);
    memcpy(GPTR(input + 4), dense->input, 64);
    assert(inverse_call(procedures[0], output + 4, output + 4, input + 4) == output + 4);
    compare_matrix(output + 4, dense->output);
    /* Check the mathematical invariant independently of the golden matrices. */
    for (unsigned row = 0; row < 4; ++row)
        for (unsigned column = 0; column < 4; ++column) {
            double sum = 0;
            for (unsigned k = 0; k < 4; ++k)
                sum += (double)as_float(dense->input[row * 4 + k]) * as_float(MEM32(output + 4 + (k * 4 + column) * 4));
            assert(fabs(sum - (row == column ? 1.0 : 0.0)) <= 3e-5);
        }
    memset(GPTR(output), 0xA5, 72); memset(GPTR(det), 0xA5, 12);
    assert(!inverse_call(procedures[0], output + 4, det + 4, 0x1000));
    assert(!inverse_call(procedures[0], 0x1000, det + 4, input + 4));
    assert(!inverse_call(procedures[0], output + 4, 0x1000, input + 4));
    for (unsigned i = 0; i < 72; ++i) assert(MEM8(output + i) == 0xA5);
    assert(MEM32(det + 4) == 0xA5A5A5A5);
    void_gfree(memory); void_gfree(input); void_gfree(output); void_gfree(det);
    printf("PASS D3DXMatrixInverse %u native vectors x3 modules; determinant/failure/NaN/Inf, optional determinant, overlaps, guards, stdcall, A*inverse=I; max normalized finite error %.9g\n", vectors, max_normalized_error);
    return 0;
}
