/* Native UE3 container lookups, script variable opcodes and shader parameter bridges.
 *
 *   004B49A0  TMap<FName, V>::Find  16-byte pairs {HashNext, Name.Index, Name.Number, Value}
 *   00B65BD0  TMap<int, V>::Find    12-byte pairs {HashNext, Key, Value} (frame statistics, actors)
 *   004B9D60  TMap<UObject*, V>::Find, the value's address; 00F78830 the same map, the value: the hash is the
 *             object's Index at key + 4, 0 for a null key
 *   00E2DC90  TMap of 8-byte keys::Find, the value's address, 16-byte pairs {HashNext, Key0, Key1, Value}
 *
 * Both are thiscall with one argument (a pointer to the key) and `ret 4`. The map is
 * {Pairs.Data, Pairs.Num, Pairs.Max, Hash, HashSize}; Find walks the bucket chain from
 * Hash[Key.Index & (HashSize - 1)] and returns the value, or 0 for an empty map or a miss.
 *
 * Outputs are the call contract proven for every caller by tools/volatile_liveness.py
 * (build/call-protocol/volatile.json): EAX, ESP (+8) and the preserved EBX/ESI/EDI/EBP. No caller
 * reads ECX, EDX or the flags after these calls, so the original's scratch values there are not
 * reproduced, and the chain walk has no cooperative-yield counter: it never waits for another thread.
 * The key is read once; the lookup writes no guest memory except the caller-dead stack below ESP.
 *
 * core-native: absent/1 = native, 0 = lifted, 2 = run both and compare the contract (EAX, ESP,
 * EBX/ESI/EDI/EBP) on every call; lookups have no side effects, so running both is safe. */
#include "host.h"
#include "native_mode.inc"

extern void __real_sub_004B49A0(void);
extern void __real_sub_00B65BD0(void);
extern void __real_sub_004B9D60(void);
extern void __real_sub_00F78830(void);
extern void __real_sub_00E2DC90(void);
extern void __real_sub_00BC1310(void);
extern void __real_sub_00BC12C0(void);
extern void __real_sub_00EC2580(void), __real_sub_00EC2610(void), __real_sub_00EC26A0(void), __real_sub_00EC2730(void);

enum { CORE_NAME, CORE_INT, CORE_OBJECT_ADDRESS, CORE_OBJECT_VALUE, CORE_PAIR_ADDRESS, CORE_KINDS };
static const char *const core_kind_names[CORE_KINDS] = { "name map", "int map", "object map (address)", "object map (value)", "pair map" };
static int core_mode = -1;
static unsigned long long core_checked[CORE_KINDS], core_mismatches[CORE_KINDS];

static __attribute__((cold, noinline)) int core_mode_load(void)
{
    char path[1100];
    snprintf(path, sizeof path, "%s/core-native", void_data_dir());
    FILE *file = fopen(path, "rb");
    int mode = file ? fgetc(file) : '1';
    if (file) fclose(file);
    core_mode = void_native_mode(mode == '0' ? 0 : mode == '2' ? 2 : 1);
    void_log("[native-core] TMap finds, script variable opcodes and shader parameter bridges: mode %d", core_mode);
    return core_mode;
}

static inline int core_native_mode(void)
{
    return __builtin_expect(core_mode >= 0, 1) ? core_mode : core_mode_load();
}

static inline uint32_t name_map_find(uint32_t map, uint32_t key)
{
    const uint32_t hash = MEM32(map + 0xC);
    if (!hash || (int32_t)MEM32(map + 4) <= 0) return 0;
    const uint32_t index = MEM32(key), number = MEM32(key + 4);
    uint32_t id = MEM32(hash + ((MEM32(map + 0x10) - 1u) & index) * 4u);
    const uint32_t pairs = MEM32(map);
    while (id != UINT32_MAX) {
        const uint32_t pair = pairs + (id << 4);
        if (MEM32(pair + 4) == index && MEM32(pair + 8) == number) return MEM32(pair + 0xC);
        id = MEM32(pair);
    }
    return 0;
}

