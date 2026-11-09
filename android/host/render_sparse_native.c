/* UE3 sparse set iterator initialization: mask the tail and find its first bit. */
#include "host.h"
#include "native_mode.inc"

extern void __real_sub_005DAF50(void);
static int render_sparse_mode = -1;

static __attribute__((cold,noinline)) int sparse_load_mode(void)
{
    char path[1100];snprintf(path,sizeof path,"%s/render-sparse-native",void_data_dir());
    FILE *f=fopen(path,"rb");const int value=f?fgetc(f):'0';if(f)fclose(f);
    /* The iterator masks shared storage and can yield; mode 2 runs the original once. */
    render_sparse_mode=void_native_mode(value=='0'||value=='2'?0:1);
    void_log("[native-render] sparse iterator: mode %d",render_sparse_mode);
    return render_sparse_mode;
}

typedef struct { uint32_t a,c,d,b,sp,bp,si,di,backedges; } sparse_context;
static inline void sparse_read(sparse_context *r)
{
    r->a=g_eax;r->c=g_ecx;r->d=g_edx;r->b=g_ebx;r->sp=g_esp;
    r->bp=g_ebp;r->si=g_esi;r->di=g_edi;r->backedges=g_backedges;
}
static inline void sparse_write(const sparse_context *r)
{
    g_eax=r->a;g_ecx=r->c;g_edx=r->d;g_ebx=r->b;g_esp=r->sp;
    g_ebp=r->bp;g_esi=r->si;g_edi=r->di;g_backedges=r->backedges;
}
static inline void sparse_backedge(sparse_context *r)
{
    if(++r->backedges>=RECOMP_YIELD_EVERY && recomp_yield_hook){
        r->backedges=0;sparse_write(r);recomp_yield_hook();sparse_read(r);
    }
}

static void sparse_iterator(void)
{
    sparse_context r;sparse_read(&r);
    PUSH32(r.sp,r.c);PUSH32(r.sp,r.bp);PUSH32(r.sp,r.si);r.si=r.c;PUSH32(r.sp,r.di);
    r.di=MEM32(r.sp+0x14);
    MEM32(r.si)=0;MEM32(r.si+4)=UINT32_MAX;
    r.a=MEM32(r.di);MEM32(r.si+8)=r.a;MEM32(r.si+12)=0xFFFFFFE0u;
    /* Preserve the original wrapped addition and signed truncation for malformed extents. */
    const uint32_t rounded=MEM32(r.di+4)+31u;
    r.d=(int32_t)rounded<0?31u:0u;
    r.a=(uint32_t)((int32_t)rounded/32);r.c=r.a;
    MEM32(r.si+16)=r.a;MEM32(r.si+20)=0;MEM32(r.si+24)=0;
    if(r.c){
        r.d=MEM32(r.si+8);r.a=r.d+r.c*4u-4u;
        r.c=r.c*32u-MEM32(r.di+4);
        r.d=UINT32_MAX>>(r.c&31u);
        MEM32(r.a)&=r.d;
    }
    r.a=~MEM32(r.si);MEM32(r.si+20)&=r.a;r.di=32;
    uint32_t flag_a=0,flag_b=0,flag_kind=FK_CMP;
    if(!MEM32(r.si+20)){
        r.a=MEM32(r.si+16);r.c=r.a-1u;r.d=1;
        for(;;){
            MEM32(r.si+4)+=r.d;r.a=MEM32(r.si+4);MEM32(r.si+12)+=r.di;
            flag_a=r.a;flag_b=r.c;
            if((int32_t)r.a>(int32_t)r.c){
                r.di=POP32_VAL(r.sp);MEM32(r.si+20)=0;
                goto finish;
            }
            r.bp=MEM32(r.si+8);r.a=MEM32(r.bp+r.a*4u);
            const int empty=r.a==0;
            MEM32(r.si+20)=r.a;
            /* A yield observes live GPRs and the last published global flags.
             * Its changes to guest memory/registers cannot change this pending TEST. */
            sparse_backedge(&r);
            if(!empty)break;
        }
    }
    r.c=MEM32(r.si+20);r.a=r.c & (0u-r.c);
    MEM32(r.si)=r.a;MEM32(r.sp+12)=r.a;
    const int selected_bit=r.a!=0;
    if(selected_bit){
        const uint32_t selected=MEM32(r.sp+12);
        if(selected)r.a=31u-(uint32_t)__builtin_clz(selected);
        MEM32(r.sp+20)=r.a;
        r.c=MEM32(r.si+12);r.a=31u-MEM32(r.sp+20);
        r.di=POP32_VAL(r.sp);
    }else{
        r.c=MEM32(r.si+12);r.a=r.di;
        /* The zero-bit path pops EDI after forming the outgoing ADD operands. */
    }
    r.c-=r.a;flag_a=r.c;flag_b=31;flag_kind=FK_ADD;r.c+=31;
    if(!selected_bit)r.di=POP32_VAL(r.sp);
    /* Both return paths write the computed index before restoring ESI/EBP/ECX. */
    MEM32(r.si+24)=r.c;
finish:
    r.a=r.si;r.si=POP32_VAL(r.sp);r.bp=POP32_VAL(r.sp);r.c=POP32_VAL(r.sp);r.sp+=8;
    sparse_write(&r);g_flag_k=flag_kind;g_flag_a=flag_a;g_flag_b=flag_b;
    g_flag_cf=flag_kind==FK_ADD?(flag_a+flag_b<flag_a):(flag_a<flag_b);
}

void __wrap_sub_005DAF50(void)
{
    const int mode=__builtin_expect(render_sparse_mode>=0,1)?render_sparse_mode:sparse_load_mode();
    if(mode==1)sparse_iterator();else __real_sub_005DAF50();
}

void void_render_sparse_native_init(void)
{void_dispatch_add(0x005DAF50u,__wrap_sub_005DAF50);}
