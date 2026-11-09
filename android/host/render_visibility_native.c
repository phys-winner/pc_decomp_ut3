/* Native UE3 view initialization and connected primitive/light visibility traversal. */
#include "host.h"
#include "native_mode.inc"

extern void __real_sub_00F01CF0(void);
#define VIS_EXTERN(a) extern void sub_##a(void)
VIS_EXTERN(00402780); VIS_EXTERN(004050E0); VIS_EXTERN(00405190); VIS_EXTERN(004055A0);
VIS_EXTERN(00486030); VIS_EXTERN(005DAF50); VIS_EXTERN(00B4F1F0); VIS_EXTERN(00B55380);
VIS_EXTERN(00BF82A0); VIS_EXTERN(00BF82F0); VIS_EXTERN(00BF8340); VIS_EXTERN(00E5B1C0);
VIS_EXTERN(00EFA7B0); VIS_EXTERN(00EFF830); VIS_EXTERN(01105580); VIS_EXTERN(0117AEC0);
VIS_EXTERN(0117C3B0); VIS_EXTERN(0119F420); VIS_EXTERN(011AA010); VIS_EXTERN(011CA6A0);
VIS_EXTERN(011CA9D0); VIS_EXTERN(011CFA90); VIS_EXTERN(01288FD4); VIS_EXTERN(012890E4);
#undef VIS_EXTERN

static int visibility_mode = -1;
static __attribute__((cold,noinline)) int visibility_load_mode(void)
{
    char path[1100]; snprintf(path,sizeof path,"%s/render-visibility-native",void_data_dir());
    FILE *f=fopen(path,"rb"); int value=f?fgetc(f):'0'; if(f)fclose(f);
    /* Query results and renderer callbacks have side effects; mode 2 runs only once. */
    visibility_mode=void_native_mode(value=='0'||value=='2'?0:1);
    void_log("[native-render] connected visibility: mode %d",visibility_mode);
    return visibility_mode;
}

/* Keep pending conditions local across yields; register storage is already global. */
typedef struct {
    uint32_t fk,fa,fb,fc;
} visibility_flags;
static inline void vis_flags_in(visibility_flags *r)
{r->fk=g_flag_k;r->fa=g_flag_a;r->fb=g_flag_b;r->fc=g_flag_cf;}
static inline void vis_flags_out(const visibility_flags *r)
{g_flag_k=r->fk;g_flag_a=r->fa;g_flag_b=r->fb;g_flag_cf=r->fc;}
static inline void vis_test(visibility_flags *r,uint32_t a,uint32_t b)
{r->fk=FK_TEST;r->fa=a;r->fb=b;r->fc=0;}
static inline void vis_byte(visibility_flags *r,uint32_t a,uint32_t b)
{r->fk=FK_NARROW(FK_TEST,24);r->fa=a<<24;r->fb=b<<24;r->fc=0;}
static inline void vis_cmp(visibility_flags *r,uint32_t a,uint32_t b)
{r->fk=FK_CMP;r->fa=a;r->fb=b;r->fc=a<b;}
static inline uint32_t vis_add(visibility_flags *r,uint32_t a,uint32_t b)
{r->fk=FK_ADD;r->fa=a;r->fb=b;r->fc=a+b<a;return a+b;}
static inline uint32_t vis_sub(visibility_flags *r,uint32_t a,uint32_t b)
{vis_cmp(r,a,b);return a-b;}
static inline void vis_float_cmp(visibility_flags *r,float a,float b)
{r->fk=FK_EFLAGS;r->fa=SSE_COMI(a,b);r->fb=0;r->fc=r->fa&1u;}
static inline int vis_above(const visibility_flags *r){return !(r->fa&0x41u);}
static inline int vis_below(const visibility_flags *r){return (r->fa&1u)!=0;}
static inline void vis_scalar(unsigned i,uint32_t bits)
{g_xmm[i]=(xmm_t){0};g_xmm[i].u32[0]=bits;}
static inline void vis_call(visibility_flags *r,recomp_func_t fn)
{
    PUSH32(g_esp,RECOMP_RETADDR);vis_flags_out(r);
    fn();vis_flags_in(r);
}
static inline void vis_icall(visibility_flags *r,uint32_t target)
{
    vis_flags_out(r);const uint32_t sp=g_esp;recomp_func_t fn=void_resolve(target);
    g_esp=sp;PUSH32(g_esp,RECOMP_RETADDR);fn();vis_flags_in(r);
}
static inline void vis_backedge(visibility_flags *r)
{
    if(++g_backedges>=RECOMP_YIELD_EVERY && recomp_yield_hook){
        g_backedges=0;recomp_yield_hook();
    }
}
static inline void vis_fp_copy(uint32_t destination,uint32_t source)
{fp_push((double)MEMF(source));MEMF(destination)=(float)fp_pop();}
static inline void vis_copy_words(visibility_flags *r,unsigned count)
{
    for(unsigned i=0;i<count;++i)MEM32(g_edi+i*4u)=MEM32(g_esi+i*4u);
    g_esi+=count*4u;g_edi+=count*4u;g_ecx=0;
}
static inline void vis_bit_write(visibility_flags *r)
{
    /* FBitReference::operator=: same guest adapter, native bit manipulation. */
    PUSH32(g_esp,RECOMP_RETADDR);
    g_eax=MEM32(g_ecx);
    if(MEM32(g_esp+4)){g_ecx=MEM32(g_ecx+4);MEM32(g_eax)|=g_ecx;vis_test(r,MEM32(g_eax),MEM32(g_eax));}
    else {g_edx=~MEM32(g_ecx+4);vis_test(r,MEM32(g_eax),g_edx);MEM32(g_eax)&=g_edx;}
    g_esp+=8;vis_flags_out(r);
}
static inline void vis_bit_read(visibility_flags *r)
{
    PUSH32(g_esp,RECOMP_RETADDR);g_eax=MEM32(g_ecx+4);g_edx=MEM32(g_ecx);
    g_eax=(MEM32(g_edx)&g_eax)!=0;vis_cmp(r,0,0u-g_eax);g_esp+=4;vis_flags_out(r);
}

/* Allocate through the engine allocator; all ownership and failure behavior stay there. */
static void vis_reallocate(visibility_flags *r,uint32_t frame,unsigned variant)
{
    /* variants describe the caller's live scratch registers at the shared allocator boundary. */
    if(variant==0){
        g_ecx=MEM32(0x1DAC458);g_eax*=0x74;vis_test(r,g_ecx,g_ecx);MEM32(frame+0x1C)=g_eax;
    }else if(variant==1){
        g_ecx=MEM32(0x1DAC458);g_eax+=g_eax;g_eax+=g_eax;vis_test(r,g_ecx,g_ecx);MEM32(frame+0x1C)=g_eax;
    }else if(variant==2){
        g_ecx=g_eax*4;MEM32(frame+0x30)=g_ecx;g_ecx=MEM32(0x1DAC458);vis_test(r,g_ecx,g_ecx);
    }else if(variant==3){
        g_ecx=MEM32(0x1DAC458);vis_test(r,g_ecx,g_ecx);g_edx=g_eax*4;MEM32(frame+0x3C)=g_edx;
    }else if(variant==4){
        g_ecx=MEM32(0x1DAC458);g_eax+=g_eax;g_eax+=g_eax;vis_test(r,g_ecx,g_ecx);MEM32(frame+0x44)=g_eax;
    }else {g_ecx=MEM32(0x1DAC458);vis_test(r,g_ecx,g_ecx);g_edi=g_eax*4;}
    if(!g_ecx){vis_call(r,sub_004055A0);g_ecx=MEM32(0x1DAC458);}
    if(variant<=2){
        g_eax=MEM32(frame+(variant==2?0x30:0x1C));g_edx=MEM32(g_ecx);g_edx=MEM32(g_edx+8);
        PUSH32(g_esp,8);PUSH32(g_esp,g_eax);g_eax=MEM32(frame+(variant==2?0x2C:0x38));PUSH32(g_esp,g_eax);vis_icall(r,g_edx);
    }else if(variant==3){
        g_edx=MEM32(frame+0x3C);g_eax=MEM32(g_ecx);g_eax=MEM32(g_eax+8);PUSH32(g_esp,8);PUSH32(g_esp,g_edx);
        g_edx=MEM32(frame+0x30);PUSH32(g_esp,g_edx);vis_icall(r,g_eax);
    }else if(variant==4){
        g_eax=MEM32(frame+0x44);g_edx=MEM32(g_ecx);g_edx=MEM32(g_edx+8);PUSH32(g_esp,8);PUSH32(g_esp,g_eax);PUSH32(g_esp,g_ebx);vis_icall(r,g_edx);
    }else {
        g_edx=MEM32(g_ecx);g_eax=MEM32(g_edx+8);PUSH32(g_esp,8);PUSH32(g_esp,g_edi);PUSH32(g_esp,g_ebx);vis_icall(r,g_eax);
    }
}

