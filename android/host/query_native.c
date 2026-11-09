/* 01590CF0, the D3D9 RHI's wait for a query result (thiscall, `ret 0x10`):
 * Query, pData, Size, bWait.
 *
 * The original reads the clock first (an inlined appSeconds(): it adds one to the 64-bit
 * call count at 0x1DBDD50, calls QueryPerformanceCounter and scales by GSecondsPerCycle),
 * then calls Query->GetData(pData, Size, D3DGETDATA_FLUSH): S_OK returns TRUE; S_FALSE
 * without bWait returns FALSE; S_FALSE with bWait polls again, reading the clock each time,
 * until a 0.5 s deadline; D3DERR_DEVICELOST sets the RHI's device-lost flag; other errors
 * are reported. In a 32-bot WAR-Torlan match it runs ~1950 times per frame and the first
 * poll answers almost every time ([rt] queries per frame: 0-1 pending), so its clock read
 * (an ISB and a slow counter read, 0.9% of the game thread on 2026-11-08) is never used.
 *
 * Native: count the clock read as the original does, poll once, and finish the two
 * answers of the original's first poll that do not use the clock (S_OK -> TRUE, S_FALSE
 * without bWait -> FALSE) exactly as it does. Any other answer uncounts the read and runs
 * the original from its entry, which reads the clock and polls again: the same as an
 * original call made a moment later. Outputs as the original's: EAX, ESP + 20, EBX/ESI/
 * EDI/EBP preserved, the flags of its closing `test`/`xor`, ECX/EDX as GetData left them
 * (the original loads ECX/EDX for the same call and does not touch them afterwards), the
 * call count; the original's dead locals below ESP are not reproduced.
 *
 * query-wait-native: absent/1 = native, 0 = original; Exact mode keeps the original. */
#include "host.h"
#include "native_mode.inc"

extern void __real_sub_01590CF0(void);
void __wrap_sub_01590CF0(void);

#define SECONDS_CALLS 0x01DBDD50u   /* 64-bit count of the inlined appSeconds() reads */
#define QUERY_GETDATA 0x1Cu         /* IDirect3DQuery9::GetData vtable offset */
#define GETDATA_FLUSH 1u

static int query_wait_mode;
static unsigned long long query_wait_fast, query_wait_original;

void void_query_wait_init(void);
void void_query_wait_init(void)
{
    char path[1100];
    snprintf(path, sizeof path, "%s/query-wait-native", void_data_dir());
    FILE *file = fopen(path, "rb");
    const int value = file ? fgetc(file) : '1';
    if (file) fclose(file);
    query_wait_mode = void_native_mode(value == '0' ? 0 : 1);
    void_log("[native-rhi] query wait 01590CF0: mode %d", query_wait_mode);
    void_dispatch_add(0x01590CF0u, __wrap_sub_01590CF0);
}

static inline void seconds_count(int64_t delta)
{
    const uint64_t count = ((uint64_t)MEM32(SECONDS_CALLS + 4) << 32 | MEM32(SECONDS_CALLS)) + (uint64_t)delta;
    MEM32(SECONDS_CALLS) = (uint32_t)count;
    MEM32(SECONDS_CALLS + 4) = (uint32_t)(count >> 32);
}

void __wrap_sub_01590CF0(void)
{
    const uint32_t sp = g_esp, query = MEM32(sp + 4);
    if (!query_wait_mode || !query) { __real_sub_01590CF0(); return; }
    seconds_count(1);
    uint32_t s = sp;
    PUSH32(s, GETDATA_FLUSH);
    PUSH32(s, MEM32(sp + 12));      /* Size */
    PUSH32(s, MEM32(sp + 8));       /* pData */
    PUSH32(s, query);
    PUSH32(s, RECOMP_RETADDR);
    const uint32_t vtable = MEM32(query), method = MEM32(vtable + QUERY_GETDATA);
    g_ecx = vtable;
    g_edx = method;
    g_esp = s;
    void_resolve(method)();
    if (g_esp != sp) void_fatal("query wait: GetData returned with ESP %08X, expected %08X", g_esp, sp);
    const uint32_t result = g_eax;
    if (result == 0u || (result == 1u && !MEM32(sp + 16))) {
        ++query_wait_fast;
        g_eax = result == 0u;
        g_flag_k = FK_TEST;
        g_flag_a = 0;
        g_flag_b = 0;
        g_flag_cf = 0;
        g_esp = sp + 20u;
        return;
    }
    ++query_wait_original;
    seconds_count(-1);              /* the original counts its own first read */
    __real_sub_01590CF0();
}
