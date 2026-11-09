/* UE3 temporal primitive occlusion decisions and conservative query bounds. */
#include "host.h"
#include "native_mode.inc"
#include <math.h>

extern void __real_sub_0117AEC0(void),__real_sub_01178C60(void);
extern void sub_01178E50(void),sub_0117ACC0(void),sub_00B55380(void),sub_0117AA10(void);
extern void sub_005D75E0(void),sub_0117AAB0(void),sub_01172010(void);
static int render_occlusion_mode=-1;
static __attribute__((cold,noinline)) int occlusion_load_mode(void)
{
    char path[1100];snprintf(path,sizeof path,"%s/render-occlusion-native",void_data_dir());
    FILE *f=fopen(path,"rb");const int value=f?fgetc(f):'0';if(f)fclose(f);
    render_occlusion_mode=void_native_mode(value=='0'||value=='2'?0:1);
    void_log("[native-render] temporal occlusion: mode %d",render_occlusion_mode);
    return render_occlusion_mode;
}
typedef struct {uint32_t a,c,d,b,sp,bp,si,di,backedges,fk,fa,fb,fc;xmm_t x[8];} occlusion_context;
static inline void oc_read(occlusion_context *r)
{
    r->a=g_eax;r->c=g_ecx;r->d=g_edx;r->b=g_ebx;r->sp=g_esp;r->bp=g_ebp;r->si=g_esi;r->di=g_edi;
    r->backedges=g_backedges;memcpy(r->x,g_xmm,sizeof r->x);
}
static inline void oc_write(const occlusion_context *r)
{
    g_eax=r->a;g_ecx=r->c;g_edx=r->d;g_ebx=r->b;g_esp=r->sp;g_ebp=r->bp;g_esi=r->si;g_edi=r->di;
    g_backedges=r->backedges;memcpy(g_xmm,r->x,sizeof r->x);
}
static inline void oc_flags_in(occlusion_context *r){r->fk=g_flag_k;r->fa=g_flag_a;r->fb=g_flag_b;r->fc=g_flag_cf;}
static inline void oc_flags_out(const occlusion_context *r){g_flag_k=r->fk;g_flag_a=r->fa;g_flag_b=r->fb;g_flag_cf=r->fc;}
static inline void oc_test(occlusion_context *r,uint32_t a,uint32_t b){r->fk=FK_TEST;r->fa=a;r->fb=b;r->fc=0;}
static inline void oc_cmp(occlusion_context *r,uint32_t a,uint32_t b){r->fk=FK_CMP;r->fa=a;r->fb=b;r->fc=a<b;}
static inline uint32_t oc_add(occlusion_context *r,uint32_t a,uint32_t b)
{r->fk=FK_ADD;r->fa=a;r->fb=b;r->fc=a+b<a;return a+b;}
static inline uint32_t oc_sub(occlusion_context *r,uint32_t a,uint32_t b){oc_cmp(r,a,b);return a-b;}
static inline void oc_byte(occlusion_context *r,uint32_t a,uint32_t b)
{r->fk=FK_NARROW(FK_TEST,24);r->fa=a<<24;r->fb=b<<24;r->fc=0;}
static inline void oc_float_cmp(occlusion_context *r,double a,double b)
{r->fk=FK_EFLAGS;r->fa=SSE_COMI(a,b);r->fb=0;r->fc=r->fa&1u;}
static inline int oc_above(const occlusion_context *r){return !(r->fa&0x41u);}
static inline void oc_scalar(occlusion_context *r,unsigned index,uint32_t bits)
{r->x[index]=(xmm_t){0};r->x[index].u32[0]=bits;}
static inline void oc_call(occlusion_context *r,recomp_func_t function)
{
    PUSH32(r->sp,RECOMP_RETADDR);oc_flags_out(r);oc_write(r);function();oc_read(r);oc_flags_in(r);
}
static inline void oc_icall(occlusion_context *r,uint32_t address)
{
    oc_flags_out(r);oc_write(r);recomp_func_t function=void_resolve(address);
    PUSH32(r->sp,RECOMP_RETADDR);g_esp=r->sp;function();oc_read(r);oc_flags_in(r);
}
static inline void oc_backedge(occlusion_context *r)
{
    if(++r->backedges>=RECOMP_YIELD_EVERY&&recomp_yield_hook){r->backedges=0;oc_write(r);recomp_yield_hook();oc_read(r);}
}
/* Keep the production oracle's operand order, including NaN payload priority.
 * C permits commuting these operations even with floating contraction disabled. */