static void visibility_buffers(visibility_flags *r,uint32_t frame)
{
    g_eax=MEM32(g_edi+0x44);vis_cmp(r,g_eax,g_edx);MEM32(frame+0x34)=g_edx;
    if((int32_t)g_eax<=(int32_t)g_edx)return;
    MEM32(frame+0x24)=0;
    do {
        g_esi=MEM32(g_edi+0x40);g_esi=vis_add(r,g_esi,MEM32(frame+0x24));g_ebx=0;
        vis_cmp(r,MEM32(g_esi+0x4FC),0);
        if((int32_t)MEM32(g_esi+0x4FC)>0)do{
            g_eax=MEM32(g_esi+0x4F8);g_ecx=MEM32(g_eax+g_ebx*4);g_edx=MEM32(g_ecx);g_eax=MEM32(g_edx);vis_icall(r,g_eax);
            g_ebx=vis_add(r,g_ebx,1);vis_cmp(r,g_ebx,MEM32(g_esi+0x4FC));vis_backedge(r);
        }while((int32_t)r->fa<(int32_t)r->fb);
        const uint32_t offsets[]={0x224,0x24C,0x268,0x284};
        for(unsigned group=0;group<4;++group){
            if(group==0 || group==3){g_ecx=MEM32(g_edi);g_eax=MEM32(g_ecx+(group?0x570:0x46C));}
            else if(group==1){g_edx=MEM32(g_edi);g_eax=MEM32(g_edx+0x570);}
            else {g_eax=MEM32(g_edi);g_eax=MEM32(g_eax+0x570);}
            PUSH32(g_esp,g_eax);PUSH32(g_esp,group?0:1);g_ecx=g_esp+0xD8;vis_call(r,sub_00BF82A0);
            g_ecx=g_esi+offsets[group];PUSH32(g_esp,g_eax);MEM8(frame+0x100)=group+1;vis_call(r,sub_00BF8340);
            g_ecx=frame+0xD0;MEM8(frame+0x100)=0;vis_call(r,sub_00BF82F0);
        }
        g_edx=MEM32(g_edi);g_eax=MEM32(g_edx+0x508);g_ebx=g_esi+0x378;PUSH32(g_esp,g_eax);g_ecx=g_ebx;vis_call(r,sub_00EFF830);
        g_eax=MEM32(g_edi);vis_cmp(r,MEM32(g_eax+0x508),0);MEM32(frame+0x18)=0;
        if((int32_t)MEM32(g_eax+0x508)>0)do{
            g_eax=MEM32(g_ebx+4);MEM32(frame+0x48)=g_eax;g_eax=vis_add(r,g_eax,1);
            vis_cmp(r,g_eax,MEM32(g_ebx+8));MEM32(g_ebx+4)=g_eax;
            if((int32_t)g_eax>(int32_t)MEM32(g_ebx+8)){
                PUSH32(g_esp,0x74);g_ecx=g_ebx;vis_call(r,sub_00B4F1F0);g_ecx=MEM32(g_ebx);vis_test(r,g_ecx,g_ecx);
                MEM32(g_ebx+8)=g_eax;MEM32(frame+0x38)=g_ecx;
                if(!g_ecx)vis_test(r,g_eax,g_eax);
                if(g_ecx||g_eax){vis_reallocate(r,frame,0);MEM32(g_ebx)=g_eax;}
            }
            g_eax=MEM32(frame+0x48);g_eax*=0x74;g_eax=vis_add(r,g_eax,MEM32(g_ebx));
            if(g_eax){
                PUSH32(g_esp,0xEFC5F0);PUSH32(g_esp,0xEFC5D0);PUSH32(g_esp,4);MEM32(g_eax)=0;PUSH32(g_esp,0x1C);
                g_eax=vis_add(r,g_eax,4);PUSH32(g_esp,g_eax);vis_call(r,sub_012890E4);
            }
            g_eax=MEM32(frame+0x18);g_ecx=MEM32(g_edi);g_eax=vis_add(r,g_eax,1);
            vis_cmp(r,g_eax,MEM32(g_ecx+0x508));MEM32(frame+0x18)=g_eax;vis_backedge(r);
        }while((int32_t)r->fa<(int32_t)r->fb);
        g_edx=MEM32(g_edi);g_eax=MEM32(g_edx+0x46C);vis_cmp(r,MEM32(g_esi+0x248),g_eax);
        g_ebx=g_esi+0x240;MEM32(g_ebx+4)=0;
        if(MEM32(g_esi+0x248)!=g_eax){
            g_ecx=MEM32(g_ebx);vis_test(r,g_ecx,g_ecx);MEM32(g_ebx+8)=g_eax;MEM32(frame+0x38)=g_ecx;
            if(!g_ecx)vis_test(r,g_eax,g_eax);
            if(g_ecx||g_eax){vis_reallocate(r,frame,1);MEM32(g_ebx)=g_eax;}
        }
        g_eax=MEM32(g_edi);g_eax=MEM32(g_eax+0x46C);PUSH32(g_esp,8);PUSH32(g_esp,4);PUSH32(g_esp,g_eax);
        g_ecx=g_ebx;MEM32(frame+0x1C)=g_eax;vis_call(r,sub_00402780);
        g_ecx=MEM32(frame+0x1C);g_edx=g_ecx*4;g_ecx=MEM32(g_ebx);PUSH32(g_esp,g_edx);g_edx=g_ecx+g_eax*4;
        PUSH32(g_esp,0);PUSH32(g_esp,g_edx);vis_call(r,sub_01288FD4);g_ebx=0;vis_test(r,0,0);g_esp=vis_add(r,g_esp,12);
        vis_cmp(r,MEM32(g_esi+0x104),0);
        if((int32_t)MEM32(g_esi+0x104)>0)do{
            g_eax=MEM32(g_esi+0x100);g_ecx=MEM32(g_eax+g_ebx*4);g_ecx=MEM32(g_ecx+0x20);
            g_eax=(uint32_t)((int32_t)g_ecx/32);g_edx=MEM32(g_esi+0x224);g_eax=g_edx+g_eax*4;g_ecx&=31;g_edx=1u<<g_ecx;
            /* Shift flags are visible to the inlined bit helper only, whose result replaces them. */
            PUSH32(g_esp,0);g_ecx=g_esp+0xC8;MEM32(frame+0xC4)=g_eax;MEM32(frame+0xC8)=g_edx;vis_bit_write(r);
            g_ebx=vis_add(r,g_ebx,1);vis_cmp(r,g_ebx,MEM32(g_esi+0x104));vis_backedge(r);
        }while((int32_t)r->fa<(int32_t)r->fb);
        g_eax=MEM32(frame+0x34);MEM32(frame+0x24)=vis_add(r,MEM32(frame+0x24),0x660);g_eax=vis_add(r,g_eax,1);
        vis_cmp(r,g_eax,MEM32(g_edi+0x44));MEM32(frame+0x34)=g_eax;vis_backedge(r);
    }while((int32_t)r->fa<(int32_t)r->fb);
}

