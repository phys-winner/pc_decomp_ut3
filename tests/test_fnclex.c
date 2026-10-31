/* Link against the production host to check every saved machine field. */
#include "host.h"
#include <assert.h>
#include <unistd.h>

void void_fpu_clear_exceptions(uint32_t address);
void void_process_exit(uint32_t code, int detach) { (void)detach; _exit((int)code); }

int main(void)
{
    void_regs before = {0}, after = {0};
    before.eax = 0x12345678; before.ecx = 0x23456789; before.edx = 0x3456789A;
    before.ebx = 0x456789AB; before.esp = 0x56789ABC; before.ebp = 0x6789ABCD;
    before.esi = 0x789ABCDE; before.edi = 0x89ABCDEF;
    before.fs = 0x12345000; before.cur = 0x10019360;
    before.flag_k = 3; before.flag_a = 0x81234567; before.flag_b = 0x76543210; before.flag_cf = 1;
    before.fp_top = 5; before.fpu_cw = 0x0B7F;
    before.fp_round_mask = 0xFEDCBA9876543210ull; before.fp_store_mask = 0x0123456789ABCDEFull;
    before.mxcsr = 0x7FFF; before.backedges = 4321;
    before.eh_frames = (void *)(uintptr_t)0x123400;
    before.eh_obj = 0xAABBCCDD; before.eh_info = 0xBBCCDDEE;
    for (unsigned i = 0; i < 8; ++i) {
        before.st[i] = (double)i * 1.25 - 3.5;
        before.st_i64[i] = 9007199254740993ll + i;
        before.mm[i] = 0x8877665544332211ull + i;
        before.xmm[i].u64[0] = 0x1122334455667788ull + i;
        before.xmm[i].u64[1] = 0xFFEEDDCCBBAA9988ull - i;
    }
    for (int value = -4; value <= 0xFFFF; ++value) {
        before.fpu_cmp = value < 0 ? value + 3 : 0x10000 | value;
        void_regs_load(&before);
#ifdef RECOMP_X87_WINDOW
        int position = g_fp_sp;
#endif
        void_fpu_clear_exceptions(0x10019376);
        void_regs_save(&after);
        if (value >= 0) before.fpu_cmp &= ~0x80FF;
        assert(!memcmp(&before, &after, sizeof before));
#ifdef RECOMP_X87_WINDOW
        assert(g_fp_sp == position);
#endif
    }
    puts("FNCLEX production host PASS: encoded status mask, scalar comparisons, all register/x87/MMX/SSE/EH snapshots preserved");
    return 0;
}
