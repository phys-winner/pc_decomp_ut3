/* Differential fixture for the call-contract native kernels on ARM64:
 *   core_native.c       004B49A0 TMap<FName>::Find, 00B65BD0 TMap<int>::Find, 004B9D60/00F78830 TMap<UObject*>,
 *                       00E2DC90 two-word keys, 00BC1310/00BC12C0 the script VM's variable opcodes,
 *                       EC2580/EC2610/EC26A0/EC2730 shader bridges (raw full state and masked caller/callback contracts)
 *   collision_native.c  0116C250 segment/box slab test
 *   crt_native.c        msvcr80 memcpy/memmove/memset (against MSVC 8's memmove semantics)
 *   particle_native.c   00BF8DA0 baked distribution lookup
 * The lifted originals come from the generated sources (tests/test_contract_native.py). Each case
 * runs native and original from the same guest state and compares the call contract: EAX, ESP,
 * EBX/ESI/EDI/EBP and guest memory. ECX, EDX, the flags and XMM are not part of the contract
 * (tools/volatile_liveness.py proves no caller reads them after these calls). */
#include "host.h"
#include "../android/host/core_native.c"
#include "../android/host/collision_native.c"
#include "../android/host/crt_native.c"
#include "../android/host/particle_native.c"
#include <assert.h>
#include <sys/mman.h>

VOID_HIDDEN recomp_ehframe_t *g_eh_frames;
VOID_HIDDEN uint32_t g_eh_obj, g_eh_info;
static unsigned checks, failures, fallbacks_seen;
static uint32_t seed = 0x2C0FFEE5u;

void void_log(const char *format, ...) { (void)format; }
const char *void_data_dir(void) { return "/nonexistent-ut3-native-test"; }
void void_dispatch_add(uint32_t address, recomp_func_t fn) { (void)address; (void)fn; }
void_fn void_dispatch_find(uint32_t address) { (void)address; return NULL; }
recomp_func_t void_resolve_site(recomp_icache_t *site, uint32_t va) { site->key = ~va; return site->fn = void_resolve(va); }
/* The script VM's copy virtual: writes through Dest what it read from Src, a marker in EAX, ret 0x14. */
#define VM_COPY_VA 0x7F001000u
static void vm_copy_stub(void)
{
    MEM32(ARG(0)) = MEM32(ARG(1)) ^ 0x5A5A5A5Au ^ ARG(2) ^ g_ecx;
    g_eax = 0x11110000u ^ ARG(0) ^ ARG(1);
    g_esp += 24;
}
#define SHADER_VS_VA 0x7F002000u
#define SHADER_PS_VA 0x7F002010u
#define SHADER_RHI (ARENA + 0xB000u)
#define SHADER_VTABLE (ARENA + 0xB100u)
static void shader_callback(void);
recomp_func_t void_resolve(uint32_t va) {
    if (va == VM_COPY_VA) return vm_copy_stub;
    if (va == SHADER_VS_VA || va == SHADER_PS_VA) return shader_callback;
    abort();
}

static uint32_t random_u32(void) { seed = seed * 1664525u + 1013904223u; return seed ^ (seed >> 15); }

enum { MEMORY = 0x2000000, ARENA = 0x10000, STACK = 0x3F000, WATCH = 0x30000 };
static uint8_t snapshot[WATCH];
static unsigned shader_callback_mode, shader_calls, shader_asserts;
static void_regs shader_entry;
static uint32_t shader_stack[10];
extern void __masked_sub_00EC2580(void), __masked_sub_00EC2610(void), __masked_sub_00EC26A0(void), __masked_sub_00EC2730(void);
extern void shader_masked_call(recomp_func_t target, const uint32_t *input, uint32_t *output);

/* Exercise the assertion fallback without terminating: the game's diagnostic can install a new RHI. */
void sub_00B55380(void)
{
    ++shader_asserts;
    MEM32(0x01DEB3E8u) = SHADER_RHI;
    g_eax = 0x12345678u; g_ecx = 0x23456789u; g_edx = 0x3456789Au;
    g_flag_k = FK_ADD; g_flag_a = 0xBEEF; g_flag_b = 0x1234; g_flag_cf = 1;
    g_esp += 4u;
}

