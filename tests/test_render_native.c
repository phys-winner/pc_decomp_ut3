/* The oracle bodies and register snapshots are extracted from production sources. */
#include "host.h"
#include <assert.h>
#include <sys/mman.h>
#include <time.h>
#include "../android/host/render_native.c"

enum { STACK = 0x20008, A = 0x30000, B = 0x30100, OUT = 0x30200,
       FRUSTUM = 0x40000, CENTER = 0x40100, PLANES = 0x40200,
       BOUNDS = 0x50000, LIGHT = 0x50000, PRIMITIVE = 0x50100, VIEW = 0x50200,
       OBJECT = 0x50300, VTABLE = 0x50400, MARKER = 0x507F0, BOUNDS_CALLBACK = 0x7F010000 };
static uint32_t random_state = 0x195EA55;
static unsigned yield_calls, failures;
recomp_ehframe_t *g_eh_frames;
uint32_t g_eh_obj, g_eh_info;
static uint32_t random_u32(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    return random_state ^= random_state << 5;
}
static float random_float(void)
{
    const uint32_t bits = (random_u32() & 0x807FFFFFu) | ((110u + random_u32() % 35u) << 23);
    float result;
    memcpy(&result, &bits, 4);
    return result;
}
static void yield_probe(void) { ++yield_calls; }
const char *void_data_dir(void) { return "/nonexistent"; }
void void_log(const char *fmt, ...) { (void)fmt; }
void void_dispatch_add(uint32_t address, recomp_func_t fn) { (void)address; (void)fn; }

extern void render_bounds_oracle(void), render_bounds_masked_oracle(void);
extern void render_bounds_masked_call(recomp_func_t target, const uint32_t *input, uint32_t *output);
static recomp_func_t bounds_fallback = render_bounds_oracle;
static unsigned bounds_original_calls, bounds_checks, bounds_fast_rejects, bounds_callbacks, bounds_mutate;
static struct { unsigned calls; void_regs entry; uint32_t stack[16]; } bounds_trace;
void __real_sub_0117D890(void) { ++bounds_original_calls; bounds_fallback(); }
/* The callback returns false, so these deeper survivor paths must remain unreachable. */
void sub_0041FF80(void) { abort(); }
void sub_0041FF60(void) { abort(); }
void sub_0117C2A0(void) { abort(); }
static void bounds_callback(void)
{
    assert(++bounds_trace.calls == 1);
    void_regs_save(&bounds_trace.entry);
    for (unsigned i = 0; i < 16; ++i) bounds_trace.stack[i] = MEM32(g_esp + 4u * i);
    MEM32(MARKER) = ARG(0) ^ g_ecx ^ g_eax;
    if (bounds_mutate)
        for (unsigned i = 0; i < 3; ++i) MEM32(g_esp + 8u + 4u * i) = 0xABC00000u + i;
    g_eax = 0; g_ecx = 0x12345678u; g_edx = 0x23456789u;
    g_flag_k = FK_ADD; g_flag_a = 1; g_flag_b = 2; g_flag_cf = 0;
    g_xmm[7].u32[2] ^= 0x55667788u;
    g_esp += 8u;
}
recomp_func_t void_resolve(uint32_t address) { assert(address == BOUNDS_CALLBACK); return bounds_callback; }

static void fixture(void)
{
    g_eax = random_u32(); g_ecx = random_u32(); g_edx = random_u32();
    g_ebx = random_u32(); g_esp = STACK; g_ebp = random_u32();
    g_esi = random_u32(); g_edi = random_u32();
    g_flag_k = FK_NONE; g_flag_a = random_u32(); g_flag_b = random_u32(); g_flag_cf = 1;
    g_backedges = random_u32() & 65535u;
    for (unsigned j = 0; j < 8; ++j)
        for (unsigned k = 0; k < 4; ++k) g_xmm[j].u32[k] = random_u32();
    MEM32(STACK) = RECOMP_RETADDR;
}

