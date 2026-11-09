/* UE3 bit arrays: inline storage, allocator transitions, fill and deep assignment.
 * Internal operations share a native context. Only engine allocation/copy/fill
 * boundaries materialize guest registers; those callbacks retain their yields.
 */
#include "host.h"
#include "native_mode.inc"

extern void __real_sub_00BF81A0(void), __real_sub_00BF82A0(void);
extern void __real_sub_00BF82F0(void), __real_sub_00BF8340(void);
extern void sub_004055A0(void), sub_012890DE(void), sub_01288FD4(void);

static int render_bitarray_mode=-1;
static __attribute__((cold,noinline)) int bitarray_load_mode(void)
{
    char path[1100];snprintf(path,sizeof path,"%s/render-bitarray-native",void_data_dir());
    FILE *file=fopen(path,"rb");const int value=file?fgetc(file):'0';if(file)fclose(file);
    /* Heap callbacks have side effects. Mode 2 executes the original once. */
    render_bitarray_mode=void_native_mode(value=='0'||value=='2'?0:1);
    void_log("[native-render] bit array storage: mode %d",render_bitarray_mode);
    return render_bitarray_mode;
}

typedef struct { uint32_t a,c,d,b,sp,bp,si,di,fk,fa,fb,fc; } bitarray_context;
static inline void ba_read(bitarray_context *r)
{
    r->a=g_eax;r->c=g_ecx;r->d=g_edx;r->b=g_ebx;r->sp=g_esp;r->bp=g_ebp;r->si=g_esi;r->di=g_edi;
    r->fk=g_flag_k;r->fa=g_flag_a;r->fb=g_flag_b;r->fc=g_flag_cf;
}
static inline void ba_write(const bitarray_context *r)
{
    g_eax=r->a;g_ecx=r->c;g_edx=r->d;g_ebx=r->b;g_esp=r->sp;g_ebp=r->bp;g_esi=r->si;g_edi=r->di;
    g_flag_k=r->fk;g_flag_a=r->fa;g_flag_b=r->fb;g_flag_cf=r->fc;
}
static inline void ba_test(bitarray_context *r,uint32_t a,uint32_t b)
{r->fk=FK_TEST;r->fa=a;r->fb=b;r->fc=0;}
static inline void ba_cmp(bitarray_context *r,uint32_t a,uint32_t b)
{r->fk=FK_CMP;r->fa=a;r->fb=b;r->fc=a<b;}
static inline uint32_t ba_add(bitarray_context *r,uint32_t a,uint32_t b)
{r->fk=FK_ADD;r->fa=a;r->fb=b;r->fc=a+b<a;return a+b;}
static inline void ba_call(bitarray_context *r,recomp_func_t function)
{PUSH32(r->sp,RECOMP_RETADDR);ba_write(r);function();ba_read(r);}
static inline void ba_virtual(bitarray_context *r,uint32_t address)
{
    /* Resolve sees the pre-call stack, as in RECOMP_ICALL. */
    ba_write(r);recomp_func_t function=void_resolve(address);
    PUSH32(r->sp,RECOMP_RETADDR);g_esp=r->sp;function();ba_read(r);
}
static inline void ba_allocator(bitarray_context *r)
{
    ba_test(r,r->c,r->c);
    if(!r->c){ba_call(r,sub_004055A0);r->c=MEM32(0x1DAC458);}
}
/* Keep the wrapped signed rounding used by the guest, including negative input. */
static inline uint32_t ba_round_numerator(uint32_t bits,uint32_t *sign_correction)
{
    const uint32_t numerator=bits+31u;
    *sign_correction=(int32_t)numerator<0?31u:0;
    return numerator+*sign_correction;
}