static void shader_callback(void)
{
    ++shader_calls;
    memset(&shader_entry, 0, sizeof shader_entry);
    void_regs_save(&shader_entry);
    for (unsigned i = 0; i < 10; ++i) shader_stack[i] = MEM32(g_esp + i * 4u);
    MEM32(ARENA + 0xC000u) = ARG(0) ^ ARG(1) ^ ARG(2) ^ ARG(3) ^ ARG(4) ^ g_ecx ^ g_eax;
    if (shader_callback_mode & 1u)
        for (unsigned i = 0; i < 5; ++i) MEM32(g_esp + 24u + i * 4u) = 0xABC00000u + i;
    g_eax = 0x98765432u; g_ecx = 0x87654321u; g_edx = 0x76543210u;
    g_ebx = 0x6543210Fu; g_esi = 0x543210FEu; g_edi = 0x43210FEDu; g_ebp = 0x3210FEDCu;
    g_flag_k = FK_CMP; g_flag_a = 0x11223344u; g_flag_b = 0x55667788u; g_flag_cf = 1;
    g_xmm[3].u32[2] ^= 0x66778899u;
    g_st[0] = 123.5; g_st_i64[0] = 0x1020304050607080LL;
    g_mxcsr ^= 0x40u; g_mm[2] ^= 0x33445566778899AAULL;
    g_esp += 24u + ((shader_callback_mode & 2u) ? 8u : 0u);
}

static void reset(uint32_t self, uint32_t arg0, uint32_t arg1)
{
    g_eax = random_u32(); g_ecx = self; g_edx = random_u32(); g_ebx = random_u32();
    g_esi = random_u32(); g_edi = random_u32(); g_ebp = random_u32(); g_esp = STACK - 0x40u;
    g_flag_k = FK_CMP; g_flag_a = random_u32(); g_flag_b = random_u32(); g_flag_cf = 1;
    g_backedges = 0; recomp_yield_hook = NULL;
    for (unsigned i = 0; i < 8; ++i)
        for (unsigned j = 0; j < 4; ++j) g_xmm[i].u32[j] = random_u32();
    MEM32(g_esp) = RECOMP_RETADDR; MEM32(g_esp + 4) = arg0; MEM32(g_esp + 8) = arg1;
}

/* Compares the contract after native() and original() from the same state; the stack below the
 * returned ESP is dead for the caller and excluded. */
static void compare(void (*native)(void), void (*original)(void), const char *name)
{
    void_regs before = {0}, a = {0}, b = {0};
    void_regs_save(&before);
    /* Guest memory the kernels may touch: the arena and the stack, [ARENA, ARENA + WATCH). */
    memcpy(snapshot, GPTR(ARENA), WATCH);
    native(); void_regs_save(&a);
    static uint8_t after[WATCH];
    memcpy(after, GPTR(ARENA), WATCH);
    memcpy(GPTR(ARENA), snapshot, WATCH);
    void_regs_load(&before);
    original(); void_regs_save(&b);
    ++checks;
    /* Below the returned ESP the stack is dead for the caller. */
    const uint32_t dead_lo = STACK - 0x200u - ARENA, dead_hi = b.esp - ARENA;
    int same = a.eax == b.eax && a.esp == b.esp && a.ebx == b.ebx && a.esi == b.esi && a.edi == b.edi &&
        a.ebp == b.ebp && !memcmp(after, GPTR(ARENA), dead_lo) &&
        !memcmp(after + dead_hi, GPTR(b.esp), WATCH - dead_hi);
    if (!same && ++failures <= 8)
        fprintf(stderr, "%s case %u differs: eax %08x/%08x esp %08x/%08x\n", name, checks, a.eax, b.eax, a.esp, b.esp);
}

