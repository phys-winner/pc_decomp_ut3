/* Native visible-light array reset: destroy per-DPG state, then resize storage. */
#include "host.h"
#include "native_mode.inc"
extern void __real_sub_00EFF830(void);
extern void sub_01288F71(void);
extern void sub_004055A0(void);
static int render_light_array_mode=-1;
static __attribute__((cold,noinline)) int light_array_load_mode(void)
{
    char path[1100];snprintf(path,sizeof path,"%s/render-light-native",void_data_dir());
    FILE *f=fopen(path,"rb");int value=f?fgetc(f):'0';if(f)fclose(f);
    /* Destructors and allocator callbacks have effects; diagnostic 2 runs original once. */
    render_light_array_mode=void_native_mode(value=='0'||value=='2'?0:1);
    void_log("[native-render] light-array reset: mode %d",render_light_array_mode);
    return render_light_array_mode;
}
typedef struct {uint32_t a,c,d,b,sp,bp,si,di,backedges,fk,fa,fb,fc;} light_array_context;
static inline void light_read(light_array_context *r)
{r->a=g_eax;r->c=g_ecx;r->d=g_edx;r->b=g_ebx;r->sp=g_esp;r->bp=g_ebp;r->si=g_esi;r->di=g_edi;r->backedges=g_backedges;}
static inline void light_write(const light_array_context *r)
{g_eax=r->a;g_ecx=r->c;g_edx=r->d;g_ebx=r->b;g_esp=r->sp;g_ebp=r->bp;g_esi=r->si;g_edi=r->di;g_backedges=r->backedges;}
static inline void light_flags_in(light_array_context *r)
{r->fk=g_flag_k;r->fa=g_flag_a;r->fb=g_flag_b;r->fc=g_flag_cf;}
static inline void light_flags_out(const light_array_context *r)
{g_flag_k=r->fk;g_flag_a=r->fa;g_flag_b=r->fb;g_flag_cf=r->fc;}
static inline void light_cmp(light_array_context *r,uint32_t a,uint32_t b)
{r->fk=FK_CMP;r->fa=a;r->fb=b;r->fc=a<b;}
static inline void light_test(light_array_context *r,uint32_t a)
{r->fk=FK_TEST;r->fa=a;r->fb=a;r->fc=0;}
static inline void light_call(light_array_context *r,recomp_func_t fn)
{PUSH32(r->sp,RECOMP_RETADDR);light_flags_out(r);light_write(r);fn();light_read(r);light_flags_in(r);}
static inline void light_yield(light_array_context *r)
{
    if(++r->backedges>=RECOMP_YIELD_EVERY&&recomp_yield_hook){
        r->backedges=0;light_write(r);recomp_yield_hook();light_read(r);
    }
}
static void light_array_reset(void)
{
    light_array_context r;light_read(&r);
    PUSH32(r.sp,r.b);PUSH32(r.sp,r.si);PUSH32(r.sp,r.di);r.si=r.c;r.di=0;
    const uint32_t count=MEM32(r.si+4);light_cmp(&r,count,0);
    if((int32_t)count>0){
        r.b=0;light_test(&r,0);
        for(;;){
            r.a=MEM32(r.si);r.c=r.a+r.b+4u;
            PUSH32(r.sp,0x00EFC5F0u);PUSH32(r.sp,4);PUSH32(r.sp,0x1C);PUSH32(r.sp,r.c);
            light_call(&r,sub_01288F71);
            ++r.di;r.b+=0x74u;
            const uint32_t remaining=MEM32(r.si+4);light_cmp(&r,r.di,remaining);
            const int more=(int32_t)r.di<(int32_t)remaining;
            light_yield(&r);
            if(!more)break;
        }
    }
    r.a=MEM32(r.sp+0x10);const uint32_t capacity=MEM32(r.si+8);
    light_cmp(&r,capacity,r.a);MEM32(r.si+4)=0;
    if(capacity!=r.a){
        r.di=MEM32(r.si);light_test(&r,r.di);MEM32(r.si+8)=r.a;
        if(!r.di)light_test(&r,r.a);
        if(r.di||r.a){
            r.c=MEM32(0x1DAC458);r.a*=0x74u;r.b=r.a;light_test(&r,r.c);
            if(!r.c){light_call(&r,sub_004055A0);r.c=MEM32(0x1DAC458);}
            r.d=MEM32(r.c);r.a=MEM32(r.d+8);
            PUSH32(r.sp,8);PUSH32(r.sp,r.b);PUSH32(r.sp,r.di);
            light_flags_out(&r);light_write(&r);recomp_func_t fn=void_resolve(r.a);
            light_read(&r);PUSH32(r.sp,RECOMP_RETADDR);g_esp=r.sp;
            fn();light_read(&r);light_flags_in(&r);MEM32(r.si)=r.a;
        }
    }
    r.di=POP32_VAL(r.sp);r.si=POP32_VAL(r.sp);r.b=POP32_VAL(r.sp);r.sp+=8;
    light_write(&r);light_flags_out(&r);
}
void __wrap_sub_00EFF830(void)
{
    int mode=__builtin_expect(render_light_array_mode>=0,1)?render_light_array_mode:light_array_load_mode();
    if(mode==1)light_array_reset();else __real_sub_00EFF830();
}
void void_render_light_native_init(void)
{void_dispatch_add(0x00EFF830u,__wrap_sub_00EFF830);}