/* Temporal view history, including camera-cut invalidation and motion-blur matrices. */
static void visibility_history(visibility_flags *r,uint32_t frame)
{
    vis_fp_copy(g_edi+0xD4,g_edi+0x28);g_eax=MEM32(g_edi+0x44);g_edx=0;
    MEM32(frame+0x100)=0;MEM32(frame+0x34)=0;vis_cmp(r,g_eax,0);
    if((int32_t)g_eax<=0)return;
    MEM32(frame+0x24)=0;
    do {
        g_esi=MEM32(g_edi+0x40);g_esi=vis_add(r,g_esi,MEM32(frame+0x24));
        g_ecx=MEM32(g_edi+0x1C)&0x80000u;g_ebx=MEM32(g_esi+4);g_eax=g_ecx?1:0;
        MEM32(frame+0x14)=g_esi;
        if(MEM32(0x1DEE008)!=g_edx || MEM32(0x1DF0D90)!=g_edx || g_eax!=g_edx || MEM32(0x1DBDD6C)!=g_edx)
            MEM32(g_esi+0x59C)=MEM32(g_esi+0x598)=1;
        g_eax=1;vis_cmp(r,g_ebx,g_edx);
        if(g_ebx!=g_edx){
            g_ecx=MEM32(g_esi);vis_scalar(2,MEM32(g_ecx+0x24));vis_scalar(1,MEM32(g_ebx+0x168));
            vis_scalar(3,MEM32(0x190E3CC));g_xmm[0]=g_xmm[2];g_xmm[0].f32[0]-=g_xmm[1].f32[0];
            MEM32(frame+0x1C)=g_xmm[0].u32[0];vis_float_cmp(r,g_xmm[3].f32[0],g_xmm[0].f32[0]);
            int cut=vis_above(r);
            if(!cut){vis_scalar(0,MEM32(0x175B600));vis_float_cmp(r,g_xmm[0].f32[0],g_xmm[1].f32[0]);cut=vis_above(r);}
            if(cut){MEM32(frame+0x18)=g_eax;vis_backedge(r);MEM32(g_esi+0x598)=1;}
            else {
                g_eax=MEM32(0x1DEB464);vis_scalar(0,MEM32(g_eax+0x334));g_xmm[0].f32[0]+=g_xmm[1].f32[0];
                vis_float_cmp(r,g_xmm[2].f32[0],g_xmm[0].f32[0]);MEM32(frame+0x18)=g_edx;
                if(!vis_above(r)){
                    fp_push((double)MEMF(g_eax+0x330));g_esp=vis_sub(r,g_esp,8);MEMF(g_esp+4)=(float)fp_pop();
                    g_edx=g_ebx+0x1B0;fp_push((double)MEMF(g_eax+0x32C));g_ecx=g_ebx+0x170;
                    MEMF(g_esp)=(float)fp_pop();PUSH32(g_esp,g_esi);vis_call(r,sub_00EFA7B0);
                    g_esp=vis_add(r,g_esp,12);vis_byte(r,g_eax&255,g_eax&255);
                    if(g_eax&255)MEM32(g_esi+0x598)=1;
                }else MEM32(g_esi+0x598)=1;
            }
            g_eax=MEM32(frame+0x14);g_esi=g_eax+0x50;g_edi=g_ebx+0x170;g_ecx=16;vis_copy_words(r,16);
            vis_scalar(0,MEM32(g_eax+0x190));MEM32(frame+0x4C)=g_xmm[0].u32[0];
            vis_scalar(0,MEM32(g_eax+0x194));g_edx=MEM32(frame+0x4C);MEM32(frame+0x50)=g_xmm[0].u32[0];
            vis_scalar(0,MEM32(g_eax+0x198));g_ecx=MEM32(frame+0x50);MEM32(g_ebx+0x1B0)=g_edx;
            MEM32(frame+0x54)=g_xmm[0].u32[0];g_edx=MEM32(frame+0x54);MEM32(g_ebx+0x1B4)=g_ecx;MEM32(g_ebx+0x1B8)=g_edx;
            vis_cmp(r,MEM32(g_eax+0x594),0);
            if(MEM32(g_eax+0x594)){
                vis_cmp(r,MEM32(frame+0x18),0);int reset=MEM32(frame+0x18)!=0;
                if(!reset){
                    fp_push((double)MEMF(g_eax+0x5F0));g_esp=vis_sub(r,g_esp,8);MEMF(g_esp+4)=(float)fp_pop();
                    g_edx=g_ebx+0x15C;fp_push((double)MEMF(g_eax+0x5EC));g_ecx=g_ebx+0x110;
                    MEMF(g_esp)=(float)fp_pop();PUSH32(g_esp,g_eax);vis_call(r,sub_00EFA7B0);
                    g_esp=vis_add(r,g_esp,12);vis_byte(r,g_eax&255,g_eax&255);reset=(g_eax&255)!=0;
                    if(reset)g_eax=MEM32(frame+0x14);
                }
                if(reset){
                    g_edx=g_eax+0x90;g_esi=g_edx;g_edi=g_ebx+0x90;g_ecx=16;vis_copy_words(r,16);
                    g_esi=g_edx;g_edx=g_eax+0x50;g_edi=g_ebx+0x50;g_ecx=16;vis_copy_words(r,16);
                    g_esi=g_edx;g_edi=g_ebx+0x110;g_ecx=16;vis_copy_words(r,16);
                    g_esi=g_edx;g_edi=g_ebx+0xD0;g_ecx=16;vis_copy_words(r,16);
                    vis_scalar(0,MEM32(g_eax+0x190));MEM32(frame+0x5C)=g_xmm[0].u32[0];
                    vis_scalar(0,MEM32(g_eax+0x194));g_ecx=MEM32(frame+0x5C);MEM32(frame+0x60)=g_xmm[0].u32[0];
                    vis_scalar(0,MEM32(g_eax+0x198));g_edx=MEM32(frame+0x60);MEM32(g_ebx+0x15C)=g_ecx;
                    MEM32(frame+0x64)=g_xmm[0].u32[0];g_ecx=MEM32(frame+0x64);MEM32(g_ebx+0x160)=g_edx;MEM32(g_ebx+0x164)=g_ecx;
                    vis_scalar(0,MEM32(g_eax+0x190));MEM32(frame+0x78)=g_xmm[0].u32[0];
                    vis_scalar(0,MEM32(g_eax+0x194));g_edx=MEM32(frame+0x78);MEM32(frame+0x7C)=g_xmm[0].u32[0];
                    vis_scalar(0,MEM32(g_eax+0x198));g_eax=MEM32(frame+0x7C);MEM32(frame+0x80)=g_xmm[0].u32[0];
                    g_ecx=MEM32(frame+0x80);vis_scalar(0,MEM32(0x17343D8));
                    MEM32(g_ebx+0x150)=g_edx;MEM32(g_ebx+0x154)=g_eax;MEM32(g_ebx+0x158)=g_ecx;MEM32(g_ebx+0x16C)=g_xmm[0].u32[0];
                }else {
                    vis_scalar(1,MEM32(frame+0x1C));vis_float_cmp(r,g_xmm[1].f32[0],MEMF(0x175B600));
                    if(vis_above(r)){
                        vis_cmp(r,MEM32(0x1D1BBC0),0);const int scale=MEM32(0x1D1BBC0)!=0;
                        g_eax=MEM32(g_ebx+0x150);MEM32(g_edx)=g_eax;g_eax=MEM32(g_ebx+0x158);
                        g_esi=g_ebx+0xD0;g_ecx=16;g_edi=g_ebx+0x110;vis_copy_words(r,16);
                        g_ecx=MEM32(g_ebx+0x154);MEM32(g_edx+4)=g_ecx;MEM32(g_edx+8)=g_eax;
                        g_eax=MEM32(frame+0x14);g_edx=g_ebx+0x50;g_esi=g_edx;g_edi=g_ebx+0x90;g_ecx=16;vis_copy_words(r,16);
                        g_esi=g_eax+0x90;g_edi=g_edx;g_ecx=16;vis_copy_words(r,16);
                        g_esi=g_eax+0x50;g_edi=g_ebx+0xD0;g_ecx=16;vis_copy_words(r,16);
                        vis_scalar(0,MEM32(g_eax+0x190));MEM32(frame+0x6C)=g_xmm[0].u32[0];
                        vis_scalar(0,MEM32(g_eax+0x194));g_ecx=MEM32(frame+0x6C);MEM32(frame+0x70)=g_xmm[0].u32[0];
                        vis_scalar(0,MEM32(g_eax+0x198));g_edx=MEM32(frame+0x70);MEM32(frame+0x74)=g_xmm[0].u32[0];
                        g_eax=MEM32(frame+0x74);vis_scalar(0,MEM32(0x17343D8));
                        MEM32(g_ebx+0x150)=g_ecx;MEM32(g_ebx+0x154)=g_edx;MEM32(g_ebx+0x158)=g_eax;
                        if(scale){g_xmm[1].f32[0]*=MEMF(0x175D318);g_xmm[0].f32[0]/=g_xmm[1].f32[0];}
                        MEM32(g_ebx+0x16C)=g_xmm[0].u32[0];
                    }
                }
                g_edx=g_ebx+0x90;PUSH32(g_esp,g_edx);g_eax=g_esp+0x88;PUSH32(g_esp,g_eax);g_ecx=g_ebx+0x110;
                vis_call(r,sub_00486030);g_edi=MEM32(frame+0x14);g_esi=g_eax;g_eax=MEM32(frame+0x14);
                g_edi=vis_add(r,g_edi,0x5A0);g_ecx=16;vis_copy_words(r,16);
            }
            fp_push((double)MEMF(g_ebx+0x168));g_ecx=MEM32(frame+0x28);g_esi=MEM32(frame+0x14);
            MEMF(g_ecx+0xD4)=(float)fp_pop();g_edx=MEM32(g_eax);fp_push((double)MEMF(g_edx+0x24));g_edi=g_ecx;
            MEMF(g_ebx+0x168)=(float)fp_pop();g_edx=0;vis_test(r,0,0);
        }
        g_eax=MEM32(frame+0x34);MEM32(frame+0x24)=vis_add(r,MEM32(frame+0x24),0x660);
        g_eax=vis_add(r,g_eax,1);MEM32(g_esi+0x204)=g_edx;vis_cmp(r,g_eax,MEM32(g_edi+0x44));
        MEM32(frame+0x34)=g_eax;vis_backedge(r);
    }while((int32_t)r->fa<(int32_t)r->fb);
}

