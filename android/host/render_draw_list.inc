/* One instantiation of UE3's static draw-list traversal (TStaticMeshDrawList::DrawVisible), included by
 * render_draw_native.c with the parameters of the template: DL_NAME (the function), DL_LIST (offset of the sparse
 * element array in a list), DL_STRIDE (element bytes), DL_CALLEE (the draw of one element) and DL_THIS / DL_ELEM (the
 * two guest registers the compiler picked for the list array and the current element). Outgoing registers, flags and
 * the guest stack frame are those of the original at every point the callees can see. */
static void DL_NAME(void)
{
    const uint32_t entry_sp = g_esp;
    g_esp -= 64;
    PUSH32(g_esp, g_ebp);
    PUSH32(g_esp, DL_THIS);
    DL_THIS = g_ecx;
    g_eax = g_ebp = 0;
    MEM32(g_esp + 8) = DL_THIS;
    MEM32(g_esp + 16) = 0;
    if ((int32_t)MEM32(DL_THIS + 4) > 0) {
        PUSH32(g_esp, g_ebx);
        PUSH32(g_esp, DL_ELEM);
        const uint32_t frame = g_esp;
        do {
            const uint32_t list = MEM32(MEM32(DL_THIS) + g_eax * 4u);
            MEM32(frame + 0x20) = list;
            MEM32(frame + 0x30) = list + DL_LIST;
            MEM32(frame + 0x1C) = 0;
            g_eax = list + DL_LIST + 0x18u;
            g_ecx = frame + 0x34u;
            PUSH32(g_esp, g_eax);
            draw_flags(FK_ADD, list + DL_LIST, 0x18u, g_eax < list + DL_LIST);
            draw_call(sub_005DAF50);
            g_ebx = MEM32(frame + 0x48);
            if (g_ebx) {
                g_eax = MEM32(frame + 0x4C);
                for (;;) {
                    DL_ELEM = MEM32(MEM32(frame + 0x30)) + g_eax * DL_STRIDE;
                    const uint32_t bit = MEM32(DL_ELEM);
                    const int32_t word = (int32_t)bit / 32;
                    const uint32_t bits = MEM32(MEM32(frame + 0x58));
                    const uint32_t word_at = bits + (uint32_t)word * 4u;
                    const uint32_t mask = 1u << (bit & 31u);
                    MEM32(frame + 0x28) = word_at;
                    MEM32(frame + 0x2C) = mask;
                    /* Native FBitReference::operator bool, including its outgoing registers/flags. */
                    g_ecx = frame + 0x28u;
                    g_edx = word_at;
                    g_eax = (MEM32(word_at) & mask) != 0;
                    draw_flags(FK_CMP, 0, 0u - g_eax, g_eax);
                    if (g_eax) {
                        g_ebp = MEM32(MEM32(DL_ELEM + 4) + 0xB4);
                        g_eax = MEM32(0x1DCE200u);
                        uint32_t stat = 0;
                        if (g_eax && (int32_t)MEM32(0x1DCE1F8u) > 0) {
                            g_ecx = (MEM32(0x1DCE204u) - 1u) & 0xD8u;
                            g_eax = MEM32(g_eax + g_ecx * 4u);
                            if (g_eax != UINT32_MAX) {
                                g_edx = MEM32(0x1DCE1F4u);
                                for (;;) {
                                    g_ecx = g_edx + g_eax * 12u;
                                    if (MEM32(g_ecx + 4) == 0xD8u) {
                                        g_eax *= 3u;
                                        stat = MEM32(g_edx + g_eax * 4u + 8u);
                                        break;
                                    }
                                    g_eax = MEM32(g_ecx);
                                    RECOMP_BACKEDGE();
                                    if (g_eax == UINT32_MAX) break;
                                }
                            }
                        }
                        DL_THIS = stat;
                        if (!stat) {
                            /* Keep UE3's original assertion/error path if the required stat is absent. */
                            draw_flags(FK_TEST, 0, 0, 0);
                            PUSH32(g_esp, 0x0173421Cu);
                            PUSH32(g_esp, 0x6CDu);
                            PUSH32(g_esp, 0x01734248u);
                            PUSH32(g_esp, 0x01737A20u);
                            draw_call(sub_00B55380);
                            g_esp += 16;
                        }
                        PUSH32(g_esp, g_ebp);
                        const uint32_t stat_base = DL_THIS;
                        DL_THIS += 12;
                        PUSH32(g_esp, DL_THIS);
                        draw_flags(FK_ADD, stat_base, 12, DL_THIS < stat_base);
                        draw_call(void_resolve(MEM32(0x172912Cu)));
                        g_edx = MEM32(frame + 0x20);
                        g_eax = MEM32(frame + 0x54);
                        PUSH32(g_esp, frame + 0x1Cu);
                        g_ecx = MEM32(frame + 0x10);
                        PUSH32(g_esp, g_edx);
                        PUSH32(g_esp, DL_ELEM);
                        PUSH32(g_esp, g_eax);
                        draw_call(DL_CALLEE);
                        DL_THIS = MEM32(frame + 0x10);
                        g_ebp = 1;
                    }
                    g_eax = ~MEM32(frame + 0x34);
                    g_ebx &= g_eax;
                    if (!g_ebx) {
                        g_eax = MEM32(frame + 0x44) - 1u;
                        do {
                            g_ecx = MEM32(frame + 0x38) + 1u;
                            MEM32(frame + 0x40) += 32;
                            MEM32(frame + 0x38) = g_ecx;
                            if ((int32_t)g_ecx > (int32_t)g_eax) goto list_finished;
                            g_edx = MEM32(frame + 0x3C);
                            g_ebx = MEM32(g_edx + g_ecx * 4u);
                            RECOMP_BACKEDGE();
                        } while (!g_ebx);
                    }
                    const uint32_t lowest = g_ebx & (0u - g_ebx);
                    MEM32(frame + 0x34) = lowest;
                    MEM32(frame + 0x24) = lowest;
                    const uint32_t index = (uint32_t)__builtin_ctz(lowest);
                    MEM32(frame + 0x14) = index;
                    g_ecx = 31u - index;
                    g_eax = MEM32(frame + 0x40) + index;
                    RECOMP_BACKEDGE();
                }
            }
list_finished:
            g_eax = MEM32(frame + 0x18) + 1u;
            MEM32(frame + 0x18) = g_eax;
            RECOMP_BACKEDGE();
        } while ((int32_t)g_eax < (int32_t)MEM32(DL_THIS + 4));
        DL_ELEM = POP32_VAL(g_esp);
        g_eax = g_ebp;
        g_ebx = POP32_VAL(g_esp);
    }
    DL_THIS = POP32_VAL(g_esp);
    g_ebp = POP32_VAL(g_esp);
    draw_flags(FK_ADD, g_esp, 64, entry_sp < g_esp);
    g_esp += 76;
}
