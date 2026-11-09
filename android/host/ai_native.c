/* UE3 Pawn's weighted navigation search (010275E0). The graph traversal,
 * reachability filtering, relaxation and ordered frontier are native C.
 * Game-specific edge costs, goal evaluators and final route policy keep their
 * original callbacks. Guest frames remain valid at callbacks and VM yields. */
#include "host.h"
#include "native_mode.inc"

extern void __real_sub_010275E0(void);
extern void sub_004050E0(void), sub_00405190(void), sub_0040D7E0(void);
extern void sub_00B54280(void), sub_00404A00(void);
static int ai_native_mode = -1;

static __attribute__((cold, noinline)) int ai_native_mode_load(void)
{
    char path[1100];
    snprintf(path, sizeof path, "%s/ai-native", void_data_dir());
    FILE *f = fopen(path, "rb");
    const int value = f ? fgetc(f) : '1';
    if (f) fclose(f);
    /* Evaluators can run script or mutate the world. Never execute them twice. */
    ai_native_mode = void_native_mode(value == '0' || value == '2' ? 0 : 1);
    void_log("[native-ai] weighted navigation search: mode %d", ai_native_mode);
    return ai_native_mode;
}

static inline void ai_flags(uint32_t kind, uint32_t a, uint32_t b, uint32_t carry)
{ g_flag_k = kind; g_flag_a = a; g_flag_b = b; g_flag_cf = carry; }
static inline void ai_cmp(uint32_t a, uint32_t b)
{ ai_flags(FK_CMP, a, b, a < b); }
static inline void ai_test(uint32_t a, uint32_t b)
{ ai_flags(FK_TEST, a, b, 0); }
static inline void ai_add_flags(uint32_t a, uint32_t b)
{ ai_flags(FK_ADD, a, b, a + b < a); }
typedef struct { uint32_t kind, a, b, carry; } ai_flag_state;
static inline ai_flag_state ai_flags_save(void)
{ return (ai_flag_state){g_flag_k,g_flag_a,g_flag_b,g_flag_cf}; }
static inline void ai_flags_load(ai_flag_state flags)
{ ai_flags(flags.kind,flags.a,flags.b,flags.carry); }
static inline void ai_call(ai_flag_state *published, recomp_func_t fn)
{ PUSH32(g_esp, RECOMP_RETADDR); fn(); *published=ai_flags_save(); }
static inline void ai_backedge(ai_flag_state *published)
{
    if (++g_backedges >= RECOMP_YIELD_EVERY && recomp_yield_hook) {
        /* Lifted branches keep arithmetic flags local across yields. */
        const ai_flag_state pending=ai_flags_save();
        ai_flags_load(*published); g_backedges=0; recomp_yield_hook();
        *published=ai_flags_save(); ai_flags_load(pending);
    }
}
static inline void ai_movss(uint32_t bits)
{ g_xmm[0].u64[0] = bits; g_xmm[0].u64[1] = 0; }
static inline void ai_comiss(float a, float b)
{ uint32_t flags = SSE_COMI(a, b); ai_flags(FK_EFLAGS, flags, 0, flags & 1u); }

/* The original diagnoses non-positive custom edge costs, then clamps to one.
 * Keep this uncommon path and its temporary FString lifetime unchanged. */
static __attribute__((noinline)) void ai_bad_cost(uint32_t frame, ai_flag_state *published)
{
    g_ecx = frame + 0x60u;
    PUSH32(g_esp, g_ecx); g_ecx = g_esi;
    ai_call(published, sub_0040D7E0);
    g_esi = g_eax;
    g_edx = frame + 0x54u;
    PUSH32(g_esp, g_edx); g_ecx = g_edi;
    MEM8(frame + 0x74) = 1;
    ai_call(published, sub_0040D7E0);
    g_ebx = g_eax;
    g_ecx = MEM32(frame + 0x14);
    g_eax = frame + 0x48u;
    PUSH32(g_esp, g_eax); MEM8(frame + 0x74) = 2;
    ai_call(published, sub_0040D7E0);
    MEM8(frame + 0x74) = 3;
    g_esi = MEM32(g_esi + 4) ? MEM32(g_esi) : 0x173421Cu;
    g_ebx = MEM32(g_ebx + 4) ? MEM32(g_ebx) : 0x173421Cu;
    ai_cmp(MEM32(g_eax + 4), 0);
    g_eax = MEM32(g_eax + 4) ? MEM32(g_eax) : 0x173421Cu;
    g_ecx = MEM32(0x1D13040u);
    PUSH32(g_esp, g_esi); PUSH32(g_esp, g_ebx); PUSH32(g_esp, g_eax);
    PUSH32(g_esp, g_ebp); PUSH32(g_esp, 0x1ABFA28u); PUSH32(g_esp, g_ecx);
    ai_call(published, sub_00B54280);
    ai_add_flags(g_esp, 24); g_esp += 24;
    g_ecx = frame + 0x48u; MEM8(frame + 0x74) = 2; ai_call(published, sub_00404A00);
    g_ecx = frame + 0x54u; MEM8(frame + 0x74) = 1; ai_call(published, sub_00404A00);
    g_ecx = frame + 0x60u; MEM8(frame + 0x74) = 0; ai_call(published, sub_00404A00);
    g_ebx = MEM32(frame + 0x18); g_ebp = 1;
}