static void compare(unsigned kernel, uint32_t output, unsigned case_id)
{
    void_regs entry = {0}, native = {0}, lifted = {0};
    uint8_t memory[64], expected[64];
    if (!kernel) memcpy(memory, GPTR(output), sizeof memory);
    void_regs_save(&entry);
    render_mode = 1;
    yield_calls = 0;
    if (kernel) __wrap_sub_0117C3B0(); else __wrap_sub_00486030();
    const unsigned native_yields = yield_calls;
    void_regs_save(&native);
    if (!kernel) {
        memcpy(expected, GPTR(output), sizeof expected);
        memcpy(GPTR(output), memory, sizeof memory);
    }
    void_regs_load(&entry);
    render_mode = 0;
    yield_calls = 0;
    if (kernel) __wrap_sub_0117C3B0(); else __wrap_sub_00486030();
    void_regs_save(&lifted);
    if (memcmp(&native, &lifted, sizeof native) || native_yields != yield_calls ||
        (!kernel && memcmp(expected, GPTR(output), sizeof expected))) {
        ++failures;
        fprintf(stderr, "%s case %u differs: eax %08x/%08x edx %08x/%08x flags %u/%u %08x/%08x %08x/%08x backedges %u/%u\n",
            kernel ? "sphere" : "matrix", case_id, native.eax, lifted.eax, native.edx, lifted.edx,
            native.flag_k, lifted.flag_k, native.flag_a, lifted.flag_a, native.flag_b, lifted.flag_b,
            native.backedges, lifted.backedges);
        for (unsigned j = 0; j < 8; ++j) for (unsigned k = 0; k < 4; ++k)
            if (native.xmm[j].u32[k] != lifted.xmm[j].u32[k])
                fprintf(stderr, "xmm%u.%u %08x/%08x\n", j, k, native.xmm[j].u32[k], lifted.xmm[j].u32[k]);
        assert(0);
    }
}

static void bounds_live_state(void_regs *r)
{
    r->ebx = r->ebp = r->esi = r->edi = 0;
    r->flag_k = r->flag_a = r->flag_b = r->flag_cf = 0;
}

static void bounds_compare(unsigned case_id, int expected_path, int expected_callback)
{
    void_regs entry = {0};
    void_regs_save(&entry);
    const uint32_t stack = entry.esp - 128u;
    uint8_t stack_before[256], stack_after[256], objects_before[0x800], objects_after[0x800];
    memcpy(stack_before, GPTR(stack), sizeof stack_before);
    memcpy(objects_before, GPTR(BOUNDS), sizeof objects_before);
    uint32_t caller[12], native_locals[12], original_locals[12];
    memcpy(caller, &entry, 8u * sizeof(uint32_t));
    caller[3] ^= 0xA5A50001u; caller[5] ^= 0xA5A50002u;
    caller[6] ^= 0xA5A50003u; caller[7] ^= 0xA5A50004u; caller[4] += 4u;
    caller[8] = FK_ADD; caller[9] = 0x1234u; caller[10] = 0x5678u; caller[11] = 0;
    for (int masked = 0; masked < 2; ++masked) {
        void_regs native = {0}, original = {0};
        memcpy(GPTR(stack), stack_before, sizeof stack_before);
        memcpy(GPTR(BOUNDS), objects_before, sizeof objects_before);
        void_regs_load(&entry);
        bounds_fallback = masked ? render_bounds_masked_oracle : render_bounds_oracle;
        bounds_original_calls = 0; memset(&bounds_trace, 0, sizeof bounds_trace);
        if (masked) render_bounds_masked_call(__wrap_sub_0117D890, caller, native_locals);
        else __wrap_sub_0117D890();
        void_regs_save(&native);
        const unsigned originals = bounds_original_calls;
        __typeof__(bounds_trace) trace = bounds_trace;
        memcpy(stack_after, GPTR(stack), sizeof stack_after);
        memcpy(objects_after, GPTR(BOUNDS), sizeof objects_after);
        memcpy(GPTR(stack), stack_before, sizeof stack_before);
        memcpy(GPTR(BOUNDS), objects_before, sizeof objects_before);
        void_regs_load(&entry); memset(&bounds_trace, 0, sizeof bounds_trace);
        if (masked) render_bounds_masked_call(bounds_fallback, caller, original_locals);
        else bounds_fallback();
        void_regs_save(&original);
        if (masked) {
            bounds_live_state(&native); bounds_live_state(&original);
            bounds_live_state(&trace.entry); bounds_live_state(&bounds_trace.entry);
        }
        int same = !memcmp(&native, &original, sizeof native) && !memcmp(&trace, &bounds_trace, sizeof trace) &&
            !memcmp(stack_after, GPTR(stack), sizeof stack_after) &&
            !memcmp(objects_after, GPTR(BOUNDS), sizeof objects_after) && originals <= 1 &&
            (expected_path < 0 || originals == (unsigned)expected_path) &&
            trace.calls == (unsigned)expected_callback && (!trace.calls || originals == 1) &&
            (!masked || (!memcmp(native_locals, original_locals, sizeof native_locals) &&
                native_locals[3] == caller[3] && native_locals[5] == caller[5] &&
                native_locals[6] == caller[6] && native_locals[7] == caller[7] &&
                !memcmp(native_locals + 8, caller + 8, 4u * sizeof(uint32_t))));
        if (!same) {
            fprintf(stderr, "bounds case %u masked %d differs: state %d stack %d objects %d trace %d originals %u/%d callback %u/%d\n",
                case_id, masked, memcmp(&native, &original, sizeof native), memcmp(stack_after, GPTR(stack), sizeof stack_after),
                memcmp(objects_after, GPTR(BOUNDS), sizeof objects_after), memcmp(&trace, &bounds_trace, sizeof trace),
                originals, expected_path, trace.calls, expected_callback);
            abort();
        }
        ++bounds_checks; bounds_fast_rejects += !originals; bounds_callbacks += trace.calls;
    }
}