/* Sparse TSet iteration: consume the current bit, skip empty words, select the next bit. */
static int vis_sparse_next(visibility_flags *r,uint32_t frame,int lights)
{
    g_eax=~MEM32(frame+0x88);g_ecx=MEM32(frame+0x9C);vis_test(r,g_ecx,g_eax);g_ecx&=g_eax;MEM32(frame+0x9C)=g_ecx;
    if(!g_ecx){
        if(lights){g_eax=MEM32(frame+0x98)-1;g_edx=MEM32(frame+0x8C);}
        else {g_edx=MEM32(frame+0x98)-1;g_eax=MEM32(frame+0x8C);}
        for(;;){
            MEM32(frame+0x94)=vis_add(r,MEM32(frame+0x94),32);
            if(lights){g_edx=vis_add(r,g_edx,1);vis_cmp(r,g_edx,g_eax);MEM32(frame+0x8C)=g_edx;}
            else {g_eax=vis_add(r,g_eax,1);vis_cmp(r,g_eax,g_edx);MEM32(frame+0x8C)=g_eax;}
            if((int32_t)r->fa>(int32_t)r->fb)return 0;
            g_ecx=MEM32(frame+0x90);g_ecx=MEM32(g_ecx+(lights?g_edx:g_eax)*4);vis_test(r,g_ecx,g_ecx);MEM32(frame+0x9C)=g_ecx;
            vis_backedge(r);if((r->fa&r->fb)!=0)break;
        }
    }
    g_eax=(g_ecx-1)&g_ecx;g_edx=g_ecx^g_eax;MEM32(frame+0x88)=g_edx;MEM32(frame+0x40)=g_edx;
    const uint32_t index=g_edx?(uint32_t)__builtin_ctz(g_edx):UINT32_MAX;
    if(lights){
        if(g_edx){g_eax=index;MEM32(frame+0x38)=g_eax;g_eax=31-MEM32(frame+0x38);}else g_eax=32;
        g_ebx=MEM32(frame+0x94)-g_eax+31;vis_test(r,g_ecx,g_ecx);vis_backedge(r);
    }else{
        if(g_edx){g_eax=index;MEM32(frame+0x1C)=g_eax;g_edx=31-MEM32(frame+0x1C);}else g_edx=32;
        g_eax=MEM32(frame+0x94)-g_edx+31;vis_test(r,g_ecx,g_ecx);MEM32(frame+0xA0)=g_eax;vis_backedge(r);
    }
    return (r->fa&r->fb)!=0;
}

/* Append to a primitive pointer array while keeping allocator callbacks observable. */
static void vis_append_pointer(visibility_flags *r,uint32_t frame,unsigned variant)
{
    /* The caller has already selected the array in ESI and saved its insertion index. */
    PUSH32(g_esp,4);g_ecx=g_esi;vis_call(r,sub_00B4F1F0);
    if(variant==2 || variant==3){
        g_ecx=MEM32(g_esi);vis_test(r,g_ecx,g_ecx);MEM32(g_esi+8)=g_eax;MEM32(frame+(variant==2?0x2C:0x30))=g_ecx;
        if(!g_ecx)vis_test(r,g_eax,g_eax);
        if(g_ecx||g_eax){vis_reallocate(r,frame,variant);MEM32(g_esi)=g_eax;}
    }else {
        g_ebx=MEM32(g_esi);vis_test(r,g_ebx,g_ebx);MEM32(g_esi+8)=g_eax;
        if(!g_ebx)vis_test(r,g_eax,g_eax);
        if(g_ebx||g_eax){vis_reallocate(r,frame,variant);if(variant>=5)g_edi=MEM32(frame+(variant==5?0x3C:0x40));MEM32(g_esi)=g_eax;}
    }
}

static void visibility_static_meshes(visibility_flags *r,uint32_t frame)
{
    vis_byte(r,g_eax&255,1);
    if(!(g_eax&1))return;
    g_eax=MEM32(g_ebx);g_esi=0;vis_cmp(r,MEM32(g_eax+0x18),0);
    if((int32_t)MEM32(g_eax+0x18)<=0)return;
    vis_scalar(0,MEM32(frame+0x58));g_ecx=MEM32(g_eax+0x14);
    do{
        g_eax=MEM32(g_ecx+g_esi*4);vis_float_cmp(r,g_xmm[0].f32[0],MEMF(g_eax+0xD0));
        int visible=!vis_below(r);
        if(visible){vis_scalar(1,MEM32(g_eax+0xD4));vis_float_cmp(r,g_xmm[1].f32[0],g_xmm[0].f32[0]);visible=vis_above(r);}
        if(visible){
            g_ecx=MEM32(g_eax+0xE0);g_eax=(uint32_t)((int32_t)g_ecx/32);g_edx=MEM32(g_edi+0x24C);g_eax=g_edx+g_eax*4;
            g_ecx&=31;g_edx=1u<<g_ecx;PUSH32(g_esp,1);g_ecx=g_esp+0x60;MEM32(frame+0x5C)=g_eax;MEM32(frame+0x60)=g_edx;vis_bit_write(r);
            MEM32(g_edi+0x600)=vis_add(r,MEM32(g_edi+0x600),1);vis_scalar(0,MEM32(frame+0x58));
        }
        g_eax=MEM32(g_ebx);g_esi=vis_add(r,g_esi,1);vis_cmp(r,g_esi,MEM32(g_eax+0x18));vis_backedge(r);
        /* The original retains ECX from the bit helper until the next loop's reload. */
        if((int32_t)r->fa<(int32_t)r->fb)g_ecx=MEM32(g_eax+0x14);
    }while((int32_t)r->fa<(int32_t)r->fb);
}

