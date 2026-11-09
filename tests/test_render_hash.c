/* Compare the native lookup with its three current generated functions using the production runtime. */
#include "../android/host/render_hash_native.c"
#include <assert.h>
#include <sys/mman.h>

void sub_00E57210(void);
static unsigned dispatch_entries;
static unsigned yields;
static void test_yield(void) { ++yields; }
const char *void_data_dir(void) { return "/unused"; }
void void_log(const char *format, ...) { (void)format; }
void void_dispatch_add(uint32_t address, void_fn function)
{
    assert((address == 0x01178E50u && function == __wrap_sub_01178E50) ||
           (address == 0x01178EA0u && function == __wrap_sub_01178EA0));
    ++dispatch_entries;
}

static uint32_t seed = 0x31415926u;
static uint32_t next(void) { seed ^= seed << 13; seed ^= seed >> 17; return seed ^= seed << 5; }

static void registers(uint32_t key)
{
    g_eax = next(); g_ecx = 0x1000; g_edx = next(); g_ebx = next();
    g_esi = next(); g_edi = next(); g_ebp = next(); g_esp = 0x3F000;
    g_flag_k = FK_TEST; g_flag_a = next(); g_flag_b = next(); g_flag_cf = next() & 1;
    g_backedges = RECOMP_YIELD_EVERY - 1;
    MEM32(g_esp) = RECOMP_RETADDR; MEM32(g_esp + 4) = key;
}

static void snapshot(uint32_t out[13])
{
    const uint32_t values[] = {g_eax,g_ecx,g_edx,g_ebx,g_esi,g_edi,g_ebp,g_esp,
                              g_flag_k,g_flag_a,g_flag_b,g_flag_cf,g_backedges};
    memcpy(out, values, sizeof values);
}

static void restore(const uint32_t in[13])
{
    g_eax=in[0]; g_ecx=in[1]; g_edx=in[2]; g_ebx=in[3];
    g_esi=in[4]; g_edi=in[5]; g_ebp=in[6]; g_esp=in[7];
    g_flag_k=in[8]; g_flag_a=in[9]; g_flag_b=in[10]; g_flag_cf=in[11];
    g_backedges=in[12];
}

int main(void)
{
    assert(mmap((void *)VOID_GUEST_BASE, 0x40000, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0) == (void *)VOID_GUEST_BASE);
    void_render_hash_init(); assert(dispatch_entries == 2);
    recomp_yield_hook = test_yield;
    for (unsigned i = 0; i < 1000000; ++i) {
        const uint32_t key = i < 33 ? (i == 32 ? UINT32_MAX : 1u << i) : next();
        uint32_t tail;
        const uint32_t hash = render_pointer_hash(key, &tail);
        registers(key); MEM32(g_esp + 8) = 0; sub_00E57210();
        assert(g_eax == hash && g_edx == tail && g_esp == 0x3F004);
    }
    static uint8_t before[0x30000];
    unsigned hits = 0, misses = 0;
    for (unsigned i = 0; i < 20000; ++i) {
        memset(GPTR(0), 0xA7, sizeof before);
        const uint32_t size = i % 7 == 0 ? 3 : 1u << (i % 10);
        const unsigned count = i % 129;
        uint32_t keys[128];
        memset(GPTR(0x2000), 0, 1024 * 4);
        MEM32(0x1008) = i % 11 ? 0x2000 : 0;
        MEM32(0x100C) = size;
        for (unsigned j = 0; j < count; ++j) {
            uint32_t tail;
            keys[j] = i % 5 == 0 ? j % 3 : next();
            const uint32_t bucket = 0x2000 + (render_pointer_hash(keys[j], &tail) & (size - 1)) * 4;
            const uint32_t node = 0x4000 + j * 28;
            MEM32(node) = keys[j]; MEM32(node + 24) = MEM32(bucket); MEM32(bucket) = node;
        }
        const uint32_t key = count && i % 3 ? keys[next() % count] : next();
        registers(key);
        uint32_t initial[13], expected[13], actual[13];
        snapshot(initial); memcpy(before, GPTR(0), sizeof before);
        void_fn original = i & 1 ? __real_sub_01178E50 : __real_sub_01178EA0;
        void_fn wrapper = i & 1 ? __wrap_sub_01178E50 : __wrap_sub_01178EA0;
        yields = 0; original(); snapshot(expected);
        const unsigned expected_yields = yields;
        if (g_eax) ++hits; else ++misses;
        assert(!memcmp(before, GPTR(0), sizeof before));
        for (int mode = 0; mode <= 2; ++mode) {
            restore(initial); render_hash_mode = mode; yields = 0; wrapper(); snapshot(actual);
            if (memcmp(expected, actual, sizeof actual)) {
                fprintf(stderr, "lookup mismatch case %u mode %d\n", i, mode);
                for (int k = 0; k < 13; ++k) fprintf(stderr, "%d %08X %08X\n", k, expected[k], actual[k]);
                return 1;
            }
            assert(!memcmp(before, GPTR(0), sizeof before));
            assert(yields == expected_yields);
        }
    }
    assert(hits && misses && render_hash_checked == 20000 && !render_hash_mismatches);
    printf("PASS render hash: 1000000 hashes; 20000 lookups (%u hits, %u misses), both entries, modes 0/1/2, "
           "registers/flags/stack cleanup, scheduling and unchanged table memory\n", hits, misses);
    return 0;
}