static void bounds_cases(unsigned cases, const uint32_t *exceptional, unsigned exceptional_count)
{
    for (unsigned n = 0; n < cases; ++n) {
        fixture(); g_esp = STACK + ((n / 12u) & 3u) * 4u;
        memset(GPTR(g_esp - 128u), 0xC7, 256);
        memset(GPTR(BOUNDS), 0, 0x800);
        const unsigned kind = n % 12u;
        uint32_t self = LIGHT, argument = PRIMITIVE;
        const uint32_t frame = ((g_esp - 4u) & ~15u) - 32u;
        int path = kind == 0 ? 0 : kind <= 3 || kind == 6 || kind >= 8 ? 1 : -1;
        if (kind == 7) argument = self;
        if (kind == 8) self = frame - 16u + ((n / 48u) % 4u) * 4u;
        if (kind == 9) argument = frame - 32u + ((n / 12u) % 5u) * 4u;
        if (kind == 10) argument = g_esp - 20u;
        if (kind == 11) { if (n & 16u) self = 0; else argument = 0; }
        if (kind >= 8) g_ebx = g_esi = g_edi = g_ebp = 0;
        MEM32(self) = VIEW; MEM32(self + 4u) = 7; MEM32(self + 8u) = 2;
        MEM32(argument) = OBJECT; MEM32(argument + 12u) = 7; MEM32(argument + 48u) = 2;
        for (unsigned axis = 0; axis < 4; ++axis) {
            MEMF(self + 16u + axis * 4u) = axis == 3 ? 1.0f : 0.0f;
            MEMF(argument + 16u + axis * 4u) = 0.0f;
        }
        MEMF(argument + 40u) = 0.0f;
        MEMF(self + 16u) = kind == 0 ? 3.0f : kind == 2 ? 0.5f : kind == 6 ? 0.0f : 1.0f;
        if (kind == 0 && (n & 16u)) MEM32(self + 16u) = 0x3F800001u; /* just outside tangent */
        if (kind == 2 && (n & 16u)) MEM32(self + 16u) = 0x3F7FFFFFu; /* just inside tangent */
        if (kind == 3) MEMF(self + 28u) = -1.0f;
        if (kind == 4) {
            const unsigned lane = (n / 12u) % 9u;
            const uint32_t address = lane < 4 ? self + 16u + lane * 4u :
                lane < 8 ? argument + 16u + (lane - 4u) * 4u : argument + 40u;
            MEM32(address) = exceptional[(n / 108u) % exceptional_count];
        }
        if (kind == 5) {
            for (unsigned axis = 0; axis < 4; ++axis) {
                MEMF(self + 16u + axis * 4u) = random_float();
                MEMF(argument + 16u + axis * 4u) = random_float();
            }
            MEMF(argument + 40u) = random_float();
        }
        MEM8(argument + 52u) = kind == 6 ? 2 : 0;
        MEM32(VIEW) = VTABLE; MEM32(VTABLE + 8u) = BOUNDS_CALLBACK;
        MEM32(g_esp) = RECOMP_RETADDR; MEM32(g_esp + 4u) = argument; g_ecx = self;
        bounds_mutate = (n / 48u) & 1u;
        render_mode = n % 67u == 0 ? 0 : n % 71u == 0 ? 2 : 1;
        if (render_mode != 1) path = 1;
        bounds_compare(n, path, kind == 6);
    }
}