static inline uint32_t int_map_find(uint32_t map, uint32_t key)
{
    const uint32_t hash = MEM32(map + 0xC);
    if (!hash || (int32_t)MEM32(map + 4) <= 0) return 0;
    const uint32_t value = MEM32(key);
    uint32_t id = MEM32(hash + ((MEM32(map + 0x10) - 1u) & value) * 4u);
    const uint32_t pairs = MEM32(map);
    while (id != UINT32_MAX) {
        const uint32_t pair = pairs + id * 12u;
        if (MEM32(pair + 4) == value) return MEM32(pair + 8);
        id = MEM32(pair);
    }
    return 0;
}

/* 004B9D60 / 00F78830: the pair of an object key (a null key hashes as 0 and is searched like any other). */
static inline uint32_t object_map_pair(uint32_t map, uint32_t key)
{
    const uint32_t hash = MEM32(map + 0xC);
    if (!hash || (int32_t)MEM32(map + 4) <= 0) return 0;
    const uint32_t object = MEM32(key);
    const uint32_t hashed = object ? MEM32(object + 4) : 0;
    uint32_t id = MEM32(hash + ((MEM32(map + 0x10) - 1u) & hashed) * 4u);
    const uint32_t pairs = MEM32(map);
    while (id != UINT32_MAX) {
        const uint32_t pair = pairs + id * 12u;
        if (MEM32(pair + 4) == object) return pair;
        id = MEM32(pair);
    }
    return 0;
}

/* 00E2DC90: two-word keys, the first word hashes; returns the pair. */
static inline uint32_t pair_map_find(uint32_t map, uint32_t key)
{
    const uint32_t hash = MEM32(map + 0xC);
    if (!hash || (int32_t)MEM32(map + 4) <= 0) return 0;
    const uint32_t k0 = MEM32(key), k1 = MEM32(key + 4);
    uint32_t id = MEM32(hash + ((MEM32(map + 0x10) - 1u) & k0) * 4u);
    const uint32_t pairs = MEM32(map);
    while (id != UINT32_MAX) {
        const uint32_t pair = pairs + id * 16u;
        if (MEM32(pair + 4) == k0 && MEM32(pair + 8) == k1) return pair;
        id = MEM32(pair);
    }
    return 0;
}

static __attribute__((cold, noinline)) void core_compare(unsigned kind, uint32_t native, void (*original)(void))
{
    const uint32_t saved_esp = g_esp, saved_ebx = g_ebx, saved_esi = g_esi, saved_edi = g_edi, saved_ebp = g_ebp;
    original();
    const int same = g_eax == native && g_esp == saved_esp + 8u && g_ebx == saved_ebx && g_esi == saved_esi &&
        g_edi == saved_edi && g_ebp == saved_ebp;
    ++core_checked[kind];
    if (!same && core_mismatches[kind]++ < 16)
        void_log("[native-core] %s mismatch: native %08X lifted %08X esp %+d", core_kind_names[kind],
                 native, g_eax, (int)(g_esp - saved_esp));
    if ((core_checked[kind] & 0xFFFFFu) == 0)
        void_log("[native-core] %s checked %llu calls, %llu different", core_kind_names[kind],
                 core_checked[kind], core_mismatches[kind]);
}

void __wrap_sub_004B49A0(void)
{
    const int mode = core_native_mode();
    if (mode == 0) { __real_sub_004B49A0(); return; }
    const uint32_t result = name_map_find(g_ecx, ARG(0));
    if (mode == 2) { core_compare(CORE_NAME, result, __real_sub_004B49A0); return; }
    g_eax = result;
    g_esp += 8;
}

void __wrap_sub_00B65BD0(void)
{
    const int mode = core_native_mode();
    if (mode == 0) { __real_sub_00B65BD0(); return; }
    const uint32_t result = int_map_find(g_ecx, ARG(0));
    if (mode == 2) { core_compare(CORE_INT, result, __real_sub_00B65BD0); return; }
    g_eax = result;
    g_esp += 8;
}