static void map_cases(void)
{
    for (unsigned test = 0; test < 40000; ++test) {
        const int name = test & 1;
        const uint32_t stride = name ? 16u : 12u, count = test % 41, hash_size = 1u << (test % 5);
        const uint32_t map = ARENA, pairs = ARENA + 0x1000, hash = ARENA + 0x8000, key = ARENA + 0x9000;
        for (unsigned i = 0; i < 16; ++i) MEM32(hash + i * 4) = UINT32_MAX;
        MEM32(map) = pairs;
        MEM32(map + 4) = test % 23 == 3 ? UINT32_MAX - (test & 7) : count;   /* negative counts too */
        MEM32(map + 8) = 64;
        MEM32(map + 0xC) = test % 17 ? hash : 0;
        MEM32(map + 0x10) = hash_size;
        for (unsigned i = 0; i < count; ++i) {
            const uint32_t k = (random_u32() >> 20) & 31u, n = name ? (random_u32() >> 30) : 0;
            const uint32_t bucket = hash + (k & (hash_size - 1u)) * 4u;
            MEM32(pairs + i * stride) = MEM32(bucket);
            MEM32(pairs + i * stride + 4) = k;
            if (name) { MEM32(pairs + i * stride + 8) = n; MEM32(pairs + i * stride + 12) = random_u32(); }
            else MEM32(pairs + i * stride + 8) = random_u32();
            MEM32(bucket) = i;
        }
        MEM32(key) = (random_u32() >> 20) & 31u;
        MEM32(key + 4) = random_u32() >> 30;
        /* A key inside the pairs array (Find(&Pair.Key)) aliases the map's own storage. */
        const uint32_t key_address = count && test % 11 == 0 ? pairs + (test % count) * stride + 4 : key;
        reset(map, key_address, 0);
        core_mode = 1;
        if (name) compare(__wrap_sub_004B49A0, __real_sub_004B49A0, "name-map");
        else compare(__wrap_sub_00B65BD0, __real_sub_00B65BD0, "int-map");
    }
}

/* TMap<UObject*, V> (004B9D60 address of the value, 00F78830 the value) and the two-word-key map (00E2DC90). */
static void map2_cases(void)
{
    for (unsigned test = 0; test < 60000; ++test) {
        const unsigned kind = test % 3;       /* 0, 1: object keys; 2: two-word keys */
        const uint32_t stride = kind == 2 ? 16u : 12u, count = test % 41, hash_size = 1u << (test % 5);
        const uint32_t map = ARENA, pairs = ARENA + 0x1000, hash = ARENA + 0x8000, key = ARENA + 0x9000, objects = ARENA + 0x9100;
        for (unsigned i = 0; i < 16; ++i) MEM32(hash + i * 4) = UINT32_MAX;
        MEM32(map) = pairs;
        MEM32(map + 4) = test % 23 == 3 ? UINT32_MAX - (test & 7) : count;
        MEM32(map + 8) = 64;
        MEM32(map + 0xC) = test % 17 ? hash : 0;
        MEM32(map + 0x10) = hash_size;
        for (unsigned i = 0; i < 40; ++i) MEM32(objects + i * 16 + 4) = random_u32() >> 26;     /* each object's Index */
        for (unsigned i = 0; i < count; ++i) {
            const uint32_t which = (random_u32() >> 20) % 40u;
            const uint32_t k0 = kind == 2 ? (random_u32() >> 20) & 31u : objects + which * 16u;
            const uint32_t bucket_key = kind == 2 ? k0 : MEM32(objects + which * 16u + 4);
            const uint32_t bucket = hash + (bucket_key & (hash_size - 1u)) * 4u;
            MEM32(pairs + i * stride) = MEM32(bucket);
            MEM32(pairs + i * stride + 4) = k0;
            if (kind == 2) { MEM32(pairs + i * stride + 8) = random_u32() >> 30; MEM32(pairs + i * stride + 12) = random_u32(); }
            else MEM32(pairs + i * stride + 8) = random_u32();
            MEM32(bucket) = i;
        }
        if (kind == 2) { MEM32(key) = (random_u32() >> 20) & 31u; MEM32(key + 4) = random_u32() >> 30; }
        else MEM32(key) = test % 7 == 0 ? 0 : objects + ((random_u32() >> 20) % 40u) * 16u;
        const uint32_t key_address = count && test % 11 == 0 && kind != 2 ? pairs + (test % count) * stride + 4 : key;
        reset(map, key_address, 0);
        core_mode = 1;
        if (kind == 0) compare(__wrap_sub_004B9D60, __real_sub_004B9D60, "object-map-address");
        else if (kind == 1) compare(__wrap_sub_00F78830, __real_sub_00F78830, "object-map-value");
        else compare(__wrap_sub_00E2DC90, __real_sub_00E2DC90, "pair-map");
    }
}

