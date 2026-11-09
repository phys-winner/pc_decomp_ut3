/* Controller perception: candidate eligibility (00FC8DE0) and notification
 * traversal (00FCBCB0). Filtering and traversal use native locals. Visibility
 * traces, virtual overrides and SeePlayer/SeeMonster script events retain their
 * original policy callbacks; no sensing intervals or AI behavior are changed. */
#include "host.h"
#include "native_mode.inc"

extern void __real_sub_00FC8DE0(void), __real_sub_00FCBCB0(void);
extern void sub_00486FD0(void), sub_00CE85C0(void), sub_00FC99E0(void), sub_00FC9980(void);
static int ai_perception_mode=-1;

static __attribute__((cold,noinline)) int perception_load_mode(void)
{
    char path[1100];
    snprintf(path,sizeof path,"%s/ai-perception-native",void_data_dir());
    FILE *f=fopen(path,"rb");const int value=f?fgetc(f):'1';if(f)fclose(f);
    /* Perception events can run script. Diagnostic mode never runs them twice. */
    ai_perception_mode=void_native_mode(value=='0'||value=='2'?0:1);
    void_log("[native-ai] controller perception: mode %d",ai_perception_mode);
    return ai_perception_mode;
}

typedef struct {
    uint32_t eax,ecx,edx,ebx,esp,esi,edi,ebp,backedges;
    uint32_t kind,a,b,carry;
    xmm_t xmm0;
} perception_context;

static inline void perception_read(perception_context *r)
{
    r->eax=g_eax;r->ecx=g_ecx;r->edx=g_edx;r->ebx=g_ebx;
    r->esp=g_esp;r->esi=g_esi;r->edi=g_edi;r->ebp=g_ebp;
    r->xmm0=g_xmm[0];r->backedges=g_backedges;
}
static inline void perception_write(const perception_context *r)
{
    g_eax=r->eax;g_ecx=r->ecx;g_edx=r->edx;g_ebx=r->ebx;
    g_esp=r->esp;g_esi=r->esi;g_edi=r->edi;g_ebp=r->ebp;
    g_xmm[0]=r->xmm0;g_backedges=r->backedges;
}
static inline void perception_flags_in(perception_context *r)
{r->kind=g_flag_k;r->a=g_flag_a;r->b=g_flag_b;r->carry=g_flag_cf;}
static inline void perception_flags_out(const perception_context *r)
{g_flag_k=r->kind;g_flag_a=r->a;g_flag_b=r->b;g_flag_cf=r->carry;}
static inline void perception_cmp(perception_context *r,uint32_t a,uint32_t b)
{r->kind=FK_CMP;r->a=a;r->b=b;r->carry=a<b;}
static inline void perception_test(perception_context *r,uint32_t a,uint32_t b)
{r->kind=FK_TEST;r->a=a;r->b=b;r->carry=0;}
static inline void perception_test_byte(perception_context *r,uint32_t a,uint32_t b)
{r->kind=FK_NARROW(FK_TEST,24);r->a=a<<24;r->b=b<<24;r->carry=0;}
static inline void perception_call(perception_context *r,recomp_func_t fn)
{
    PUSH32(r->esp,RECOMP_RETADDR);perception_write(r);perception_flags_out(r);
    fn();perception_read(r);perception_flags_in(r);
}
static inline void perception_icall(perception_context *r,uint32_t target)
{
    perception_write(r);perception_flags_out(r);
    recomp_func_t fn=void_resolve(target);
    PUSH32(r->esp,RECOMP_RETADDR);g_esp=r->esp;
    fn();perception_read(r);perception_flags_in(r);
}
static inline void perception_backedge(perception_context *r)
{
    if(++r->backedges>=RECOMP_YIELD_EVERY && recomp_yield_hook){
        r->backedges=0;perception_write(r);
        /* Original branches retain their local arithmetic flags across yields. */
        recomp_yield_hook();perception_read(r);
    }
}