#define CORE_FIND_KERNEL(address, kind, expression) \
void __wrap_sub_##address(void) { \
    const int mode = core_native_mode(); \
    if (mode == 0) { __real_sub_##address(); return; } \
    const uint32_t result = (expression); \
    if (mode == 2) { core_compare(kind, result, __real_sub_##address); return; } \
    g_eax = result; \
    g_esp += 8; \
}

static inline uint32_t object_map_address(uint32_t map, uint32_t key)
{
    const uint32_t pair = object_map_pair(map, key);
    return pair ? pair + 8u : 0;
}
static inline uint32_t object_map_value(uint32_t map, uint32_t key)
{
    const uint32_t pair = object_map_pair(map, key);
    return pair ? MEM32(pair + 8) : 0;
}
static inline uint32_t pair_map_address(uint32_t map, uint32_t key)
{
    const uint32_t pair = pair_map_find(map, key);
    return pair ? pair + 12u : 0;
}

CORE_FIND_KERNEL(004B9D60, CORE_OBJECT_ADDRESS, object_map_address(g_ecx, ARG(0)))
CORE_FIND_KERNEL(00F78830, CORE_OBJECT_VALUE, object_map_value(g_ecx, ARG(0)))
CORE_FIND_KERNEL(00E2DC90, CORE_PAIR_ADDRESS, pair_map_address(g_ecx, ARG(0)))

/* The script VM's two variable opcodes (UObject::execInstanceVariable 00BC1310, execLocalVariable 00BC12C0;
 * indirect calls through GNatives, thiscall with (FFrame& Stack, RESULT_DECL), ret 8): the property read from the
 * bytecode at Stack.Code (+0xC), GProperty 01DAC46C, the variable's address (the property's Offset at +0x64 added to
 * the object, or to Stack.Locals at +0x10), GPropAddr 01DAC474, GPropObject 01DAC478 (the object, 0 for a local);
 * with a Result, the value through UProperty::CopyCompleteValue (vtable + 0x14C, thiscall, ret 0x14). Without a Result
 * EAX is what the original leaves there. Mode 2 of core-native keeps the original: both would run the copy. */
#define VM_GPROPERTY 0x01DAC46Cu
#define VM_GPROPADDR 0x01DAC474u
#define VM_GPROPOBJECT 0x01DAC478u

static inline void vm_variable_finish(uint32_t sp, uint32_t property, uint32_t result, uint32_t address, uint32_t bare_eax)
{
    if (result) {
        PUSH32(g_esp, 0u);
        PUSH32(g_esp, 0u);
        PUSH32(g_esp, 0u);
        PUSH32(g_esp, address);
        PUSH32(g_esp, result);
        PUSH32(g_esp, RECOMP_RETADDR);
        g_ecx = property;
        void_resolve(MEM32(MEM32(property) + 0x14Cu))();
    } else {
        g_eax = bare_eax;
    }
    g_esp = sp + 12u;
}

void __wrap_sub_00BC1310(void)
{
    if (core_native_mode() != 1) { __real_sub_00BC1310(); return; }
    const uint32_t sp = g_esp, self = g_ecx, stack = MEM32(sp + 4), result = MEM32(sp + 8);
    const uint32_t code = MEM32(stack + 0xC), property = MEM32(code);
    MEM32(stack + 0xC) = code + 4u;
    MEM32(VM_GPROPERTY) = property;
    const uint32_t address = MEM32(property + 0x64) + self;
    MEM32(VM_GPROPOBJECT) = self;
    MEM32(VM_GPROPADDR) = address;
    vm_variable_finish(sp, property, result, address, property);
}

void __wrap_sub_00BC12C0(void)
{
    if (core_native_mode() != 1) { __real_sub_00BC12C0(); return; }
    const uint32_t sp = g_esp, stack = MEM32(sp + 4), result = MEM32(sp + 8);
    const uint32_t code = MEM32(stack + 0xC), property = MEM32(code);
    MEM32(stack + 0xC) = code + 4u;
    MEM32(VM_GPROPERTY) = property;
    const uint32_t address = MEM32(property + 0x64) + MEM32(stack + 0x10);
    MEM32(VM_GPROPADDR) = address;
    MEM32(VM_GPROPOBJECT) = 0;
    vm_variable_finish(sp, property, result, address, address);
}

/* SetShaderParameter: cdecl (shader, {uint16 buffer, base, bytes}, data, array index).
 * Keep the full register/flag and callback contract, including saved stack slots a callback can modify.
 * The null-RHI assertion and comparison mode run the original once because their callbacks have side effects. */
static void shader_parameter(uint32_t width, uint32_t method, void_fn original)
{
    if (core_native_mode() != 1 || !MEM32(0x01DEB3E8u) || (uint32_t)(g_esp - 0x01DEB3E8u) < 16u) {
        original();
        return;
    }
    const uint32_t sp = g_esp, alignment = MEM32(0x01D1BC24u), descriptor = MEM32(sp + 8u);
    const uint32_t offset = ((alignment + width - 1u) & ~(alignment - 1u)) * MEM32(sp + 16u);
    const uint32_t bytes = MEM16(descriptor + 4u);
    if (bytes <= offset) {
        g_eax = offset;
        g_ecx = bytes;
        g_edx = descriptor;
        g_flag_k = FK_CMP;
        g_flag_a = bytes;
        g_flag_b = offset;
        g_flag_cf = bytes < offset;
        g_esp = sp + 4u;
        return;
    }
    PUSH32(g_esp, g_ebx);
    PUSH32(g_esp, g_esi);
    PUSH32(g_esp, g_edi);
    const uint32_t count = bytes - offset < width ? bytes - offset : width;
    const uint32_t base = (uint32_t)MEM16(descriptor + 2u) + offset;
    const uint32_t rhi = MEM32(0x01DEB3E8u), buffer = MEM16(descriptor);
    g_ebx = count;
    g_esi = base;
    g_edi = buffer;
    g_ecx = rhi;
    g_eax = MEM32(MEM32(rhi) + method);
    g_edx = MEM32(sp + 12u);
    g_flag_k = FK_TEST;
    g_flag_a = g_flag_b = rhi;
    g_flag_cf = 0;
    PUSH32(g_esp, g_edx);
    g_edx = MEM32(sp + 4u);
    PUSH32(g_esp, count);
    PUSH32(g_esp, base);
    PUSH32(g_esp, buffer);
    PUSH32(g_esp, g_edx);
    static recomp_icache_t sites[2];
    recomp_icache_t *site = &sites[method == 0xD4u];
    const recomp_func_t target = __builtin_expect(site->key == ~g_eax, 1) ? site->fn : void_resolve_site(site, g_eax);
    PUSH32(g_esp, RECOMP_RETADDR);
    target();
    g_edi = POP32_VAL(g_esp);
    g_esi = POP32_VAL(g_esp);
    g_ebx = POP32_VAL(g_esp);
    g_esp += 4u;
}

void __wrap_sub_00EC2580(void) { shader_parameter(4u, 0xD8u, __real_sub_00EC2580); }
void __wrap_sub_00EC2610(void) { shader_parameter(16u, 0xD4u, __real_sub_00EC2610); }
void __wrap_sub_00EC26A0(void) { shader_parameter(16u, 0xD8u, __real_sub_00EC26A0); }
void __wrap_sub_00EC2730(void) { shader_parameter(64u, 0xD8u, __real_sub_00EC2730); }

void void_core_native_init(void)
{
    void_dispatch_add(0x004B49A0u, __wrap_sub_004B49A0);
    void_dispatch_add(0x00B65BD0u, __wrap_sub_00B65BD0);
    void_dispatch_add(0x004B9D60u, __wrap_sub_004B9D60);
    void_dispatch_add(0x00F78830u, __wrap_sub_00F78830);
    void_dispatch_add(0x00E2DC90u, __wrap_sub_00E2DC90);
    void_dispatch_add(0x00BC1310u, __wrap_sub_00BC1310);
    void_dispatch_add(0x00BC12C0u, __wrap_sub_00BC12C0);
    void_dispatch_add(0x00EC2580u, __wrap_sub_00EC2580);
    void_dispatch_add(0x00EC2610u, __wrap_sub_00EC2610);
    void_dispatch_add(0x00EC26A0u, __wrap_sub_00EC26A0);
    void_dispatch_add(0x00EC2730u, __wrap_sub_00EC2730);
}
