#include "host.h"
#include <assert.h>
#include <sys/mman.h>
#include "../android/host/render_draw_native.c"

recomp_ehframe_t *g_eh_frames;
uint32_t g_eh_obj, g_eh_info;
extern void original_iterator(void);
enum { STACK = 0x30008, DATA = 0x100000, DATA_SIZE = 0x20000,
       SELF = DATA, LISTS = DATA + 0x100, VIS = DATA + 0x200,
       VIS_WORDS = DATA + 0x300, BUCKETS = DATA + 0x400, STATS = DATA + 0x500,
       COUNTER = DATA + 0x600, DRAW_LIST = DATA + 0x1000 };
typedef struct { unsigned kind; void_regs regs; uint32_t args[4], dirty, iterator[7]; } event;
/* The instantiations of the traversal: their wrapper, and the template's two shape parameters. */
typedef struct { const char *name; void (*wrapper)(void); uint32_t list, stride; } variant_t;
static const variant_t variants[] = {
    { "00F058D0", __wrap_sub_00F058D0, 0x28, 160 }, { "00F05700", __wrap_sub_00F05700, 0x28, 160 },
    { "00F05AA0", __wrap_sub_00F05AA0, 0x30, 160 }, { "00EFE060", __wrap_sub_00EFE060, 0x18, 16 },
    { "00EFE230", __wrap_sub_00EFE230, 0x1C, 16 },
    { "00F05530", __wrap_sub_00F05530, 0x28, 16 },
};
static event events[4096], expected_events[4096];
static unsigned event_count, draw_count, case_number;
static uint32_t seed = 0x13FAB719;
static uint32_t random_u32(void) { seed = seed * 1664525u + 1013904223u; return seed; }
const char *void_data_dir(void) { return "/nonexistent"; }
void void_log(const char *fmt, ...) { (void)fmt; }
void void_dispatch_add(uint32_t address, recomp_func_t fn) { (void)address; (void)fn; }

static void record(unsigned kind, unsigned args)
{
    assert(event_count < 4096);
    event *e = &events[event_count++];
    memset(e, 0, sizeof *e);
    e->kind = kind;
    void_regs_save(&e->regs);
    for (unsigned i = 0; i < args; ++i) e->args[i] = ARG(i);
    if (kind == 3) {
        e->dirty = MEM32(ARG(3));
        memcpy(e->iterator, GPTR(STACK - 0x50 + 0x34), sizeof e->iterator);
    }
}

void sub_005DAF50(void)
{
    record(1, 1);
    original_iterator();
}

void sub_00B55380(void)
{
    record(4, 4);
    assert(ARG(0) == 0x1737A20 && ARG(1) == 0x1734248 && ARG(2) == 0x6CD && ARG(3) == 0x173421C);
    g_eax = 0x12345678;
    g_esp += 4;
}

static void stat_add(void)
{
    record(2, 2);
    const uint32_t old = MEM32(ARG(0));
    MEM32(ARG(0)) = old + ARG(1);
    g_eax = old;
    g_esp += 12;
}

recomp_func_t void_resolve(uint32_t address)
{
    assert(address == 0x7FF12340);
    return stat_add;
}