static void visibility_translucency(visibility_flags *r,uint32_t frame)
{
    g_eax=MEM32(frame+0x20);g_eax=MEM32(g_eax);vis_test(r,g_eax,0x200);
    int any=(g_eax&0x200)!=0;
    if(!any){vis_byte(r,g_eax&255,8);any=(g_eax&8)!=0;}
    if(!any)return;
    g_ebx=0;vis_test(r,0,0);
    do{
        vis_cmp(r,g_ebx,3);
        if(g_ebx<=3){
            if(g_ebx==1){g_edx=MEM32(frame+0x20);g_ecx=MEM32(g_edx);}
            else if(g_ebx==2){g_eax=MEM32(frame+0x20);g_ecx=MEM32(g_eax);}
            else {g_ecx=MEM32(frame+0x20);g_ecx=MEM32(g_ecx);}
            g_eax=(g_ecx>>(4+g_ebx))&1;vis_cmp(r,g_eax,1);
            if(g_eax==1){
                vis_byte(r,g_ecx&255,8);
                if(g_ecx&8){
                    g_edx=MEM32(frame+0x14);g_eax=MEM32(g_edx);g_ecx=g_ebx*3+0xAE;g_esi=g_edi+g_ecx*4;MEM32(frame+0x44)=g_eax;
                    g_eax=MEM32(g_esi+4);MEM32(frame+0x2C)=g_eax;g_eax=vis_add(r,g_eax,1);vis_cmp(r,g_eax,MEM32(g_esi+8));MEM32(g_esi+4)=g_eax;
                    if((int32_t)g_eax>(int32_t)MEM32(g_esi+8))vis_append_pointer(r,frame,3);
                    g_ecx=MEM32(g_esi);g_edx=MEM32(frame+0x2C);g_eax=g_ecx+g_edx*4;vis_test(r,g_eax,g_eax);
                    if(g_eax){g_ecx=MEM32(frame+0x44);MEM32(g_eax)=g_ecx;}
                }
                g_edx=MEM32(frame+0x20);g_eax=MEM32(g_edx);vis_test(r,g_eax,0x200);
                if(g_eax&0x200){
                    g_esi=MEM32(frame+0x14);g_eax>>=12;vis_test(r,g_eax,1);g_eax&=1;PUSH32(g_esp,g_eax);g_eax=MEM32(g_esi);PUSH32(g_esp,g_edi);
                    g_ecx=g_ebx*3+0x5D;PUSH32(g_esp,g_eax);g_ecx=g_edi+g_ecx*8;vis_call(r,sub_011CA6A0);
                    g_edx=MEM32(frame+0x20);vis_test(r,MEM32(g_edx),0x400);
                    if(MEM32(g_edx)&0x400){g_eax=MEM32(g_esi);PUSH32(g_esp,g_edi);g_ecx=g_ebx*3+0xD2;PUSH32(g_esp,g_eax);g_ecx=g_edi+g_ecx*4;vis_call(r,sub_011CFA90);}
                }
            }
        }
        g_ebx=vis_add(r,g_ebx,1);vis_cmp(r,g_ebx,4);vis_backedge(r);
    }while(r->fa<r->fb);
}

static void visibility_light_links(visibility_flags *r,uint32_t frame)
{
    g_edx=MEM32(frame+0x14);g_eax=MEM32(g_edx);g_eax=MEM32(g_eax+0x6C);vis_test(r,g_eax,g_eax);MEM32(frame+0x30)=g_eax;
    if(!g_eax)return;
    do{
        g_ecx=MEM32(frame+0x20);vis_test(r,MEM32(g_ecx),0x2000);int eligible=0;
        if(MEM32(g_ecx)&0x2000){g_edx=MEM32(frame+0x30);g_eax=MEM32(g_edx+0xC);vis_cmp(r,(uint32_t)MEM8(g_eax+0x114)<<24,1u<<24);r->fk=FK_NARROW(FK_CMP,24);eligible=MEM8(g_eax+0x114)==1;}
        if(!eligible){
            g_eax=MEM32(frame+0x30);vis_byte(r,MEM8(g_eax+0x14),2);
            if(!(MEM8(g_eax+0x14)&2)){g_ecx=g_eax;g_eax=MEM32(g_ecx+0xC);vis_cmp(r,(uint32_t)MEM8(g_eax+0x114)<<24,0);r->fk=FK_NARROW(FK_CMP,24);eligible=MEM8(g_eax+0x114)!=0;}
        }
        if(eligible){
            g_edx=MEM32(frame+0x30);g_eax=MEM32(g_edx+8);g_ecx=MEM32(frame+0x68);g_eax*=0x74;g_eax=vis_add(r,g_eax,MEM32(g_ecx+0x378));
            MEM32(frame+0x2C)=0;MEM32(frame+0x44)=g_eax;
            do{
                vis_cmp(r,MEM32(frame+0x2C),3);
                if(MEM32(frame+0x2C)<=3){
                    const unsigned pass=MEM32(frame+0x2C);g_edx=pass;
                    if(pass==1){g_ecx=MEM32(frame+0x20);g_eax=MEM32(g_ecx);}
                    else if(pass==2){g_edx=MEM32(frame+0x20);g_eax=MEM32(g_edx);}
                    else {g_eax=MEM32(frame+0x20);g_eax=MEM32(g_eax);}
                    g_eax>>=4+pass;g_eax&=1;vis_test(r,g_eax,g_eax);
                    if(g_eax){
                        g_eax=MEM32(frame+0x2C);g_edx=MEM32(frame+0x44);g_ecx=g_eax*7;g_eax=MEM32(frame+0x20);g_esi=g_edx+g_ecx*4;
                        MEM32(g_esi+0x1C)=1;vis_byte(r,MEM8(g_eax),2);MEM32(frame+0x40)=g_esi;
                        if(MEM8(g_eax)&2){
                            g_edi=MEM32(g_esi+8);g_esi=vis_add(r,g_esi,4);g_eax=g_edi+1;vis_cmp(r,g_eax,MEM32(g_esi+8));
                            MEM32(frame+0x3C)=g_edi;MEM32(g_esi+4)=g_eax;
                            if((int32_t)g_eax>(int32_t)MEM32(g_esi+8))vis_append_pointer(r,frame,5);
                            g_ecx=MEM32(g_esi);g_eax=g_ecx+g_edi*4;vis_test(r,g_eax,g_eax);
                            if(g_eax){g_edx=MEM32(frame+0x14);g_ecx=MEM32(g_edx);MEM32(g_eax)=g_ecx;}
                        }
                        vis_cmp(r,MEM32(frame+0xCC),0);
                        if(MEM32(frame+0xCC)){
                            g_esi=MEM32(frame+0x40);g_edi=MEM32(g_esi+0x14);g_esi=vis_add(r,g_esi,0x10);g_eax=g_edi+1;
                            vis_cmp(r,g_eax,MEM32(g_esi+8));MEM32(frame+0x40)=g_edi;MEM32(g_esi+4)=g_eax;
                            if((int32_t)g_eax>(int32_t)MEM32(g_esi+8))vis_append_pointer(r,frame,6);
                            g_ecx=MEM32(g_esi);g_eax=g_ecx+g_edi*4;vis_test(r,g_eax,g_eax);
                            if(g_eax){g_edx=MEM32(frame+0x14);g_ecx=MEM32(g_edx);MEM32(g_eax)=g_ecx;}
                            g_edx=MEM32(frame+0x14);g_ecx=MEM32(g_edx+4);g_eax=MEM32(g_ecx);g_edx=MEM32(frame+0x2C);g_eax=MEM32(g_eax+0x10);
                            PUSH32(g_esp,g_edx);vis_icall(r,g_eax);
                        }
                    }
                }
                g_eax=MEM32(frame+0x2C);g_eax=vis_add(r,g_eax,1);vis_cmp(r,g_eax,4);MEM32(frame+0x2C)=g_eax;vis_backedge(r);
            }while(r->fa<r->fb);
        }
        g_ecx=MEM32(frame+0x30);g_eax=MEM32(g_ecx+0x24);vis_test(r,g_eax,g_eax);MEM32(frame+0x30)=g_eax;vis_backedge(r);
    }while((r->fa&r->fb)!=0);
    g_edi=MEM32(frame+0x68);
}