/* The script VM's two variable opcodes: registers, the frame's Code pointer, the three GProp* globals, the copy. */
static void vm_cases(void)
{
    enum { FRAME = ARENA + 0xA000, CODE = ARENA + 0xB000, PROPERTY = ARENA + 0xC000, VTABLE = ARENA + 0xC800, SELF = ARENA + 0xD000,
           LOCALS = ARENA + 0xD800, RESULT = ARENA + 0xE000 };
    for (unsigned test = 0; test < 40000; ++test) {
        const uint32_t code = CODE + (test % 5);     /* bytecode is not aligned */
        memset(GPTR(ARENA + 0xA000), 0xC5, 0x5000);
        MEM32(FRAME + 0xC) = code;
        MEM32(FRAME + 0x10) = LOCALS;
        memcpy(GPTR(code), &(uint32_t){ PROPERTY }, 4);
        MEM32(PROPERTY) = VTABLE;
        MEM32(PROPERTY + 0x64) = (test * 37) % 0x400;
        MEM32(VTABLE + 0x14C) = VM_COPY_VA;
        const uint32_t result = test % 3 ? RESULT + (test % 8) * 4u : 0;
        MEM32(0x1DAC46C) = MEM32(0x1DAC474) = MEM32(0x1DAC478) = 0xDEAD0000u;
        reset(SELF, FRAME, result);
        core_mode = 1;
        void (*native)(void) = test & 1 ? __wrap_sub_00BC1310 : __wrap_sub_00BC12C0;
        void (*original)(void) = test & 1 ? __real_sub_00BC1310 : __real_sub_00BC12C0;
        void_regs before = {0}, a = {0}, b = {0};
        void_regs_save(&before);
        static uint8_t snap[0x5000], after[0x5000];
        memcpy(snap, GPTR(ARENA + 0xA000), sizeof snap);
        const uint32_t g0 = MEM32(0x1DAC46C), g1 = MEM32(0x1DAC474), g2 = MEM32(0x1DAC478);
        native(); void_regs_save(&a);
        const uint32_t na = MEM32(0x1DAC46C), nb = MEM32(0x1DAC474), nc = MEM32(0x1DAC478);
        memcpy(after, GPTR(ARENA + 0xA000), sizeof after);
        memcpy(GPTR(ARENA + 0xA000), snap, sizeof snap);
        MEM32(0x1DAC46C) = g0; MEM32(0x1DAC474) = g1; MEM32(0x1DAC478) = g2;
        void_regs_load(&before);
        original(); void_regs_save(&b);
        ++checks;
        const int same = a.eax == b.eax && a.esp == b.esp && a.ebx == b.ebx && a.esi == b.esi && a.edi == b.edi && a.ebp == b.ebp &&
            na == MEM32(0x1DAC46C) && nb == MEM32(0x1DAC474) && nc == MEM32(0x1DAC478) && !memcmp(after, GPTR(ARENA + 0xA000), sizeof after);
        if (!same && ++failures <= 8)
            fprintf(stderr, "vm op case %u differs: eax %08x/%08x esp %08x/%08x\n", test, a.eax, b.eax, a.esp, b.esp);
    }
}

static float random_coord(void)
{
    switch (random_u32() % 8) {
    case 0: return 0.0f;
    case 1: return -0.0f;
    case 2: return (float)(int32_t)(random_u32() % 9) - 4.0f;           /* exact edges */
    default: return (float)((int32_t)(random_u32() % 200001u) - 100000) * 0.0001f;
    }
}

