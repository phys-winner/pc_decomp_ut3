/* Native render-history lookup: both original entries use the same pointer-key hash table. */
#include "host.h"
#include "native_mode.inc"

extern void __real_sub_01178E50(void);
extern void __real_sub_01178EA0(void);

static int render_hash_mode = -1;
static unsigned long long render_hash_checked, render_hash_mismatches;

static __attribute__((noinline, cold)) int render_hash_mode_load(void)
{
    char path[1100];
    snprintf(path, sizeof path, "%s/render-hash-native", void_data_dir());
    FILE *file = fopen(path, "rb");
    int value = file ? fgetc(file) : '1';
    if (file) fclose(file);
    render_hash_mode = void_native_mode(value == '0' ? 0 : value == '2' ? 2 : 1);
    void_log("[render-hash] native lookup: mode %d (0 lifted, 1 native, 2 compare)", render_hash_mode);
    return render_hash_mode;
}

struct render_hash_result { uint32_t node, mask, hash_tail; };

/* sub_00E57210(key, 0): unsigned Jenkins mixing, retaining its EDX result for the guest ABI. */
static uint32_t render_pointer_hash(uint32_t key, uint32_t *tail)
{
    uint32_t a = key;
    uint32_t b = (0x9E3779B9u - a) ^ (a << 8);
    uint32_t c = (0u - b - a) ^ (b >> 13);
    a = (a - b - c) ^ (c >> 12);
    b = (b - a - c) ^ (a << 16);
    c = (c - b - a) ^ (b >> 5);
    a = (a - b - c) ^ (c >> 3);
    b = (b - a - c) ^ (a << 10);
    *tail = c;
    return (c - b - a) ^ (b >> 15);
}

static struct render_hash_result render_hash_find(uint32_t table, uint32_t key, uint32_t incoming_edx, int yield)
{
    struct render_hash_result result = {0, table, incoming_edx};
    const uint32_t buckets = MEM32(table + 8);
    if (buckets) {
        const uint32_t hash = render_pointer_hash(key, &result.hash_tail);
        result.mask = MEM32(table + 12) - 1u;
        result.node = MEM32(buckets + (hash & result.mask) * 4u);
        while (result.node && MEM32(result.node) != key) {
            result.node = MEM32(result.node + 24);
            /* Comparison mode leaves scheduling to the original lookup that follows. */
            if (yield) RECOMP_BACKEDGE();
        }
    }
    return result;
}

static void render_hash_lookup(void_fn original)
{
    const int mode = __builtin_expect(render_hash_mode >= 0, 1) ? render_hash_mode : render_hash_mode_load();
    if (!mode) { original(); return; }
    const uint32_t key = ARG(0), stack = g_esp;
    const struct render_hash_result result = render_hash_find(g_ecx, key, g_edx, mode == 1);
    const uint32_t flag_kind = result.node ? FK_CMP : FK_TEST;
    const uint32_t flag_value = result.node ? key : 0;
    if (mode == 2) {
        const uint32_t saved_ebx = g_ebx, saved_esi = g_esi, saved_edi = g_edi, saved_ebp = g_ebp;
        original();
        const int same = g_eax == result.node && g_ecx == result.mask && g_edx == result.hash_tail &&
            g_esp == stack + 8u && g_ebx == saved_ebx && g_esi == saved_esi && g_edi == saved_edi && g_ebp == saved_ebp &&
            g_flag_k == flag_kind && g_flag_a == flag_value && g_flag_b == flag_value && !g_flag_cf;
        ++render_hash_checked;
        if (!same && render_hash_mismatches++ < 16)
            void_log("[render-hash] mismatch key %08X: native %08X/%08X/%08X, lifted %08X/%08X/%08X",
                     key, result.node, result.mask, result.hash_tail, g_eax, g_ecx, g_edx);
        if ((render_hash_checked & 0xFFFFu) == 0)
            void_log("[render-hash] checked %llu calls, %llu mismatches", render_hash_checked, render_hash_mismatches);
        return;
    }
    g_eax = result.node;
    g_ecx = result.mask;
    g_edx = result.hash_tail;
    g_flag_k = flag_kind;
    g_flag_a = g_flag_b = flag_value;
    g_flag_cf = 0;
    g_esp = stack + 8u;
}

void __wrap_sub_01178E50(void) { render_hash_lookup(__real_sub_01178E50); }
void __wrap_sub_01178EA0(void) { render_hash_lookup(__real_sub_01178EA0); }

void void_render_hash_init(void)
{
    void_dispatch_add(0x01178E50u, __wrap_sub_01178E50);
    void_dispatch_add(0x01178EA0u, __wrap_sub_01178EA0);
}