static inline float oc_mul_float(float scale,float input)
{
#if defined(__aarch64__)
    float result;__asm__("fmul %s0, %s1, %s2" : "=w"(result) : "w"(scale),"w"(input));return result;
#elif defined(__x86_64__) || defined(_M_X64)
    __asm__("mulss %1, %0" : "+x"(scale) : "x"(input));return scale;
#else
    return scale*input;
#endif
}
static inline float oc_add_float(float product,float bias)
{
#if defined(__aarch64__)
    float result;__asm__("fadd %s0, %s1, %s2" : "=w"(result) : "w"(product),"w"(bias));return result;
#elif defined(__x86_64__) || defined(_M_X64)
    __asm__("addss %1, %0" : "+x"(product) : "x"(bias));return product;
#else
    return product+bias;
#endif
}

/* Copy the centre, expand each extent and radius, and keep the original lazy
 * constants and ordered stores for overlapping source/output/scratch memory. */
static inline __attribute__((always_inline)) void occlusion_bounds(occlusion_context *r)
{
    r->d=MEM32(0x1DF8B9C);oc_scalar(r,3,MEM32(0x1B1FCC4));r->sp-=12;
    if(!(r->d&1)){r->d|=1;r->x[7]=r->x[3];MEM32(0x1DF8B9C)=r->d;MEM32(0x1DF8B98)=r->x[7].u32[0];}
    else oc_scalar(r,7,MEM32(0x1DF8B98));
    if(!(r->d&2)){
        r->x[4]=r->x[7];r->d|=2;r->x[6]=r->x[4];r->x[5]=r->x[4];MEM32(0x1DF8B9C)=r->d;
        MEM32(0x1DF8B8C)=r->x[6].u32[0];MEM32(0x1DF8B90)=r->x[5].u32[0];MEM32(0x1DF8B94)=r->x[4].u32[0];
    }else {oc_scalar(r,4,MEM32(0x1DF8B94));oc_scalar(r,5,MEM32(0x1DF8B90));oc_scalar(r,6,MEM32(0x1DF8B8C));}
    r->d=MEM32(r->c);oc_scalar(r,0,MEM32(r->c+12));oc_scalar(r,1,MEM32(r->c+16));oc_scalar(r,2,MEM32(r->c+20));
    MEM32(r->a)=r->d;r->d=MEM32(r->c+4);MEM32(r->a+4)=r->d;r->d=MEM32(r->c+8);
    r->x[0].f32[0]=oc_mul_float(r->x[3].f32[0],r->x[0].f32[0]);r->x[6].f32[0]=oc_add_float(r->x[0].f32[0],r->x[6].f32[0]);oc_scalar(r,0,MEM32(r->c+24));
    MEM32(r->a+8)=r->d;MEM32(r->sp)=r->x[6].u32[0];r->d=MEM32(r->sp);MEM32(r->a+12)=r->d;
    r->x[1].f32[0]=oc_mul_float(r->x[3].f32[0],r->x[1].f32[0]);r->x[5].f32[0]=oc_add_float(r->x[1].f32[0],r->x[5].f32[0]);MEM32(r->sp+4)=r->x[5].u32[0];r->d=MEM32(r->sp+4);
    r->x[2].f32[0]=oc_mul_float(r->x[3].f32[0],r->x[2].f32[0]);r->x[4].f32[0]=oc_add_float(r->x[2].f32[0],r->x[4].f32[0]);MEM32(r->a+16)=r->d;
    MEM32(r->sp+8)=r->x[4].u32[0];r->d=MEM32(r->sp+8);
    r->x[0].f32[0]=oc_mul_float(r->x[3].f32[0],r->x[0].f32[0]);r->x[0].f32[0]=oc_add_float(r->x[0].f32[0],r->x[7].f32[0]);
    MEM32(r->a+20)=r->d;MEM32(r->a+24)=r->x[0].u32[0];r->sp=oc_add(r,r->sp,12);r->sp+=4;
}