/* The draw of one element: every instantiation's callee is this stub. */
static void draw_policy(void)
{
    record(3, 4);
    assert(g_ecx == SELF && ARG(0) == 0x24681357);
    assert(ARG(1) >= DRAW_LIST && ARG(1) < DATA + DATA_SIZE);
    assert(ARG(2) >= DRAW_LIST && ARG(2) < DATA + DATA_SIZE);
    ++draw_count;
    MEM32(ARG(3)) += 3;
    MEM32(ARG(1) + 8) += draw_count;
    /* A draw policy can change future visibility and its referenced per-list state. */
    if (case_number % 7 == 0 && draw_count == 1) MEM32(VIS_WORDS + 4) ^= 0xAAAAAAAAu;
    g_eax = 0xABCD0000u + draw_count;
    g_ecx = 0xBCD00000u + draw_count;
    g_edx = 0xCD000000u + draw_count;
    g_xmm[0].u32[0] = 0xDE000000u + draw_count;
    g_flag_k = FK_CMP; g_flag_a = draw_count; g_flag_b = 123; g_flag_cf = draw_count < 123;
    g_esp += 20;
}
void sub_00F043E0(void) { draw_policy(); }
void sub_00F042C0(void) { draw_policy(); }
void sub_00F04350(void) { draw_policy(); }
void sub_00F04470(void) { draw_policy(); }
void sub_00EFB030(void) { draw_policy(); }
void sub_00EFB0C0(void) { draw_policy(); }

static void yield_probe(void) { record(5, 0); }

static void fixture(unsigned n, const variant_t *v)
{
    case_number = n;
    memset(GPTR(DATA), 0xA6, DATA_SIZE);
    memset(GPTR(STACK - 0x200), 0xB7, 0x300);
    MEM32(12) = 0;
    const int32_t list_count = n < 2 ? (int32_t)n - 1 : (int32_t)(n % 4);
    MEM32(SELF) = LISTS; MEM32(SELF + 4) = (uint32_t)list_count;
    MEM32(VIS) = VIS_WORDS;
    for (unsigned i = 0; i < 4; ++i) MEM32(VIS_WORDS + i * 4) = n % 5 ? random_u32() : 0;
    for (unsigned list = 0; list < 3; ++list) {
        const uint32_t base = DRAW_LIST + list * 0x6000;
        const uint32_t elements = base + 0x100, bits = base + 0x5800, material = base + 0x5900;
        MEM32(LISTS + list * 4) = base;
        MEM32(base + v->list) = elements;
        MEM32(base + v->list + 0x18) = bits;
        const uint32_t size = n % 10 == 0 ? 0 : (n * 7 + list * 11) % 97;
        MEM32(base + v->list + 0x1C) = size;
        MEM32(base + v->list + 0x20) = 128;
        for (unsigned word = 0; word < 4; ++word)
            MEM32(bits + word * 4) = n % 9 == 0 ? 0 : n % 11 == 0 ? UINT32_MAX : random_u32();
        MEM32(material + 0xB4) = 3 + list;
        for (unsigned e = 0; e < 97; ++e) {
            MEM32(elements + v->stride * e) = (e * 11 + list * 3) % 128;
            MEM32(elements + v->stride * e + 4) = material;
        }
    }
    for (unsigned b = 0; b < 16; ++b) MEM32(BUCKETS + 4 * b) = UINT32_MAX;
    MEM32(BUCKETS + 4 * (0xD8 & 15)) = n % 13 == 0 ? UINT32_MAX : 0;
    MEM32(STATS) = 1; MEM32(STATS + 4) = 0x123; MEM32(STATS + 8) = 0;
    MEM32(STATS + 12) = UINT32_MAX; MEM32(STATS + 16) = 0xD8;
    MEM32(STATS + 20) = n % 17 == 0 ? 0 : COUNTER;
    MEM32(COUNTER + 12) = 11;
    MEM32(0x1DCE1F4) = STATS; MEM32(0x1DCE1F8) = n % 19 == 0 ? 0 : 2;
    MEM32(0x1DCE200) = n % 23 == 0 ? 0 : BUCKETS; MEM32(0x1DCE204) = 16;
    MEM32(0x172912C) = 0x7FF12340;
    g_eax = random_u32(); g_ecx = SELF; g_edx = random_u32(); g_ebx = random_u32();
    g_esp = STACK; g_esi = random_u32(); g_edi = random_u32(); g_ebp = random_u32();
    g_flag_k = FK_TEST; g_flag_a = random_u32(); g_flag_b = random_u32(); g_flag_cf = 1;
    g_backedges = n % 3 ? 0 : RECOMP_YIELD_EVERY - 3;
    recomp_yield_hook = yield_probe;
    for (unsigned j = 0; j < 8; ++j) for (unsigned k = 0; k < 4; ++k) g_xmm[j].u32[k] = random_u32();
    MEM32(STACK) = RECOMP_RETADDR; ARG(0) = 0x24681357; ARG(1) = VIS;
    event_count = draw_count = 0;
}