static int visibility_allowed(visibility_flags *r,uint32_t frame)
{
    /* Optional per-view actor inclusion sets. Collision chains retain their original order. */
    vis_test(r,g_esi,g_esi);if(!g_esi)return 1;
    g_eax=MEM32(g_esi+4);vis_test(r,g_eax,g_eax);
    if(g_eax){
        g_ecx=MEM32(frame+0x14);g_ebx=MEM32(g_ecx+8);g_ecx=MEM32(g_eax+0x1C4);vis_test(r,g_ecx,g_ecx);if(!g_ecx)return 0;
        vis_test(r,g_ebx,g_ebx);g_edx=g_ebx?MEM32(g_ebx+4):0;
        g_eax=MEM32(g_eax+0x1C8)-1;g_eax&=g_edx;g_eax=MEM32(g_ecx+g_eax*4);vis_test(r,g_eax,g_eax);if(!g_eax)return 0;
        for(;;){vis_cmp(r,MEM32(g_eax),g_ebx);if(MEM32(g_eax)==g_ebx)break;
            g_eax=MEM32(g_eax+8);vis_test(r,g_eax,g_eax);vis_backedge(r);if(!(r->fa&r->fb))return 0;}
    }
    vis_cmp(r,MEM32(g_esi+0x1D0),0);if(!MEM32(g_esi+0x1D0))return 1;
    g_eax=MEM32(g_esi+0x1DC);vis_test(r,g_eax,g_eax);g_ecx=MEM32(frame+0x14);g_edx=MEM32(g_ecx+8);if(!g_eax)return 0;
    vis_test(r,g_edx,g_edx);g_ecx=g_edx?MEM32(g_edx+4):0;
    g_ebx=MEM32(g_esi+0x1E0)-1;g_ebx&=g_ecx;g_eax=MEM32(g_eax+g_ebx*4);vis_test(r,g_eax,g_eax);if(!g_eax)return 0;
    for(;;){vis_cmp(r,MEM32(g_eax),g_edx);if(MEM32(g_eax)==g_edx)return 1;
        g_eax=MEM32(g_eax+8);vis_test(r,g_eax,g_eax);vis_backedge(r);if(!(r->fa&r->fb))return 0;}
}

static inline float vis_distance_sum(float x2,float y2,float z2)
{
#if defined(__aarch64__)
    /* Match the lifted ARM operand priority for distinct axis NaN payloads. */
    float sum;
    __asm__("fadd %s0, %s2, %s1\n\tfadd %s0, %s3, %s0"
            : "=&w"(sum) : "w"(x2), "w"(y2), "w"(z2));
    return sum;
#else
    return (x2+y2)+z2;
#endif
}
static int visibility_distance(visibility_flags *r,uint32_t frame)
{
    g_xmm[0]=(xmm_t){0};vis_scalar(1,MEM32(g_edi+0x19C));vis_float_cmp(r,g_xmm[1].f32[0],0);
    MEM32(frame+0x58)=0;if(!vis_above(r))return 1;
    g_eax=MEM32(frame+0x14);
    const float dx=MEMF(g_eax+0x10)-MEMF(g_edi+0x190),dy=MEMF(g_eax+0x14)-MEMF(g_edi+0x194),dz=MEMF(g_eax+0x18)-MEMF(g_edi+0x198);
    vis_scalar(0,sse_u32(dx));vis_scalar(1,sse_u32(dy));vis_scalar(2,sse_u32(dz));vis_scalar(3,MEM32(g_edi+0x200));
    g_xmm[4]=g_xmm[1];g_xmm[4].f32[0]*=g_xmm[1].f32[0];g_xmm[0].f32[0]*=g_xmm[0].f32[0];
    g_xmm[1]=g_xmm[2];g_xmm[1].f32[0]*=g_xmm[2].f32[0];
    g_xmm[0].f32[0]=vis_distance_sum(g_xmm[0].f32[0],g_xmm[4].f32[0],g_xmm[1].f32[0]);
    vis_scalar(1,MEM32(g_eax+0x2C));g_xmm[0].f32[0]*=g_xmm[3].f32[0];g_xmm[0].f32[0]*=g_xmm[3].f32[0];
    g_xmm[2]=g_xmm[1];g_xmm[2].f32[0]*=g_xmm[1].f32[0];vis_float_cmp(r,g_xmm[0].f32[0],g_xmm[2].f32[0]);MEM32(frame+0x58)=g_xmm[0].u32[0];
    if(vis_above(r)){MEM32(frame+0x18)=vis_add(r,MEM32(frame+0x18),1);return 0;}return 1;
}

static void visibility_primitive_view(visibility_flags *r,uint32_t frame)
{
    if(!visibility_allowed(r,frame)||!visibility_distance(r,frame))return;
    g_eax=MEM32(frame+0x8C);g_edx=MEM32(g_edi+0x224);g_eax+=g_eax;g_eax+=g_eax;g_edx=vis_add(r,g_edx,g_eax);
    g_eax=MEM32(frame+0x88);g_ecx=frame+0x78;MEM32(frame+0x78)=g_edx;MEM32(frame+0x7C)=g_eax;vis_bit_read(r);vis_test(r,g_eax,g_eax);if(!g_eax)return;
    g_eax=MEM32(frame+0x14);fp_push((double)MEMF(g_eax+0x28));PUSH32(g_esp,g_ecx);g_eax=vis_add(r,g_eax,0x10);
    MEMF(g_esp)=(float)fp_pop();PUSH32(g_esp,g_eax);g_ecx=g_edi+0x1A0;MEM32(frame+0x48)=0;vis_call(r,sub_0117C3B0);vis_test(r,g_eax,g_eax);
    if(g_eax){
        g_ebx=MEM32(frame+0x14);g_eax=MEM32(g_ebx);g_eax=MEM32(g_ebx+4);
        g_edx=MEM32(frame+0x28);g_eax=MEM32(g_edx+0x18)&0x800u;g_ecx=0;vis_test(r,g_eax,g_eax);
        int occluded=0;
        if(!g_eax){
            vis_test(r,g_esi,g_esi);
            if(g_esi){fp_push((double)MEMF(g_edx+0x28));PUSH32(g_esp,g_ecx);MEMF(g_esp)=(float)fp_pop();PUSH32(g_esp,g_edi);PUSH32(g_esp,g_ebx);
                g_ecx=g_esi;vis_call(r,sub_0117AEC0);vis_test(r,g_eax,g_eax);occluded=g_eax!=0;}
        }
        if(occluded)MEM32(frame+0x34)=vis_add(r,MEM32(frame+0x34),1);
        else {
            g_ecx=MEM32(g_edi+0x240);g_edx=MEM32(frame+0xA0);g_esi=g_ecx+g_edx*4;g_ecx=MEM32(g_ebx+4);g_eax=MEM32(g_ecx);g_eax=MEM32(g_eax+0x2C);
            PUSH32(g_esp,g_edi);g_edx=g_esp+0xC8;PUSH32(g_esp,g_edx);MEM32(frame+0x20)=g_esi;vis_icall(r,g_eax);
            g_eax=MEM32(g_eax);vis_test(r,g_eax,0x8000);MEM32(g_esi)=g_eax;if(g_eax&0x8000)MEM32(frame+0x38)=1;
            visibility_static_meshes(r,frame);
            g_eax=MEM32(frame+0x20);vis_byte(r,MEM8(g_eax),2);
            if(MEM8(g_eax)&2){
                g_eax=MEM32(g_edi+0x2A4);g_esi=g_edi+0x2A0;MEM32(frame+0x3C)=g_eax;g_eax=vis_add(r,g_eax,1);
                vis_cmp(r,g_eax,MEM32(g_esi+8));MEM32(g_esi+4)=g_eax;if((int32_t)g_eax>(int32_t)MEM32(g_esi+8))vis_append_pointer(r,frame,2);
                g_eax=MEM32(g_esi);g_ecx=MEM32(frame+0x3C);g_eax+=g_ecx*4;vis_test(r,g_eax,g_eax);if(g_eax){g_edx=MEM32(g_ebx);MEM32(g_eax)=g_edx;}
                MEM32(g_edi+0x604)=vis_add(r,MEM32(g_edi+0x604),1);
            }
            visibility_translucency(r,frame);
            g_edx=MEM32(frame+0x20);g_eax=MEM32(g_edx);g_ecx=(g_eax>>1)|g_eax;vis_byte(r,g_ecx&255,1);
            if(g_ecx&1){
                g_ecx=MEM32(frame+0x24);g_eax=(uint32_t)((int32_t)g_ecx/32);g_edx=MEM32(frame+0xD0);g_eax=g_edx+g_eax*4;g_ecx&=31;g_edx=1u<<g_ecx;
                PUSH32(g_esp,1);g_ecx=g_esp+0x70;MEM32(frame+0x6C)=g_eax;MEM32(frame+0x70)=g_edx;vis_bit_write(r);
                g_esi=MEM32(frame+0x14);g_ecx=MEM32(g_esi);MEM32(frame+0x48)=1;vis_call(r,sub_01105580);
                g_ecx=MEM32(g_esi+4);PUSH32(g_esp,g_edi);vis_call(r,sub_00E5B1C0);vis_test(r,g_eax,g_eax);MEM32(frame+0xCC)=g_eax;
                if(g_eax){
                    g_eax=MEM32(g_edi+0x2B0);g_esi=g_edi+0x2AC;MEM32(frame+0x3C)=g_eax;g_eax=vis_add(r,g_eax,1);
                    vis_cmp(r,g_eax,MEM32(g_esi+8));MEM32(g_esi+4)=g_eax;if((int32_t)g_eax>(int32_t)MEM32(g_esi+8))vis_append_pointer(r,frame,4);
                    g_eax=MEM32(g_esi);g_ecx=MEM32(frame+0x3C);g_eax+=g_ecx*4;vis_test(r,g_eax,g_eax);if(g_eax){g_edx=MEM32(frame+0x14);g_ecx=MEM32(g_edx);MEM32(g_eax)=g_ecx;}
                }
                visibility_light_links(r,frame);
            }
        }
    }
    g_eax=MEM32(frame+0x8C);g_edx=MEM32(g_edi+0x224);g_ecx=MEM32(frame+0x48);g_eax+=g_eax;g_eax+=g_eax;g_edx=vis_add(r,g_edx,g_eax);g_eax=MEM32(frame+0x88);
    PUSH32(g_esp,g_ecx);g_ecx=g_esp+0x50;MEM32(frame+0x4C)=g_edx;MEM32(frame+0x50)=g_eax;vis_bit_write(r);
}