static void ba_storage(bitarray_context *r)
{
    PUSH32(r->sp,r->c);PUSH32(r->sp,r->b);PUSH32(r->sp,r->si);
    r->si=r->c;r->b=MEM32(r->si);ba_test(r,r->b,r->b);
    if(!r->b)ba_cmp(r,MEM32(r->si+8),r->b);
    if(r->b||MEM32(r->si+8)){
        r->a=ba_round_numerator(MEM32(r->si+4),&r->d);
        PUSH32(r->sp,r->bp);r->bp=r->a;
        r->a=ba_round_numerator(MEM32(r->si+8),&r->d);
        PUSH32(r->sp,r->di);r->di=(uint32_t)((int32_t)r->a>>5);r->bp=(uint32_t)((int32_t)r->bp>>5);
        ba_cmp(r,r->di,4);
        if((int32_t)r->di<=4){
            /* At most 128 capacity bits fit in the object's four inline words. */
            r->di=r->si+12;ba_cmp(r,r->b,r->di);
            if(r->b!=r->di){
                ba_test(r,r->b,r->b);
                if(r->b){
                    r->a=r->bp*4;PUSH32(r->sp,r->a);PUSH32(r->sp,r->b);PUSH32(r->sp,r->di);
                    ba_call(r,sub_012890DE);
                    r->c=MEM32(0x1DAC458);r->b=MEM32(r->si);r->sp=ba_add(r,r->sp,12);ba_allocator(r);
                    r->d=MEM32(r->c);r->a=MEM32(r->d+12);PUSH32(r->sp,r->b);ba_virtual(r,r->a);
                }
            }
            MEM32(r->si)=r->di;
        }else{
            r->c=MEM32(0x1DAC458);r->a=r->si+12;ba_cmp(r,r->b,r->a);MEM32(r->sp+16)=r->a;
            if(r->b==r->a){
                /* Growing out of inline storage uses Malloc followed by a copy. */
                ba_allocator(r);r->d=MEM32(r->c);r->d=MEM32(r->d+4);
                PUSH32(r->sp,8);r->a=r->di*4;PUSH32(r->sp,r->a);ba_virtual(r,r->d);
                r->d=MEM32(r->sp+16);r->c=r->bp*4;PUSH32(r->sp,r->c);PUSH32(r->sp,r->d);PUSH32(r->sp,r->a);
                MEM32(r->si)=r->a;ba_call(r,sub_012890DE);r->sp=ba_add(r,r->sp,12);
            }else{
                /* An existing heap allocation (or null) stays with Realloc. */
                ba_allocator(r);r->a=MEM32(r->c);r->a=MEM32(r->a+8);
                PUSH32(r->sp,8);r->d=r->di*4;PUSH32(r->sp,r->d);PUSH32(r->sp,r->b);ba_virtual(r,r->a);
                /* The pointer write follows the guest's EDI restore: aliasing is observable. */
                r->di=POP32_VAL(r->sp);MEM32(r->si)=r->a;
                r->bp=POP32_VAL(r->sp);r->si=POP32_VAL(r->sp);r->b=POP32_VAL(r->sp);r->c=POP32_VAL(r->sp);r->sp+=4;
                return;
            }
        }
        r->di=POP32_VAL(r->sp);r->bp=POP32_VAL(r->sp);
    }
    r->si=POP32_VAL(r->sp);r->b=POP32_VAL(r->sp);r->c=POP32_VAL(r->sp);r->sp+=4;
}

static void ba_construct(bitarray_context *r)
{
    r->a=MEM32(r->sp+8);PUSH32(r->sp,r->si);r->si=r->c;
    MEM32(r->si)=0;MEM32(r->si+4)=r->a;MEM32(r->si+8)=r->a;
    PUSH32(r->sp,RECOMP_RETADDR);ba_storage(r);
    r->a=MEM32(r->si+4);ba_test(r,r->a,r->a);
    if(r->a){
        r->a=ba_round_numerator(r->a,&r->d);r->c=MEM32(r->si);
        r->a=(uint32_t)((int32_t)r->a>>5)*4u;PUSH32(r->sp,r->a);
        const uint32_t value=MEM32(r->sp+12)?UINT32_MAX:0;
        ba_test(r,value,255);r->a=value&255;PUSH32(r->sp,r->a);PUSH32(r->sp,r->c);
        ba_call(r,sub_01288FD4);r->sp=ba_add(r,r->sp,12);
    }
    r->a=r->si;r->si=POP32_VAL(r->sp);r->sp+=12;
}