int main(void)
{
    void *mapped = mmap((void *)VOID_GUEST_BASE, 0x2000000, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    assert(mapped == (void *)VOID_GUEST_BASE);
    static uint8_t data_before[DATA_SIZE], expected_data[DATA_SIZE], stack_before[0x300];
    unsigned total_draws = 0, total_yields = 0, total_errors = 0;
    for (unsigned variant = 0; variant < sizeof variants / sizeof *variants; ++variant) {
        const variant_t *v = &variants[variant];
        for (unsigned n = 0; n < 3000; ++n) {
            fixture(n, v);
            void_regs before = {0}, expected = {0}, actual = {0};
            void_regs_save(&before);
            memcpy(data_before, GPTR(DATA), DATA_SIZE);
            memcpy(stack_before, GPTR(STACK - 0x200), sizeof stack_before);
            render_draw_mode = 1; v->wrapper();
            void_regs_save(&expected);
            memcpy(expected_data, GPTR(DATA), DATA_SIZE);
            memcpy(expected_events, events, sizeof(event) * event_count);
            const unsigned expected_count = event_count;
            const uint32_t expected_null_stat = MEM32(12);
            total_draws += draw_count;
            for (unsigned i = 0; i < event_count; ++i) {
                total_yields += events[i].kind == 5;
                total_errors += events[i].kind == 4;
            }
            memcpy(GPTR(DATA), data_before, DATA_SIZE);
            memcpy(GPTR(STACK - 0x200), stack_before, sizeof stack_before);
            MEM32(12) = 0;
            void_regs_load(&before); event_count = draw_count = 0;
            render_draw_mode = 0; v->wrapper();
            void_regs_save(&actual);
            if (event_count != expected_count || memcmp(expected_events, events, sizeof(event) * event_count) ||
                memcmp(&expected, &actual, sizeof actual) || memcmp(expected_data, GPTR(DATA), DATA_SIZE) ||
                expected_null_stat != MEM32(12)) {
                fprintf(stderr, "%s case %u failed, events %u/%u\n", v->name, n, expected_count, event_count);
                for (unsigned i = 0; i < event_count && i < expected_count; ++i)
                    if (memcmp(&events[i], &expected_events[i], sizeof(event))) {
                        fprintf(stderr, "event %u kind %u/%u\n", i, expected_events[i].kind, events[i].kind);
                        for (unsigned j = 0; j < sizeof(event) / 4; ++j)
                            if (((uint32_t *)&events[i])[j] != ((uint32_t *)&expected_events[i])[j])
                                fprintf(stderr, "event byte %u: %08x/%08x\n", j * 4,
                                    ((uint32_t *)&expected_events[i])[j], ((uint32_t *)&events[i])[j]);
                        break;
                    }
                for (unsigned j = 0; j < sizeof actual / 4; ++j)
                    if (((uint32_t *)&expected)[j] != ((uint32_t *)&actual)[j])
                        fprintf(stderr, "state byte %u: %08x/%08x\n", j * 4, ((uint32_t *)&expected)[j], ((uint32_t *)&actual)[j]);
                return 1;
            }
        }
    }
    assert(total_draws && total_yields && total_errors);
    printf("PASS native draw-list: %zu instantiations x 3000 cases, %u draws, %u yields, %u assertions; callback state/order/args, mutations and outgoing state exact\n",
        sizeof variants / sizeof *variants, total_draws, total_yields, total_errors);
    munmap(mapped, 0x2000000);
    return 0;
}