static void perception_eligible(void)
{
    perception_context r;perception_read(&r);perception_flags_in(&r);
    PUSH32(r.esp,r.esi);r.esi=r.ecx;
    uint32_t player=MEM8(r.esi+0x1C4);
    perception_test_byte(&r,player,1);
    PUSH32(r.esp,r.edi);r.edi=MEM32(r.esp+12);
    if(!(player&1)){
        player=MEM8(r.edi+0x1C4);perception_test_byte(&r,player,1);
        if(!(player&1))goto rejected;
    }
    r.xmm0=(xmm_t){0};
    r.a=SSE_COMI(0.0f,sse_f32(MEM32(r.esi+0x28C)));
    r.kind=FK_EFLAGS;r.b=0;r.carry=r.a&1u;
    if(r.a&(1u|0x40u))goto rejected;
    r.esp-=8;player=MEM8(r.edi+0x1C4);perception_test_byte(&r,player,1);
    r.eax=r.esp;MEM32(r.eax+4)=0;MEM32(r.eax)=player&1?0x154:0x158;
    perception_call(&r,sub_00486FD0);
    perception_test(&r,r.eax,r.eax);if(!r.eax)goto rejected;
    uint32_t flags=MEM32(r.esi+0x1C4);perception_test(&r,flags,0x20000);
    if(flags&0x20000)goto accepted;
    r.eax=MEM32(r.esi+0x98);r.eax=MEM32(r.eax+0x338);
    perception_test(&r,r.eax,r.eax);
    if(r.eax){
        flags=MEM8(r.eax+0x1B4);perception_test_byte(&r,flags,4);
        if(!(flags&4))goto accepted;
    }
    r.esi=MEM32(r.esi+0x1B8);perception_test(&r,r.esi,r.esi);if(!r.esi)goto accepted;
    r.esi=MEM32(r.esi+0x1F4);perception_test(&r,r.esi,r.esi);if(!r.esi)goto accepted;
    r.eax=MEM32(r.edi+0x1B8);perception_test(&r,r.eax,r.eax);if(!r.eax)goto accepted;
    r.eax=MEM32(r.eax+0x1F4);perception_test(&r,r.eax,r.eax);if(!r.eax)goto accepted;
    perception_cmp(&r,r.esi,r.eax);if(r.esi==r.eax)goto rejected;
accepted:
    r.eax=1;r.edi=POP32_VAL(r.esp);r.esi=POP32_VAL(r.esp);r.esp+=8;
    perception_write(&r);perception_flags_out(&r);return;
rejected:
    r.edi=POP32_VAL(r.esp);r.eax=0;perception_test(&r,0,0);
    r.esi=POP32_VAL(r.esp);r.esp+=8;perception_write(&r);perception_flags_out(&r);
}

static void perception_scan(void)
{
    perception_context r;perception_read(&r);perception_flags_in(&r);
    PUSH32(r.esp,r.edi);r.edi=r.ecx;
    perception_cmp(&r,MEM32(r.edi+0x1B4),0);
    if(MEM32(r.edi+0x1B4)){
        r.ecx=MEM32(0x1DEE5ECu);PUSH32(r.esp,r.esi);
        perception_call(&r,sub_00CE85C0);
        r.esi=r.eax;perception_test(&r,r.esi,r.esi);
        if(r.esi)do{
            perception_cmp(&r,r.esi,r.edi);
            if(r.esi!=r.edi){
                r.eax=MEM32(r.esi);r.edx=MEM32(r.eax+0x32C);
                PUSH32(r.esp,r.edi);r.ecx=r.esi;
                perception_icall(&r,r.edx);
                perception_test(&r,r.eax,r.eax);
                if(r.eax){
                    r.ecx=MEM32(r.edi+0x1B4);r.eax=MEM32(r.esi);r.edx=MEM32(r.eax+0x330);
                    PUSH32(r.esp,1);PUSH32(r.esp,r.ecx);r.ecx=r.esi;
                    perception_icall(&r,r.edx);
                    perception_test(&r,r.eax,r.eax);
                    if(r.eax){
                        const uint32_t player=MEM8(r.edi+0x1C4);perception_test_byte(&r,player,1);
                        if(player&1){
                            r.eax=MEM32(r.edi+0x1B4);PUSH32(r.esp,r.eax);r.ecx=r.esi;
                            perception_call(&r,sub_00FC99E0);
                        }else{
                            r.ecx=MEM32(r.edi+0x1B4);PUSH32(r.esp,r.ecx);r.ecx=r.esi;
                            perception_call(&r,sub_00FC9980);
                        }
                    }
                }
            }
            r.esi=MEM32(r.esi+0x1C0);perception_test(&r,r.esi,r.esi);
            perception_backedge(&r);
        }while(TEST_NZ(r.a,r.b));
        r.esi=POP32_VAL(r.esp);
    }
    r.edi=POP32_VAL(r.esp);r.esp+=4;perception_write(&r);perception_flags_out(&r);
}

void __wrap_sub_00FC8DE0(void)
{
    const int mode=__builtin_expect(ai_perception_mode>=0,1)?ai_perception_mode:perception_load_mode();
    if(mode==1)perception_eligible();else __real_sub_00FC8DE0();
}
void __wrap_sub_00FCBCB0(void)
{
    const int mode=__builtin_expect(ai_perception_mode>=0,1)?ai_perception_mode:perception_load_mode();
    if(mode==1)perception_scan();else __real_sub_00FCBCB0();
}
void void_ai_perception_native_init(void)
{
    void_dispatch_add(0x00FC8DE0u,__wrap_sub_00FC8DE0);
    void_dispatch_add(0x00FCBCB0u,__wrap_sub_00FCBCB0);
}