static void ba_destroy(bitarray_context *r)
{
    PUSH32(r->sp,r->si);r->si=r->c;PUSH32(r->sp,r->di);r->di=MEM32(r->si);r->a=r->si+12;
    ba_cmp(r,r->di,r->a);
    if(r->di!=r->a){
        r->c=MEM32(0x1DAC458);ba_allocator(r);r->d=MEM32(r->c);r->a=MEM32(r->d+12);
        PUSH32(r->sp,r->di);ba_virtual(r,r->a);
    }
    r->di=POP32_VAL(r->sp);MEM32(r->si)=0;MEM32(r->si+8)=0;MEM32(r->si+4)=0;
    r->si=POP32_VAL(r->sp);r->sp+=4;
}

static void ba_assign(bitarray_context *r)
{
    PUSH32(r->sp,r->b);r->b=MEM32(r->sp+8);PUSH32(r->sp,r->si);r->si=r->c;ba_cmp(r,r->si,r->b);
    if(r->si!=r->b){
        PUSH32(r->sp,r->di);r->di=MEM32(r->si);r->a=r->si+12;ba_cmp(r,r->di,r->a);
        if(r->di!=r->a){
            r->c=MEM32(0x1DAC458);ba_allocator(r);r->d=MEM32(r->c);r->a=MEM32(r->d+12);
            PUSH32(r->sp,r->di);ba_virtual(r,r->a);
        }
        /* Read the source only after clearing the destination, as required for aliasing. */
        MEM32(r->si+8)=0;MEM32(r->si+4)=0;MEM32(r->si)=0;
        r->a=MEM32(r->b+4);ba_test(r,r->a,r->a);MEM32(r->si+8)=r->a;MEM32(r->si+4)=r->a;
        if(r->a){
            r->a=ba_round_numerator(r->a,&r->d);r->di=(uint32_t)((int32_t)r->a>>5);r->c=r->si;
            r->fc=(r->a>>4)&1u;r->fk=FK_EFLAGS;r->fa=recomp_flags_pack(r->di,r->fc,0,0);r->fb=0;
            PUSH32(r->sp,RECOMP_RETADDR);ba_storage(r);
            r->d=MEM32(r->b);r->a=MEM32(r->si);r->c=r->di*4;
            PUSH32(r->sp,r->c);PUSH32(r->sp,r->d);PUSH32(r->sp,r->a);ba_call(r,sub_012890DE);r->sp=ba_add(r,r->sp,12);
        }
        r->di=POP32_VAL(r->sp);
    }
    r->a=r->si;r->si=POP32_VAL(r->sp);r->b=POP32_VAL(r->sp);r->sp+=8;
}

static inline int ba_native_enabled(void)
{return (__builtin_expect(render_bitarray_mode>=0,1)?render_bitarray_mode:bitarray_load_mode())==1;}
void __wrap_sub_00BF81A0(void)
{if(!ba_native_enabled()){__real_sub_00BF81A0();return;}bitarray_context r;ba_read(&r);ba_storage(&r);ba_write(&r);}
void __wrap_sub_00BF82A0(void)
{if(!ba_native_enabled()){__real_sub_00BF82A0();return;}bitarray_context r;ba_read(&r);ba_construct(&r);ba_write(&r);}
void __wrap_sub_00BF82F0(void)
{if(!ba_native_enabled()){__real_sub_00BF82F0();return;}bitarray_context r;ba_read(&r);ba_destroy(&r);ba_write(&r);}
void __wrap_sub_00BF8340(void)
{if(!ba_native_enabled()){__real_sub_00BF8340();return;}bitarray_context r;ba_read(&r);ba_assign(&r);ba_write(&r);}

void void_render_bitarray_native_init(void)
{
    void_dispatch_add(0x00BF81A0u,__wrap_sub_00BF81A0);void_dispatch_add(0x00BF82A0u,__wrap_sub_00BF82A0);
    void_dispatch_add(0x00BF82F0u,__wrap_sub_00BF82F0);void_dispatch_add(0x00BF8340u,__wrap_sub_00BF8340);
}
