#include "host.h"
#include <assert.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <inttypes.h>
VOID_HIDDEN recomp_ehframe_t *g_eh_frames;
VOID_HIDDEN uint32_t g_eh_obj,g_eh_info;
uint32_t expected_entry_sp,expected_live_sp;
static int failures,checks,attach_calls,lazy;
static uint64_t checksum=1469598103934665603ull;
void probe_leaf(void);void probe_lazy_call(void);void probe_lazy_tail(void);
void probe_tail_leaf(void);void probe_backedge(void);void probe_eh(void);
void sub_0117C3B0(void);
void recomp_eh_push(recomp_ehframe_t *f){f->prev=g_eh_frames;g_eh_frames=f;}
void recomp_eh_pop(recomp_ehframe_t *f){assert(g_eh_frames==f);g_eh_frames=f->prev;}
void fixture_check(int good,int code){++checks;if(!good){++failures;if(failures<12)fprintf(stderr,"check_failed=%d\n",code);}}
void void_log(const char *fmt,...){va_list args;va_start(args,fmt);vfprintf(stderr,fmt,args);va_end(args);}
void void_lift_trap(uint32_t va,const char *why){fprintf(stderr,"trap=%08X:%s\n",va,why);abort();}
static void mix(uint32_t v){checksum=(checksum^v)*1099511628211ull;}
static void callback(void)
{
    /* Production void_call_guest pushed three DllMain-like args and marker. */
    fixture_check(g_esp==expected_live_sp-16,101);
    fixture_check(g_eax==0x12345678&&g_ecx==0x9ABCDEF0,102);
#ifdef EXPECT_LIVE_FLAGS
    fixture_check(g_flag_k==FK_CMP&&g_flag_a==7&&g_flag_b==9&&g_flag_cf==1,103);
#endif
    fixture_check(MEM32(g_esp)==RECOMP_RETADDR,104);
    fixture_check(MEM32(g_esp+4)==0x10000000&&MEM32(g_esp+8)==1&&MEM32(g_esp+12)==0,105);
    ++attach_calls;g_eax=1;g_ecx=0x11111111;g_esp+=16;
}
static void throw_now(void)
{
    fixture_check(g_eh_frames&&g_eh_frames->entry_esp==expected_entry_sp,106);
    fixture_check(g_eax==0xCAFED00D&&g_esp==expected_entry_sp-52,107);
    g_eax=0xAABBCCDD;g_ecx=0x55667788;g_esp=expected_entry_sp-40;
    g_eh_frames->resume=0x1234;_longjmp(g_eh_frames->jb,1);
}
recomp_func_t void_resolve(uint32_t va)
{
    if((va==0x02000020||va==0x02000050)&&lazy){
        /* Same snapshot/callback/restore order as module_prepare_call. */
        lazy=0;void_regs saved;void_regs_save(&saved);
        uint32_t args[3]={0x10000000,1,0};
        uint32_t result=void_call_guest(0x02000030,3,args,1);
        fixture_check(result==1,108);void_regs_load(&saved);
    }
    switch(va){case 0x02000020:return probe_leaf;case 0x02000030:return callback;
    case 0x02000050:return probe_tail_leaf;case 0x02000080:return throw_now;
    default:abort();}
}
static void yield_hook(void)
{
    fixture_check(g_eax==0x88888888&&g_ecx==0x99999999&&g_esp==expected_entry_sp-32,109);
    g_ecx=0x77777777;
}
static void reset(void)
{
    g_eax=0x1111;g_ecx=0x2222;g_edx=0x3333;g_ebx=0x4444;
    g_esi=0x5555;g_edi=0x6666;g_ebp=0x7777;g_esp=0x3F004;
    expected_entry_sp=g_esp;g_flag_k=FK_TEST;g_flag_a=g_flag_b=0;g_flag_cf=0;
    MEM32(g_esp)=RECOMP_RETADDR;
}
static uint32_t seed=0x139517ACu;
static float random_float(void)
{
    seed=seed*1664525u+1013904223u;
    return(float)((int)(seed%65u)-32)*0.25f;
}
static void original_owner_cases(void)
{
    float *planes=(float*)ADDR(0x20000),*point=(float*)ADDR(0x22000);
    for(unsigned test=0;test<4096;++test){
        unsigned count=(test%5u)*4u;
        for(unsigned i=0;i<4;++i)point[i]=random_float();
        for(unsigned i=0;i<64;++i)planes[i]=random_float();
        float radius=(float)(test%33u)*0.5f;
        int expected=1;
        for(unsigned plane=0;plane<count;++plane){
            unsigned block=(plane/4u)*16u,lane=plane%4u;
            float v=point[1]*planes[block+4u+lane]+point[0]*planes[block+lane];
            v=v+point[2]*planes[block+8u+lane];v=v-planes[block+12u+lane];
            if(radius<v)expected=0;
        }
        reset();g_ecx=0x21000;
        MEM32(g_ecx+12)=0x20000;MEM32(g_ecx+16)=count;
        MEM32(g_esp+4)=0x22000;memcpy((void*)ADDR(g_esp+8),&radius,4);
        sub_0117C3B0();
        fixture_check(g_eax==(uint32_t)expected&&g_esp==expected_entry_sp+12,120);
        fixture_check(g_ebx==0x4444&&g_ebp==0x7777&&g_esi==0x5555&&g_edi==0x6666,121);
        mix(g_eax);mix(g_ecx);mix(g_edx);mix(g_flag_k);mix(g_flag_a);mix(g_flag_b);mix(g_flag_cf);
        for(unsigned i=0;i<8;++i)for(unsigned j=0;j<4;++j)mix(g_xmm[i].u32[j]);
    }
}
int main(void)
{
    assert(mmap((void*)VOID_GUEST_BASE,0x40000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)==(void*)VOID_GUEST_BASE);
    for(unsigned i=0;i<64;++i){
        reset();lazy=1;probe_lazy_call();fixture_check(g_esp==expected_entry_sp+4,110);mix(g_eax);mix(g_flag_k);mix(g_flag_a);
        reset();lazy=1;probe_lazy_tail();fixture_check(g_esp==expected_entry_sp+4,111);mix(g_eax);mix(g_flag_k);mix(g_flag_a);
        reset();recomp_yield_hook=yield_hook;probe_backedge();recomp_yield_hook=NULL;fixture_check(g_esp==expected_entry_sp+4,112);mix(g_ecx);mix(g_flag_k);mix(g_flag_a);
        reset();probe_eh();fixture_check(g_esp==expected_entry_sp+4&&!g_eh_frames,113);mix(g_eax);mix(g_ecx);
    }
    original_owner_cases();
    printf("%s checks=%d failures=%d lazy_callbacks=%d exact_owner_cases=4096 checksum=%016" PRIx64 "\n",failures?"FAIL":"PASS",checks,failures,attach_calls,checksum);
    return failures?1:0;
}