static void store_float(uint32_t address, float value) { MEMF(address) = value; }

static void collision_cases(void)
{
    const uint32_t box = ARENA, check = ARENA + 0x200, time = ARENA + 0x400;
    store_float(0x1750298u, 0.1f);
    store_float(0x17343D8u, 1.0f);
    for (unsigned test = 0; test < 200000; ++test) {
        for (unsigned axis = 0; axis < 3; ++axis) {
            float lo = random_coord(), hi = random_coord();
            if (random_u32() % 8 && lo > hi) { float t = lo; lo = hi; hi = t; }
            store_float(box + axis * 4, lo);
            store_float(box + 0xC + axis * 4, hi);
            store_float(check + 0xA8 + axis * 4, random_coord() * 2.0f);
            const float d = random_u32() % 6 ? random_coord() * 3.0f : 0.0f;
            store_float(check + 0xC0 + axis * 4, d);
            store_float(check + 0xCC + axis * 4, d != 0.0f ? 1.0f / d : (random_u32() & 1 ? INFINITY : 0.0f));
        }
        if (test % 997 == 5) MEM32(check + 0xA8 + (test % 3) * 4) = 0x7FC00000u | (random_u32() & 0xFFFu);
        MEM32(time) = random_u32();
        const uint32_t out = test % 401 == 7 ? check + 0xC0 : time;   /* Time aliasing an input */
        reset(box, check, out);
        collision_mode = 1;
        const unsigned long long before = collision_fallbacks;
        compare(__wrap_sub_0116C250, __real_sub_0116C250, "collision");
        fallbacks_seen += (unsigned)(collision_fallbacks - before);
    }
}

/* MSVC 8 memcpy is memmove: the reference keeps that meaning. */
static void reference_copy(void)
{
    const uint32_t dst = ARG(0), src = ARG(1), count = ARG(2);
    memmove(GPTR(dst), GPTR(src), count);
    g_eax = dst; g_esp += 4;
}

static void reference_set(void)
{
    const uint32_t dst = ARG(0), value = ARG(1), count = ARG(2);
    for (uint32_t i = 0; i < count; ++i) MEM8(dst + i) = (uint8_t)value;
    g_eax = dst; g_esp += 4;
}

static void crt_cases(void)
{
    original_memcpy = original_memmove = reference_copy;
    original_memset = reference_set;
    crt_mode = 1;
    for (unsigned test = 0; test < 20000; ++test) {
        const uint32_t base = ARENA + 0x2000, count = test % 7 ? random_u32() % 700 : random_u32() % 70000;
        const uint32_t dst = base + random_u32() % 4096, src = base + (test % 3 ? random_u32() % 4096 : dst % 4096);
        for (uint32_t i = 0; i < 80000; i += 4) MEM32(base + i) = random_u32();
        reset(0, dst, src);
        MEM32(g_esp + 12) = count;
        compare(test & 1 ? crt_memcpy : crt_memmove, reference_copy, "copy");
        reset(0, dst, random_u32());
        MEM32(g_esp + 12) = count;
        compare(crt_memset, reference_set, "set");
    }
}