static void visibility_primitives(visibility_flags *r,uint32_t frame)
{
    g_edi=MEM32(g_edi);g_edi=vis_add(r,g_edi,0x468);MEM32(frame+0x84)=g_edi;g_esi=0;vis_test(r,0,0);
    g_edi=vis_add(r,g_edi,0x18);PUSH32(g_esp,g_edi);g_ecx=g_esp+0x8C;MEM32(frame+0x34)=0;MEM32(frame+0x18)=0;vis_call(r,sub_005DAF50);
    vis_cmp(r,MEM32(frame+0x9C),g_esi);if(MEM32(frame+0x9C)==g_esi)return;
    do{
        g_eax=MEM32(frame+0xA0);g_edx=MEM32(frame+0x84);g_esi=MEM32(frame+0x28);g_ecx=g_eax*7;g_eax=MEM32(g_edx);g_ecx=g_eax+g_ecx*8;
        g_eax=MEM32(g_esi+0x44);PUSH32(g_esp,g_eax);MEM32(frame+0x14)=g_ecx;g_edi=0;vis_test(r,0,0);PUSH32(g_esp,g_edi);g_ecx=g_esp+0xD8;vis_call(r,sub_00BF82A0);
        vis_cmp(r,MEM32(g_esi+0x44),g_edi);MEM8(frame+0x100)=5;MEM32(frame+0x38)=g_edi;MEM32(frame+0x24)=g_edi;
        if((int32_t)MEM32(g_esi+0x44)>(int32_t)g_edi){
            do{
                g_edi=MEM32(frame+0x24);g_edx=MEM32(frame+0x28);g_edi*=0x660;g_edi=vis_add(r,g_edi,MEM32(g_edx+0x40));
                g_esi=MEM32(g_edi+4);vis_test(r,g_esi,g_esi);MEM32(frame+0x68)=g_edi;
                visibility_primitive_view(r,frame);
                g_eax=MEM32(frame+0x24);g_edx=MEM32(frame+0x28);g_eax=vis_add(r,g_eax,1);vis_cmp(r,g_eax,MEM32(g_edx+0x44));MEM32(frame+0x24)=g_eax;vis_backedge(r);
            }while((int32_t)r->fa<(int32_t)r->fb);
            vis_cmp(r,MEM32(frame+0x38),0);
            if(MEM32(frame+0x38)){
                vis_byte(r,MEM8(0x1DF1044),1);
                if(!(MEM8(0x1DF1044)&1)){MEM32(0x1DF1044)|=1;vis_test(r,MEM32(0x1DF1044),MEM32(0x1DF1044));MEM32(0x1DF1040)=0;}
                g_eax=MEM32(frame+0x14);g_ecx=MEM32(g_eax+4);g_eax=MEM32(0x1DF1040);g_edx=MEM32(g_ecx);g_edx=MEM32(g_edx+0x30);
                PUSH32(g_esp,g_eax);g_eax=g_esp+0xD4;PUSH32(g_esp,g_eax);g_eax=MEM32(frame+0x28);g_eax=vis_add(r,g_eax,4);PUSH32(g_esp,g_eax);vis_icall(r,g_edx);
            }
        }
        g_ecx=frame+0xD0;MEM8(frame+0x100)=0;vis_call(r,sub_00BF82F0);
    }while(vis_sparse_next(r,frame,0));
}

static void visibility_stat_lookup(visibility_flags *r,uint32_t key)
{
    g_eax=MEM32(0x1DCE200);vis_test(r,g_eax,g_eax);
    if(g_eax){
        vis_cmp(r,MEM32(0x1DCE1F8),0);
        if((int32_t)MEM32(0x1DCE1F8)>0){
            g_edx=MEM32(0x1DCE204)-1;g_esi=key;vis_test(r,g_edx,g_esi);g_edx&=g_esi;
            g_eax=MEM32(g_eax+g_edx*4);vis_cmp(r,g_eax,UINT32_MAX);
            if(g_eax!=UINT32_MAX){
                g_edx=MEM32(0x1DCE1F4);
                for(;;){
                    g_ecx=g_eax*3;vis_cmp(r,MEM32(g_edx+g_ecx*4+4),g_esi);g_ecx=g_edx+g_ecx*4;
                    if(r->fa==r->fb){g_eax*=3;g_esi=MEM32(g_edx+g_eax*4+8);vis_test(r,g_esi,g_esi);if(g_esi)return;break;}
                    g_eax=MEM32(g_ecx);vis_cmp(r,g_eax,UINT32_MAX);vis_backedge(r);if(r->fa==r->fb)break;
                }
                if(g_esi==0)goto missing;
            }
        }
    }
    g_esi=0;vis_test(r,0,0);
missing:
    PUSH32(g_esp,0x173421C);PUSH32(g_esp,0x6CD);PUSH32(g_esp,0x1734248);PUSH32(g_esp,0x1737A20);
    vis_call(r,sub_00B55380);g_esp=vis_add(r,g_esp,16);
}

static void visibility_stats(visibility_flags *r,uint32_t frame)
{
    visibility_stat_lookup(r,0x77E);g_ecx=MEM32(frame+0x34);g_edi=MEM32(0x172912C);PUSH32(g_esp,g_ecx);
    g_esi=vis_add(r,g_esi,12);PUSH32(g_esp,g_esi);vis_icall(r,g_edi);
    visibility_stat_lookup(r,0x77D);g_ecx=MEM32(frame+0x18);PUSH32(g_esp,g_ecx);g_esi=vis_add(r,g_esi,12);PUSH32(g_esp,g_esi);vis_icall(r,g_edi);
}