static void ai_search(void)
{
    ai_flag_state published=ai_flags_save();
    PUSH32(g_esp, UINT32_MAX); PUSH32(g_esp, 0x016A2170u);
    g_eax = MEM32(FS_BASE); PUSH32(g_esp, g_eax);
    g_esp -= 0x58;
    PUSH32(g_esp, g_ebx); PUSH32(g_esp, g_ebp);
    PUSH32(g_esp, g_esi); PUSH32(g_esp, g_edi);
    g_eax = MEM32(0x1D324E0u) ^ g_esp;
    ai_test(g_eax, g_eax); PUSH32(g_esp, g_eax);
    const uint32_t frame = g_esp;
    g_eax = frame + 0x6Cu; MEM32(FS_BASE) = g_eax;
    g_edi = g_ecx; MEM32(frame + 0x2C) = g_edi;
    PUSH32(g_esp, 0xB56u); g_ecx = frame + 0x40u;
    ai_call(&published, sub_004050E0);

    g_ebx = MEM32(frame + 0x80); g_eax = 0; ai_test(0, 0);
    MEM32(frame + 0x74) = 0; MEM32(frame + 0x1C) = 0;
    g_eax = MEM32(g_edi + 0x358); ai_movss(MEM32(g_eax + 0x1BC));
    g_ebp = g_ebx; MEM32(frame + 0x14) = g_ebp; MEM32(frame + 0x18) = g_ebx;
    MEM32(frame + 0x30) = g_xmm[0].u32[0];
    g_eax = (uint32_t)sse_cvt_i32(g_xmm[0].f32[0], 1);
    g_ecx = MEM32(g_edi + 0x358); ai_movss(MEM32(g_ecx + 0x1B8));
    MEM32(frame + 0x24) = g_eax; MEM32(frame + 0x30) = g_xmm[0].u32[0];
    g_eax = (uint32_t)sse_cvt_i32(g_xmm[0].f32[0], 1);
    ai_movss(MEM32(g_edi + 0x280));
    g_xmm[0].f32[0] *= sse_f32(MEM32(g_edi + 0x27C));
    MEM32(frame + 0x28) = g_eax; MEM32(frame + 0x30) = g_xmm[0].u32[0];
    g_eax = (uint32_t)sse_cvt_i32(g_xmm[0].f32[0], 1);
    g_edx = MEM32(g_edi); MEM32(frame + 0x3C) = g_eax;
    g_eax = MEM32(g_edx + 0x390); g_ecx = g_edi;
    ai_call(&published, void_resolve(g_eax));
    MEM32(frame + 0x38) = g_eax;
    if (MEM8(g_edi + 0x1D4) & 0x20) {
        ai_movss(MEM32(g_edi + 0x1E0)); MEM32(frame + 0x30) = g_xmm[0].u32[0];
        g_eax = (uint32_t)sse_cvt_i32(g_xmm[0].f32[0], 1);
        ai_movss(MEM32(g_edi + 0x1E4)); MEM32(frame + 0x28) = g_eax;
        MEM32(frame + 0x30) = g_xmm[0].u32[0];
        g_eax = (uint32_t)sse_cvt_i32(g_xmm[0].f32[0], 1);
        MEM32(frame + 0x24) = g_eax;
    }
    g_ecx = MEM32(frame + 0x8C) - 1u; g_edx = 9999998;
    g_eax = g_ecx <= g_edx;
    MEM32(frame + 0x34) = g_eax; MEM32(frame + 0x20) = 0;
    ai_test(g_ebx, g_ebx);
    if (g_ebx) for (;;) {
        ai_test(g_eax, g_eax);
        if (g_eax) {
            g_eax = MEM32(frame + 0x8C);
            ai_cmp(MEM32(g_ebp + 0x220), g_eax);
            if ((int32_t)MEM32(g_ebp + 0x220) > (int32_t)g_eax) break;
        }
        MEM32(g_ebp + 0x1B4) |= 0x10u;
        ai_test(MEM32(g_ebp + 0x1B4), MEM32(g_ebp + 0x1B4));
        g_esi = MEM32(frame + 0x84);
        fp_push(sse_f32(MEM32(g_esi)));
        PUSH32(g_esp, g_ecx); *(float *)ADDR(g_esp) = (float)fp_pop();
        PUSH32(g_esp, g_edi); PUSH32(g_esp, g_ebp);
        ai_call(&published, void_resolve(MEM32(frame + 0x7C)));
        *(float *)ADDR(frame + 0x30) = (float)g_st[0];
        fp_push(sse_f32(MEM32(g_esi)));
        g_esp += 12;
        { double value=g_st[0]; g_st[0]=g_st[1]; g_st[1]=value;
          int64_t shadow=g_st_i64[0]; g_st_i64[0]=g_st_i64[1]; g_st_i64[1]=shadow; }
        const uint32_t comparison = SSE_COMI(g_st[0], g_st[1]);
        ai_flags(FK_EFLAGS, comparison, 0, comparison & 1u);
        fp_pop(); fp_pop();
        if (!(comparison & (1u | 0x40u))) {
            ai_movss(MEM32(frame + 0x30)); MEM32(g_esi) = g_xmm[0].u32[0];
            MEM32(frame + 0x1C) = g_ebp;
        }
        ai_movss(MEM32(g_esi)); ai_comiss(g_xmm[0].f32[0], sse_f32(MEM32(0x17343D8u)));
        if (g_xmm[0].f32[0] >= sse_f32(MEM32(0x17343D8u))) break;
        g_eax = MEM32(frame + 0x20); g_ecx = g_eax; ++g_eax;
        ai_cmp(g_ecx, MEM32(frame + 0x90)); MEM32(frame + 0x20) = g_eax;
        if ((int32_t)g_ecx > (int32_t)MEM32(frame + 0x90)) {
            ai_comiss(g_xmm[0].f32[0], sse_f32(MEM32(0x1737E48u)));
            if (g_xmm[0].f32[0] > sse_f32(MEM32(0x1737E48u))) break;
        }
        g_eax = 0; MEM32(frame + 0x30) = 0;
        ai_cmp(MEM32(g_ebp + 0x1F4), 0);
        if ((int32_t)MEM32(g_ebp + 0x1F4) > 0) do {
            g_edx = MEM32(g_ebp + 0x1F0); g_esi = MEM32(g_edx + g_eax * 4u);
            if (!g_esi) goto next_edge;
            g_eax = MEM32(g_esi + 0x54);
            if (!g_eax || (MEM8(g_eax + 0x1B4) & 0x10)) goto next_edge;
            g_eax = MEM32(frame + 0x24);
            if ((int32_t)MEM32(g_esi + 0x68) < (int32_t)g_eax) goto next_edge;
            g_ecx = MEM32(frame + 0x28);
            if ((int32_t)MEM32(g_esi + 0x6C) < (int32_t)g_ecx) goto next_edge;
            g_eax = MEM32(g_esi + 0x70); g_edx = g_eax & MEM32(frame + 0x38);
            if (g_edx != g_eax) goto next_edge;
            g_eax = MEM32(frame + 0x3C);
            ai_cmp(MEM32(g_esi + 0x74), g_eax);
            if ((int32_t)MEM32(g_esi + 0x74) > (int32_t)g_eax) goto next_edge;
            g_edx = MEM32(g_esi); g_eax = MEM32(g_edx + 0x10C);
            PUSH32(g_esp, g_edi); g_ecx = g_esi;
            ai_call(&published, void_resolve(g_eax));
            g_ebp = g_eax;
            if ((int32_t)g_ebp < 10000000) {
                ai_test(g_ebp, g_ebp); g_edi = MEM32(g_esi + 0x54);
                if ((int32_t)g_ebp <= 0) ai_bad_cost(frame, &published);
                /* Relax this edge, removing a previously queued destination
                 * and reinserting it in the original stable frontier order. */
                g_edx = MEM32(MEM32(frame + 0x14) + 0x220) + g_ebp;
                if ((int32_t)MEM32(g_edi + 0x220) > (int32_t)g_edx) {
                    g_eax = MEM32(frame + 0x14); MEM32(g_edi + 0x234) = g_eax;
                    g_eax = MEM32(g_edi + 0x230); g_esi = 0;
                    if (g_eax) {
                        g_ecx = MEM32(g_edi + 0x22C); MEM32(g_eax + 0x22C) = g_ecx;
                        g_eax = MEM32(g_edi + 0x22C);
                        if (g_eax) { g_ecx = MEM32(g_edi + 0x230); MEM32(g_eax + 0x230) = g_ecx; }
                        if (g_ebx != g_edi) g_eax = MEM32(g_ebx + 0x220);
                        if (g_ebx == g_edi || (int32_t)g_eax > (int32_t)MEM32(g_edi + 0x220)) {
                            g_ecx = MEM32(g_edi + 0x230); MEM32(frame + 0x18) = g_ecx; g_ebx = g_ecx;
                        }
                        MEM32(g_edi + 0x230) = 0; MEM32(g_edi + 0x22C) = 0;
                    }
                    MEM32(g_edi + 0x220) = g_edx; g_eax = g_ebx;
                    if ((int32_t)MEM32(g_ebx + 0x220) <= (int32_t)g_edx) {
                        g_ecx = MEM32(g_ebx + 0x22C);
                        while (g_ecx && (int32_t)MEM32(g_ecx + 0x220) < (int32_t)g_edx) {
                            g_eax = g_ecx; g_ecx = MEM32(g_eax + 0x22C);
                            ai_cmp(g_ecx, g_esi); ai_backedge(&published);
                            if (!CMP_NE(g_flag_a,g_flag_b)) break;
                        }
                    } else {
                        g_ecx = g_ebx + 0x230u;
                        if (MEM32(g_ecx)) while ((int32_t)MEM32(g_eax + 0x220) > (int32_t)g_edx) {
                            g_eax = MEM32(g_ecx); ai_cmp(MEM32(g_eax + 0x230), g_esi);
                            g_ecx = g_eax + 0x230u; ai_backedge(&published);
                            if (!CMP_NE(g_flag_a,g_flag_b)) break;
                        }
                    }
                    g_ecx = MEM32(g_eax + 0x22C);
                    if (g_ecx != g_edi) {
                        if (g_ecx) MEM32(g_ecx + 0x230) = g_edi;
                        g_edx = MEM32(g_eax + 0x22C);
                        MEM32(g_edi + 0x22C) = g_edx; MEM32(g_eax + 0x22C) = g_edi;
                        MEM32(g_edi + 0x230) = g_eax;
                    }
                    MEM32(frame + 0x18) = g_edi; g_ebx = g_edi;
                }
                g_edi = MEM32(frame + 0x2C);
            }
            g_ebp = MEM32(frame + 0x14);
next_edge:
            g_eax = MEM32(frame + 0x30) + 1u;
            ai_cmp(g_eax, MEM32(g_ebp + 0x1F4)); MEM32(frame + 0x30) = g_eax;
            ai_backedge(&published);
        } while (CMP_L(g_flag_a,g_flag_b));
        g_eax = MEM32(g_ebp + 0x22C); ai_test(g_eax, g_eax);
        MEM32(frame + 0x14) = g_eax; ai_backedge(&published);
        if (!TEST_NZ(g_flag_a,g_flag_b)) break;
        g_ebp = MEM32(frame + 0x14); g_eax = MEM32(frame + 0x34);
    }
    g_ecx = MEM32(frame + 0x88); g_edx = MEM32(frame + 0x80); g_eax = MEM32(g_edi);
    PUSH32(g_esp, g_ecx); g_ecx = MEM32(frame + 0x1C); PUSH32(g_esp, g_edx);
    g_edx = MEM32(g_eax + 0x38C); PUSH32(g_esp, g_ecx); g_ecx = g_edi;
    ai_call(&published, void_resolve(g_edx));
    g_ecx = frame + 0x40u; g_esi = g_eax; MEM32(frame + 0x74) = UINT32_MAX;
    ai_call(&published, sub_00405190);
    g_eax = g_esi; g_ecx = MEM32(frame + 0x6C); MEM32(FS_BASE) = g_ecx;
    g_ecx = POP32_VAL(g_esp); g_edi = POP32_VAL(g_esp); g_esi = POP32_VAL(g_esp);
    g_ebp = POP32_VAL(g_esp); g_ebx = POP32_VAL(g_esp);
    ai_add_flags(g_esp, 0x64); g_esp += 0x80;
}

void __wrap_sub_010275E0(void)
{
    const int mode = __builtin_expect(ai_native_mode >= 0, 1) ? ai_native_mode : ai_native_mode_load();
    if (mode == 1) ai_search(); else __real_sub_010275E0();
}

void void_ai_native_init(void)
{ void_dispatch_add(0x010275E0u, __wrap_sub_010275E0); }