static void particle_cases(void)
{
    const uint32_t table = ARENA, outs = ARENA + 0x100;
    MEMF(0x173D9B8u) = 4294967296.0f;   /* the image's FADD constant */
    for (unsigned test = 0; test < 200000; ++test) {
        MEM8(table + 3) = (uint8_t)(random_u32() % 9);                 /* stride, 0 included */
        MEM32(table + 4) = ARENA + 0x1000 + (random_u32() & 0xFFC);       /* table data */
        MEM32(table + 8) = random_u32() % 300;                           /* Num */
        const unsigned kind = random_u32() % 10;
        float bias = (float)((int32_t)(random_u32() % 2001) - 1000) * 0.01f, scale = (float)(random_u32() % 1000) * 0.07f;
        float time = (float)((int32_t)(random_u32() % 40001) - 20000) * 0.001f;
        if (kind == 0) time = bias;                                      /* exact zero */
        if (kind == 1) scale = 3.0e38f;                                  /* overflow to +Inf */
        if (kind == 2) { uint32_t nan = 0x7FC00000u | (random_u32() & 0xFFFu); memcpy(&time, &nan, 4); }
        if (kind == 3) time = 1.0e10f;                                   /* index beyond INT32_MAX */
        if (kind == 4) scale = -scale;                                   /* negative t clamps to 0 */
        MEMF(table + 0x10) = scale; MEMF(table + 0x14) = bias;
        /* An output pointer into the table or into the argument slots runs the original. */
        const uint32_t out1 = test % 211 == 3 ? table + 4 : outs, out2 = test % 223 == 5 ? STACK - 0x40u + 16 : outs + 4;
        reset(table, 0, out1);
        MEMF(g_esp + 4) = time; MEM32(g_esp + 8) = out1; MEM32(g_esp + 12) = out2; MEM32(g_esp + 16) = outs + 8;
        particle_mode = 1;
        compare(__wrap_sub_00BF8DA0, __real_sub_00BF8DA0, "particle");
    }
}

/* Saved globals and flags are dead at a 0x17 boundary; their caller-local copies remain live. */
static void shader_live_state(void_regs *r)
{
    r->ebx = r->ebp = r->esi = r->edi = 0;
    r->flag_k = r->flag_a = r->flag_b = r->flag_cf = 0;
}

static void shader_compare(void (*native)(void), void (*original)(void), unsigned kind, int masked)
{
    void_regs before = {0}, a = {0}, b = {0}, entry = {0};
    void_regs_save(&before);
    const uint32_t stack_lo = before.esp - 128u, rhi = MEM32(0x01DEB3E8u);
    uint8_t stack_before[256], stack_after[256];
    uint32_t args[10];
    uint32_t caller[12] = {0}, native_locals[12] = {0}, original_locals[12] = {0};
    memcpy(caller, &before, 8u * sizeof(uint32_t));
    for (unsigned i = 0; i < 8; ++i) if (i != 4) caller[i] ^= 0xA5A50001u + i;
    caller[4] += 4u; /* RECOMP_CALL_P pushes the existing entry return slot. */
    caller[8] = FK_ADD; caller[9] = 0x1234u; caller[10] = 0x5678u; caller[11] = 0;
    static uint8_t after[WATCH];
    memcpy(snapshot, GPTR(ARENA), WATCH);
    memcpy(stack_before, GPTR(stack_lo), sizeof stack_before);
    shader_calls = shader_asserts = 0;
    if (masked) shader_masked_call(native, caller, native_locals); else native();
    void_regs_save(&a);
    const unsigned calls = shader_calls, asserts = shader_asserts;
    const uint32_t after_rhi = MEM32(0x01DEB3E8u);
    entry = shader_entry; memcpy(args, shader_stack, sizeof args);
    memcpy(after, GPTR(ARENA), WATCH);
    memcpy(stack_after, GPTR(stack_lo), sizeof stack_after);
    memcpy(GPTR(ARENA), snapshot, WATCH);
    memcpy(GPTR(stack_lo), stack_before, sizeof stack_before);
    MEM32(0x01DEB3E8u) = rhi;
    void_regs_load(&before);
    shader_calls = shader_asserts = 0;
    if (masked) shader_masked_call(original, caller, original_locals); else original();
    void_regs_save(&b);
    if (masked) {
        shader_live_state(&a); shader_live_state(&b);
        shader_live_state(&entry); shader_live_state(&shader_entry);
    }
    ++checks;
    const int same = !memcmp(&a, &b, sizeof a) && !memcmp(after, GPTR(ARENA), WATCH) &&
        !memcmp(stack_after, GPTR(stack_lo), sizeof stack_after) && after_rhi == MEM32(0x01DEB3E8u) &&
        calls == shader_calls && asserts == shader_asserts &&
        (!calls || (!memcmp(&entry, &shader_entry, sizeof entry) && !memcmp(args, shader_stack, sizeof args))) &&
        (!masked || (!memcmp(native_locals, original_locals, sizeof native_locals) &&
            native_locals[3] == caller[3] && native_locals[5] == caller[5] &&
            native_locals[6] == caller[6] && native_locals[7] == caller[7] &&
            !memcmp(native_locals + 8, caller + 8, 4u * sizeof(uint32_t))));
    if (!same && ++failures <= 8)
        fprintf(stderr, "shader %u masked %d case %u differs: state %d memory %d stack %d callback %d, eax %08x/%08x esp %08x/%08x\n",
            kind, masked, checks, memcmp(&a, &b, sizeof a), memcmp(after, GPTR(ARENA), WATCH),
            memcmp(stack_after, GPTR(stack_lo), sizeof stack_after), memcmp(&entry, &shader_entry, sizeof entry),
            a.eax, b.eax, a.esp, b.esp);
}

