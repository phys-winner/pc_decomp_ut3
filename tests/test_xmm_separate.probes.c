/* Compiled twice as generated code, against pinned baseline/candidate headers. */
#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
extern void fixture_native_callback(void);
extern void fixture_xmm_check(unsigned phase);

void fixture_xmm_probe(void)
{
    uint32_t _cf = 0, _flag_k = FK_TEST, _flag_a = 0, _flag_b = 0;
    uint32_t source = ecx, destination = edx, output = ebx;
    RECOMP_REGS_LOCALS
    RECOMP_ENTER(0x02000100u);
    _xmm[0] = xmm_ld(source + 1);
    MEM32(source + 5) = 0x19283746u;
    _xmm[1] = xmm_ld(source + 3);
    xmm_st(destination + 2, _xmm[0]);
    _xmm[2] = xmm_ld(source + 5);
    MEM8(destination + 7) ^= 0xB3u;
    _xmm[3] = xmm_ld(source + 7);
    xmm_st(destination + 11, _xmm[1]);
    _xmm[4] = xmm_ld(source + 9);
    MEM16(source + 15) = 0xD25Fu;
    _xmm[5] = xmm_ld(source + 11);
    xmm_st(destination + 1, _xmm[2]);
    _xmm[6] = xmm_ld(source + 13);
    _xmm[7] = xmm_ld(source + 15);
    fixture_xmm_check(0);
    RECOMP_CALL(fixture_native_callback);
    RECOMP_FLAGS_IN();
    fixture_xmm_check(1);
    g_backedges = RECOMP_YIELD_EVERY - 1;
    RECOMP_BACKEDGE();
    fixture_xmm_check(2);
    /* Unaligned output, all eight registers, after callback and real yield. */
    for (unsigned i = 0; i < 8; ++i) xmm_st(output + 1 + 16 * i, _xmm[i]);
    RECOMP_FLAGS_OUT();
    esp += 4;
    RECOMP_REGS_OUT();
}

void fixture_xmm_guest_callback(void)
{
    uint32_t _cf = 0, _flag_k = FK_TEST, _flag_a = 0, _flag_b = 0;
    RECOMP_REGS_LOCALS
    RECOMP_ENTER(0x02000120u);
    fixture_xmm_check(0);
    for (unsigned i = 0; i < 8; ++i)
        for (unsigned lane = 0; lane < 4; ++lane) _xmm[i].u32[lane] ^= 0xA5A5A5A5u;
    eax = 0x33445566u;
    RECOMP_FLAGS_OUT();
    esp += 12; /* Two arguments and return marker. */
    RECOMP_REGS_OUT();
}

/* IR must retain volatile polling; this fixture never executes the spin. */
uint32_t fixture_xmm_volatile_read(uint32_t address)
{
    uint32_t first = MEM32(address), second = MEM32(address);
    return first ^ second;
}