static void visibility_lights(visibility_flags *r,uint32_t frame)
{
    g_ebx=MEM32(frame+0x28);vis_cmp(r,MEM32(g_ebx+0x44),0);MEM32(frame+0x1C)=0;
    if((int32_t)MEM32(g_ebx+0x44)>0){
        MEM32(frame+0x18)=0;
        do{
            g_eax=MEM32(g_ebx+0x40);g_eax=vis_add(r,g_eax,MEM32(frame+0x18));g_edi=4;g_esi=g_eax+0x2E8;
            do{g_ecx=g_esi;vis_call(r,sub_011CA9D0);g_esi=vis_add(r,g_esi,0x18);g_edi=vis_sub(r,g_edi,1);vis_backedge(r);}while(r->fa!=r->fb);
            g_eax=MEM32(frame+0x1C);MEM32(frame+0x18)=vis_add(r,MEM32(frame+0x18),0x660);g_eax=vis_add(r,g_eax,1);
            vis_cmp(r,g_eax,MEM32(g_ebx+0x44));MEM32(frame+0x1C)=g_eax;vis_backedge(r);
        }while((int32_t)r->fa<(int32_t)r->fb);
    }
    g_ebx=MEM32(g_ebx);g_ebx=vis_add(r,g_ebx,0x504);MEM32(frame+0x84)=g_ebx;g_ebx=vis_add(r,g_ebx,0x18);
    PUSH32(g_esp,g_ebx);g_ecx=g_esp+0x8C;vis_call(r,sub_005DAF50);vis_cmp(r,MEM32(frame+0x9C),0);
    if(!MEM32(frame+0x9C))return;
    g_ebx=MEM32(frame+0xA0);
    do{
        g_edx=MEM32(frame+0x84);g_eax=g_ebx*48;g_eax=vis_add(r,g_eax,MEM32(g_edx));g_ecx=0;vis_test(r,0,0);
        g_edi=MEM32(g_eax);g_eax=MEM32(frame+0x28);vis_cmp(r,MEM32(g_eax+0x44),g_ecx);MEM32(frame+0x1C)=g_ecx;
        if((int32_t)MEM32(g_eax+0x44)>(int32_t)g_ecx){
            MEM32(frame+0x18)=g_ecx;
            do{
                g_esi=MEM32(g_eax+0x40);g_esi=vis_add(r,g_esi,MEM32(frame+0x18));
                vis_cmp(r,(uint32_t)MEM8(g_edi+0x115)<<24,1u<<24);r->fk=FK_NARROW(FK_CMP,24);
                int relevant=MEM8(g_edi+0x115)==1;
                if(!relevant){
                    g_edx=MEM32(g_esi+0x378);g_ecx=g_ebx;g_eax=0;vis_test(r,0,0);g_ecx=g_ecx*0x74+g_edx+0x1C;
                    for(;;){vis_cmp(r,MEM32(g_ecx),0);if(MEM32(g_ecx)){relevant=1;break;}
                        g_eax=vis_add(r,g_eax,1);g_ecx=vis_add(r,g_ecx,0x1C);vis_cmp(r,g_eax,4);vis_backedge(r);if(r->fa>=r->fb)break;}
                }
                if(relevant){
                    SET_LO8(g_eax,MEM8(g_edi+0x114));vis_cmp(r,(g_eax&255)<<24,2u<<24);r->fk=FK_NARROW(FK_CMP,24);
                    int sphere=(g_eax&255)==2;
                    if(!sphere){vis_cmp(r,(g_eax&255)<<24,3u<<24);r->fk=FK_NARROW(FK_CMP,24);sphere=(g_eax&255)==3;}
                    if(!sphere){g_eax=MEM32(g_edi+0xF0);g_ecx=MEM32(g_esi+0x378);g_eax*=0x74;MEM32(g_eax+g_ecx)=1;}
                    else {
                        g_edx=MEM32(g_edi);vis_scalar(0,MEM32(g_edi+0xB0));vis_scalar(1,MEM32(g_edi+0xB4));vis_scalar(2,MEM32(g_edi+0xB8));
                        g_eax=MEM32(g_edx);g_ecx=g_edi;MEM32(frame+0x4C)=g_xmm[0].u32[0];MEM32(frame+0x50)=g_xmm[1].u32[0];MEM32(frame+0x54)=g_xmm[2].u32[0];
                        vis_icall(r,g_eax);PUSH32(g_esp,g_ecx);g_ecx=g_esp+0x50;MEMF(g_esp)=(float)fp_pop();PUSH32(g_esp,g_ecx);
                        g_ecx=g_esi+0x1A0;vis_call(r,sub_0117C3B0);vis_test(r,g_eax,g_eax);
                        if(g_eax){g_edx=MEM32(g_edi+0xF0);g_eax=MEM32(g_esi+0x378);g_edx*=0x74;MEM32(g_edx+g_eax)=1;}
                    }
                }
                g_ecx=MEM32(frame+0x1C);g_eax=MEM32(frame+0x28);MEM32(frame+0x18)=vis_add(r,MEM32(frame+0x18),0x660);
                g_ecx=vis_add(r,g_ecx,1);vis_cmp(r,g_ecx,MEM32(g_eax+0x44));MEM32(frame+0x1C)=g_ecx;vis_backedge(r);
            }while((int32_t)r->fa<(int32_t)r->fb);
        }
    }while(vis_sparse_next(r,frame,1));
}

static void visibility_pipeline(void)
{
    visibility_flags r;vis_flags_in(&r);
    PUSH32(g_esp,g_ebp);g_ebp=g_esp;g_esp&=~15u;
    PUSH32(g_esp,UINT32_MAX);PUSH32(g_esp,0x16939B0);g_eax=MEM32(FS_BASE);PUSH32(g_esp,g_eax);
    g_esp=vis_sub(&r,g_esp,0xE8);PUSH32(g_esp,g_ebx);PUSH32(g_esp,g_esi);PUSH32(g_esp,g_edi);
    g_eax=MEM32(0x1D324E0)^g_esp;vis_test(&r,g_eax,g_eax);PUSH32(g_esp,g_eax);
    const uint32_t frame=g_esp;g_eax=frame+0xF8;MEM32(FS_BASE)=g_eax;g_edi=g_ecx;MEM32(frame+0x28)=g_edi;
    PUSH32(g_esp,0x76D);g_ecx=g_esp+0xF0;vis_call(&r,sub_004050E0);
    visibility_history(&r,frame);visibility_buffers(&r,frame);visibility_primitives(&r,frame);
    visibility_stats(&r,frame);visibility_lights(&r,frame);
    g_esi=MEM32(frame+0x28);g_eax=MEM32(g_esi+0x18)&0x20;g_ecx=0;vis_test(&r,g_eax,g_eax);
    if(g_eax){vis_cmp(&r,MEM32(0x1DED8CC),g_ecx);if(MEM32(0x1DED8CC)!=g_ecx){g_ecx=g_esi;vis_call(&r,sub_0119F420);}}
    g_ecx=g_esi;vis_call(&r,sub_011AA010);g_ecx=frame+0xEC;MEM32(frame+0x100)=UINT32_MAX;vis_call(&r,sub_00405190);
    g_ecx=MEM32(frame+0xF8);MEM32(FS_BASE)=g_ecx;g_ecx=POP32_VAL(g_esp);g_edi=POP32_VAL(g_esp);g_esi=POP32_VAL(g_esp);g_ebx=POP32_VAL(g_esp);
    g_esp=g_ebp;g_ebp=POP32_VAL(g_esp);g_esp+=4;vis_flags_out(&r);
}

void __wrap_sub_00F01CF0(void)
{
    const int mode=__builtin_expect(visibility_mode>=0,1)?visibility_mode:visibility_load_mode();
    if(mode)visibility_pipeline();else __real_sub_00F01CF0();
}
void void_render_visibility_native_init(void)
{void_dispatch_add(0x00F01CF0u,__wrap_sub_00F01CF0);}