static void shader_cases(void)
{
    static void (*const native[])(void) = {__wrap_sub_00EC2580, __wrap_sub_00EC2610, __wrap_sub_00EC26A0, __wrap_sub_00EC2730};
    static void (*const original[])(void) = {__real_sub_00EC2580, __real_sub_00EC2610, __real_sub_00EC26A0, __real_sub_00EC2730};
    static void (*const masked[])(void) = {__masked_sub_00EC2580, __masked_sub_00EC2610, __masked_sub_00EC26A0, __masked_sub_00EC2730};
    static const uint32_t alignments[] = {0, 1, 2, 4, 8, 16, 32, 64, 3, 7, 15, 0x80000000u, UINT32_MAX};
    static const uint32_t indices[] = {0, 1, 2, 3, 4, 16, 256, 65535, 0x80000000u, UINT32_MAX};
    for (unsigned test = 0; test < 160000; ++test) {
        const unsigned kind = test & 3u;
        const uint32_t descriptor = test % 23u == 0 ? STACK - 0x40u - 12u + (test % 7u) : ARENA + 0xA000u;
        reset(random_u32(), random_u32(), descriptor);
        if (test % 503u == 0) {
            g_esp = 0x01DEB3E8u + 4u;   /* pushes overwrite GDynamicRHI: retain original ordering */
            MEM32(g_esp) = RECOMP_RETADDR;
            MEM32(g_esp + 4u) = random_u32(); MEM32(g_esp + 8u) = descriptor;
            g_ebx = SHADER_RHI;
        }
        MEM32(g_esp + 12u) = ARENA + 0xA100u;
        MEM32(g_esp + 16u) = test % 3u ? indices[(test / 4u) % (sizeof indices / sizeof indices[0])] : random_u32();
        MEM16(descriptor) = (uint16_t)random_u32();
        MEM16(descriptor + 2u) = (uint16_t)random_u32();
        MEM16(descriptor + 4u) = test % 5u ? (uint16_t)(test % 131u) : (uint16_t)random_u32();
        MEM32(0x01D1BC24u) = alignments[(test / 4u) % (sizeof alignments / sizeof alignments[0])];
        MEM32(0x01DEB3E8u) = test % 41u ? SHADER_RHI : 0;
        MEM32(SHADER_RHI) = SHADER_VTABLE;
        MEM32(SHADER_VTABLE + 0xD4u) = SHADER_VS_VA;
        MEM32(SHADER_VTABLE + 0xD8u) = SHADER_PS_VA;
        shader_callback_mode = (test / 7u) & 3u;
        core_mode = test % 89u == 0 ? 0 : test % 97u == 0 ? 2 : 1;
        shader_compare(native[kind], test < 80000 ? original[kind] : masked[kind], kind, test >= 80000);
    }
}

int main(void)
{
    assert(mmap((void *)VOID_GUEST_BASE, MEMORY, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0) == (void *)VOID_GUEST_BASE);
    map_cases(); map2_cases(); vm_cases(); collision_cases(); crt_cases(); particle_cases(); shader_cases();
    printf("%s %u contract cases, %u mismatches, %u collision fallbacks to the original\n",
           failures ? "FAIL" : "PASS", checks, failures, fallbacks_seen);
    return failures != 0;
}