static void primitive_occlusion(void)
{
    occlusion_context r;oc_read(&r);oc_flags_in(&r);
    PUSH32(r.sp,UINT32_MAX);PUSH32(r.sp,0x16B50F8);r.a=MEM32(FS_BASE);PUSH32(r.sp,r.a);
    r.sp-=0x24;PUSH32(r.sp,r.b);PUSH32(r.sp,r.bp);PUSH32(r.sp,r.si);PUSH32(r.sp,r.di);
    r.a=MEM32(0x1D324E0)^r.sp;oc_test(&r,r.a,r.a);PUSH32(r.sp,r.a);
    const uint32_t frame=r.sp;r.a=frame+0x38;MEM32(FS_BASE)=r.a;MEM32(frame+0x18)=r.c;
    r.a=MEM32(frame+0x48);r.di=MEM32(r.a+8);r.b=r.c+0x1E4;PUSH32(r.sp,r.di);r.c=r.b;oc_call(&r,sub_01178E50);
    r.bp=MEM32(frame+0x4C);r.si=r.a;r.a=0;oc_cmp(&r,r.si,0);MEM32(frame+0x14)=0;
    if(!r.si){
        r.x[0]=(xmm_t){0};MEM32(frame+0x1C)=r.di;MEM32(frame+0x20)=0;
        MEM32(frame+0x24)=MEM32(frame+0x28)=MEM32(frame+0x2C)=0;
        r.c=frame+0x1C;PUSH32(r.sp,r.c);r.c=r.b;MEM32(frame+0x40)=0;oc_call(&r,sub_0117ACC0);
        r.di=0;oc_test(&r,0,0);r.si=r.a;MEM32(frame+0x40)=UINT32_MAX;MEM32(frame+0x14)=0;
    }else{
        oc_cmp(&r,MEM32(r.bp+0x598),0);
        if(MEM32(r.bp+0x598)){r.d=MEM32(r.bp+0x59C);r.di=0;oc_test(&r,0,0);MEM32(frame+0x14)=r.d;}
        else {
            r.di=MEM32(r.si+4);oc_cmp(&r,r.di,0);MEM32(frame+0x4C)=0;
            if(r.di){
                r.c=MEM32(0x1DEB3E8);oc_cmp(&r,r.c,0);
                if(!r.c){
                    PUSH32(r.sp,0x173421C);PUSH32(r.sp,0x187);PUSH32(r.sp,0x1734390);PUSH32(r.sp,0x1734384);
                    oc_call(&r,sub_00B55380);r.c=MEM32(0x1DEB3E8);r.sp=oc_add(&r,r.sp,16);
                }
                r.a=MEM32(r.c);r.a=MEM32(r.a+0x90);PUSH32(r.sp,1);r.d=r.sp+0x50;
                PUSH32(r.sp,r.d);PUSH32(r.sp,r.di);oc_icall(&r,r.a);oc_test(&r,r.a,r.a);
                if(r.a){
                    r.a=MEM32(frame+0x4C);r.c=r.a==0;oc_test(&r,r.a,r.a);MEM32(frame+0x4C)=r.a;
                    fp_push((double)(int32_t)MEM32(frame+0x4C));r.di=r.c;
                    if((int32_t)r.a<0)g_st[0]+=(double)MEMF(0x173D9B8);
                    fp_push((double)MEMF(r.bp+0x30));MEM32(frame+0x14)=1;g_st[0]*=(double)MEMF(r.bp+0x2C);
                    const double area=fp_pop();if(area!=0.0)g_st[0]/=area;MEMF(r.si+16)=(float)fp_pop();
                }else {r.di=0;oc_test(&r,0,0);}
            }else{
                r.d=MEM32(0x1DEB464);oc_scalar(&r,0,MEM32(r.d+0x334));r.x[0].f32[0]+=MEMF(r.si+8);
                oc_scalar(&r,1,MEM32(frame+0x50));oc_float_cmp(&r,r.x[1].f32[0],r.x[0].f32[0]);
                if(oc_above(&r)){r.x[0]=(xmm_t){0};r.di=1;}
                else {oc_scalar(&r,0,MEM32(0x17343D8));r.di=0;oc_test(&r,0,0);}
                MEM32(r.si+16)=r.x[0].u32[0];
            }
        }
        r.c=MEM32(frame+0x18);r.a=r.si+4;PUSH32(r.sp,r.a);r.c=oc_add(&r,r.c,0x38);oc_call(&r,sub_0117AA10);
    }
    /* Decide whether to issue an individual or grouped query for the next frame. */
    oc_scalar(&r,0,MEM32(frame+0x50));MEM32(r.si+12)=r.x[0].u32[0];oc_cmp(&r,MEM32(r.bp+0x59C),0);
    int schedule=MEM32(r.bp+0x59C)==0;
    if(schedule){r.a=MEM32(frame+0x18);oc_cmp(&r,MEM32(r.a+4),0);schedule=MEM32(r.a+4)==0;}
    if(schedule){oc_cmp(&r,MEM32(r.a+0x1D0),0);schedule=MEM32(r.a+0x1D0)==0;}
    int force_visible=0;
    if(schedule){
        r.b=MEM32(frame+0x48);r.c=r.b+16;r.a=frame+0x1C;
        PUSH32(r.sp,RECOMP_RETADDR);occlusion_bounds(&r);oc_flags_out(&r);
        r.c=MEM32(r.bp+0x1B8);oc_test(&r,r.c,r.c);
        int outside=0;
        if(r.c){
            fp_push((double)MEMF(r.bp+0x1C0));g_st[0]*=(double)MEMF(frame+0x20);
            fp_push((double)MEMF(r.bp+0x1C4));g_st[0]*=(double)MEMF(frame+0x24);double v=fp_pop();g_st[0]+=v;
            fp_push((double)MEMF(r.bp+0x1BC));g_st[0]*=(double)MEMF(frame+0x1C);v=fp_pop();g_st[0]+=v;g_st[0]-=(double)MEMF(r.bp+0x1C8);
            fp_push((double)MEMF(r.bp+0x1C0));g_st[0]*=(double)MEMF(frame+0x2C);g_st[0]=fabs(g_st[0]);
            fp_push((double)MEMF(r.bp+0x1C4));g_st[0]*=(double)MEMF(frame+0x30);g_st[0]=fabs(g_st[0]);v=fp_pop();g_st[0]+=v;
            fp_push((double)MEMF(r.bp+0x1BC));g_st[0]*=(double)MEMF(frame+0x28);g_st[0]=fabs(g_st[0]);v=fp_pop();g_st[0]+=v;g_st[0]=-g_st[0];
            oc_float_cmp(&r,g_st[0],g_st[1]);fp_pop();fp_pop();outside=oc_above(&r);
            if(!outside){oc_test(&r,r.c,r.c);force_visible=r.c!=0;}
        }
        if(!outside&&!force_visible){
            oc_scalar(&r,0,MEM32(0x175B5F8));oc_float_cmp(&r,r.x[0].f32[0],MEMF(frame+0x34));
            force_visible=!oc_above(&r);
        }
        if(!force_visible){
            oc_byte(&r,MEM8(r.b+0x34),1);
            int grouped=0,submit=1;
            if(MEM8(r.b+0x34)&1){
                oc_test(&r,r.di,r.di);grouped=r.di!=0;
                if(!grouped){
                    oc_byte(&r,MEM8(0x1DF8BDC),1);
                    if(!(MEM8(0x1DF8BDC)&1)){MEM32(0x1DF8BDC)|=1;oc_test(&r,MEM32(0x1DF8BDC),MEM32(0x1DF8BDC));MEM32(0x1DF8BD8)=r.di;}
                    oc_cmp(&r,MEM32(frame+0x14),0);
                    if(MEM32(frame+0x14)){
                        r.c=0x1DF8BD8;oc_call(&r,sub_005D75E0);r.c=MEM32(0x1DEB464);fp_push((double)MEMF(r.c+0x338));
                        oc_float_cmp(&r,g_st[0],g_st[1]);fp_pop();fp_pop();submit=oc_above(&r);
                    }
                }
            }
            if(submit){
                if(grouped){r.a=frame+0x1C;PUSH32(r.sp,r.a);r.c=r.bp+0x630;oc_backedge(&r);}
                else {r.d=frame+0x1C;PUSH32(r.sp,r.d);r.c=r.bp+0x608;}
                oc_call(&r,sub_0117AAB0);PUSH32(r.sp,r.a);r.c=r.si+4;oc_call(&r,sub_01172010);
            }
        }
    }
    if(force_visible){r.di=0;oc_test(&r,0,0);oc_backedge(&r);}
    else {
        oc_cmp(&r,MEM32(frame+0x14),0);int update=MEM32(frame+0x14)!=0;
        if(!update){r.c=MEM32(frame+0x18);oc_cmp(&r,MEM32(r.c+0x1D0),0);update=MEM32(r.c+0x1D0)!=0;}
        if(update){oc_test(&r,r.di,r.di);update=r.di==0;}
        if(!update)goto finish;
    }
    oc_scalar(&r,0,MEM32(frame+0x50));MEM32(r.si+8)=r.x[0].u32[0];
finish:
    r.a=r.di;r.c=MEM32(frame+0x38);MEM32(FS_BASE)=r.c;r.c=POP32_VAL(r.sp);
    r.di=POP32_VAL(r.sp);r.si=POP32_VAL(r.sp);r.bp=POP32_VAL(r.sp);r.b=POP32_VAL(r.sp);
    r.sp=oc_add(&r,r.sp,0x30);r.sp+=16;oc_write(&r);oc_flags_out(&r);
}

void __wrap_sub_0117AEC0(void)
{
    const int mode=__builtin_expect(render_occlusion_mode>=0,1)?render_occlusion_mode:occlusion_load_mode();
    if(mode==1)primitive_occlusion();else __real_sub_0117AEC0();
}
void __wrap_sub_01178C60(void)
{
    const int mode=__builtin_expect(render_occlusion_mode>=0,1)?render_occlusion_mode:occlusion_load_mode();
    if(mode==1){occlusion_context r;oc_read(&r);occlusion_bounds(&r);oc_write(&r);oc_flags_out(&r);}
    else __real_sub_01178C60();
}
void void_render_occlusion_native_init(void)
{void_dispatch_add(0x0117AEC0u,__wrap_sub_0117AEC0);void_dispatch_add(0x01178C60u,__wrap_sub_01178C60);}