int main(int argc, char **argv)
{
    const unsigned cases = argc > 1 ? (unsigned)strtoul(argv[1], NULL, 10) : 200000;
    void *window = mmap((void *)VOID_GUEST_BASE, 0x2000000, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    assert(window == (void *)VOID_GUEST_BASE);
    recomp_yield_hook = yield_probe;
    const uint32_t exceptional[] = {0, 0x80000000, 0x00000001, 0x80000001, 0x007FFFFF,
        0x00800000, 0x7F7FFFFF, 0xFF7FFFFF, 0x7F800000, 0xFF800000, 0x7FC12345, 0x7F812345};
    for (unsigned n = 0; n < cases; ++n) {
        fixture();
        for (unsigned j = 0; j < 16; ++j) {
            MEMF(A + 4 * j) = random_float();
            MEMF(B + 4 * j) = random_float();
        }
        if (n < 384) MEM32((n & 1 ? A : B) + 4 * ((n / 2) % 16)) = exceptional[n / 32];
        const uint32_t output = n % 4 == 0 ? A : n % 4 == 1 ? B : n % 4 == 2 ? A + 16 : OUT;
        g_ecx = A; ARG(0) = output; ARG(1) = B;
        compare(0, output, n);
        fixture();
        MEM32(FRUSTUM + 12) = PLANES;
        const int32_t count = (int32_t)(n % 35) - 2;
        MEM32(FRUSTUM + 16) = (uint32_t)count;
        for (unsigned j = 0; j < 4; ++j) MEMF(CENTER + j * 4) = random_float();
        for (unsigned j = 0; j < 128; ++j) MEMF(PLANES + j * 4) = random_float();
        g_ecx = FRUSTUM; ARG(0) = CENTER;
        MEMF(STACK + 8) = n & 1 ? random_float() : 1.0e30f;
        if (n < 384) MEM32((n & 1 ? CENTER : PLANES) + 4 * ((n / 2) % 4)) = exceptional[n / 32];
        if (n % 97 == 0) g_backedges = RECOMP_YIELD_EVERY - 1;
        compare(1, 0, n);
    }
    /* Touching a plane is inside; a padded fourth lane is still tested. */
    fixture();
    memset(GPTR(PLANES), 0, 64);
    memset(GPTR(CENTER), 0, 16);
    MEM32(FRUSTUM + 12) = PLANES; MEM32(FRUSTUM + 16) = 1;
    g_ecx = FRUSTUM; ARG(0) = CENTER; MEMF(STACK + 8) = 1;
    MEMF(PLANES + 60) = -1;
    compare(1, 0, cases);
    assert(g_eax == 1);
    fixture(); g_ecx = FRUSTUM; ARG(0) = CENTER; MEMF(STACK + 8) = 1;
    MEMF(PLANES + 60) = -2;
    compare(1, 0, cases + 1);
    assert(g_eax == 0);
    /* Exercise the production compare path, which must leave the original state. */
    fixture(); g_ecx = FRUSTUM; ARG(0) = CENTER; MEMF(STACK + 8) = 1;
    render_mode = 2; __wrap_sub_0117C3B0();
    fixture(); g_ecx = A; ARG(0) = A; ARG(1) = B;
    render_mode = 2; __wrap_sub_00486030();
    assert(render_checked[0] == 1 && render_checked[1] == 1);
    assert(!render_mismatches[0] && !render_mismatches[1] && !failures);
    bounds_cases(cases, exceptional, sizeof exceptional / sizeof exceptional[0]);
    printf("PASS native render: %u matrix + %u sphere cases, output/GPR/XMM/flags/yields exact; alias/NaN/Inf/subnormal/boundary cases\n",
        cases + 1, cases + 3);
    printf("PASS native bounds: %u raw/masked cases, %u fast rejects, %u callbacks; full state/stack, old 0x17 contract, alias/NaN/Inf/tangent and fallback-once exact\n",
        bounds_checks, bounds_fast_rejects, bounds_callbacks);
    munmap(window, 0x2000000);
    return 0;
}
