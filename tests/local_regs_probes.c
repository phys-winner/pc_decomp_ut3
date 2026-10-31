#define RECOMP_GENERATED_CODE
#include "recomp_types.h"
extern void fixture_check(int,int);
extern uint32_t expected_entry_sp,expected_live_sp;
#define LOCALS uint32_t _cf=0,_flag_k=FK_NONE,_flag_a=0,_flag_b=0,_itail_tgt=0; RECOMP_REGS_LOCALS
#define CHECK(c,n) fixture_check((c),(n))
#define EXIT() do { RECOMP_FLAGS_OUT(); RECOMP_REGS_OUT(); return; } while(0)
void probe_leaf(void)
{
    LOCALS; RECOMP_ENTER(0x02000020u);
    CHECK(_flag_k==FK_CMP&&_flag_a==7&&_flag_b==9,20);
    CHECK(eax==0x12345678&&ecx==0x9ABCDEF0,21);
    eax=3;_flag_k=FK_TEST;_flag_a=_flag_b=eax;_cf=0;
    esp+=4;EXIT();
}
void probe_tail_leaf(void)
{
    LOCALS; RECOMP_ENTER(0x02000050u);
    CHECK(_flag_k==FK_CMP&&_flag_a==7&&_flag_b==9,50);
    CHECK(eax==0x12345678&&ecx==0x9ABCDEF0,51);
    CHECK(MEM32(expected_entry_sp-16)==0xCAFEBABE,52);
    eax=5;_flag_k=FK_TEST;_flag_a=_flag_b=eax;_cf=0;
    esp+=68;EXIT();
}
void probe_lazy_call(void)
{
    LOCALS; RECOMP_ENTER(0x02000010u);
    uint32_t entry=esp;esp-=64;
    MEM32(entry-16)=0xCAFEBABE;
    expected_live_sp=esp;
    eax=0x12345678;ecx=0x9ABCDEF0;
    _flag_k=FK_CMP;_flag_a=7;_flag_b=9;_cf=1;
    RECOMP_ICALL(0x02000020u);RECOMP_FLAGS_IN();
    CHECK(MEM32(entry-16)==0xCAFEBABE,10);
    CHECK(eax==3&&esp==entry-64,11);
    CHECK(_flag_k==FK_TEST&&_flag_a==3&&_flag_b==3&&_cf==0,12);
    eax=0x12345678;ecx=0x9ABCDEF0;
    _flag_k=FK_CMP;_flag_a=7;_flag_b=9;_cf=1;
    RECOMP_CALL(probe_leaf);RECOMP_FLAGS_IN();
    CHECK(eax==3&&esp==entry-64&&_flag_k==FK_TEST&&_flag_a==3,13);
    esp=entry+4;EXIT();
}
void probe_lazy_tail(void)
{
    LOCALS; RECOMP_ENTER(0x02000040u);
    esp-=64;expected_live_sp=esp;
    MEM32(expected_entry_sp-16)=0xCAFEBABE;
    eax=0x12345678;ecx=0x9ABCDEF0;
    _flag_k=FK_CMP;_flag_a=7;_flag_b=9;_cf=1;
    RECOMP_ITAIL(0x02000050u);RECOMP_FLAGS_IN();
    CHECK(eax==5&&esp==expected_entry_sp+4,41);
    EXIT();
}
void probe_backedge(void)
{
    LOCALS; RECOMP_ENTER(0x02000060u);
    eax=0x88888888;ecx=0x99999999;esp-=32;
    _flag_k=FK_CMP;_flag_a=17;_flag_b=12;_cf=0;
    g_backedges=RECOMP_YIELD_EVERY-1;
    RECOMP_BACKEDGE();
    CHECK(eax==0x88888888&&ecx==0x77777777,60);
    CHECK(esp==expected_entry_sp-32&&_flag_k==FK_CMP&&_flag_a==17&&_flag_b==12,61);
    esp=expected_entry_sp+4;EXIT();
}
void probe_eh(void)
{
    LOCALS; RECOMP_ENTER(0x02000070u);
    RECOMP_EH_ENTER();
    eax=0xCAFED00D;esp-=48;
    _flag_k=FK_CMP;_flag_a=3;_flag_b=3;_cf=0;
    RECOMP_ICALL(0x02000080u);
    CHECK(0,70);RECOMP_EH_LEAVE();EXIT();
_ljump:
    CHECK(_itail_tgt==0x1234,71);
    CHECK(eax==0xAABBCCDD&&ecx==0x55667788&&esp==expected_entry_sp-40,72);
    RECOMP_EH_LEAVE();
    esp=expected_entry_sp+4;EXIT();
}
