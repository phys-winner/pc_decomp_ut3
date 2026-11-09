#include "host.h"
#include <assert.h>
#include <sys/mman.h>
#include "../android/host/render_bitarray_native.c"

recomp_ehframe_t *g_eh_frames;
uint32_t g_eh_obj,g_eh_info;
enum { STACK=0x30008,STACK_LO=0x2FF00,STACK_BYTES=0x240,
       DATA=0x100000,DATA_BYTES=0x4000,SELF=DATA+0x40,SOURCE=DATA+0x100,
       HEAP=DATA+0x1000,SOURCE_HEAP=DATA+0x1800,NEW_HEAP=DATA+0x2000,
       ALLOCATOR=DATA+0x3000,VTABLE=DATA+0x3100,
       MALLOC=0x7FF00100,REALLOC=0x7FF00200,FREE=0x7FF00300,YIELD=0x7FF00400,RESOLVE=0x7FF00500 };
typedef struct { uint32_t kind,argument,allocator;void_regs regs;unsigned char data[DATA_BYTES],stack[STACK_BYTES]; } event;
static event expected_events[64];
static unsigned case_number,operation,event_count,expected_count,checking,callback_count;
static uint64_t total_events,total_yields,total_mutations,oversize_requests;
static uint32_t seed=0x76C510E9;
static uint32_t random_u32(void){seed=seed*1664525u+1013904223u;return seed;}
const char *void_data_dir(void){return "/nonexistent";}
void void_log(const char *format,...){(void)format;}
void void_dispatch_add(uint32_t address,recomp_func_t function){(void)address;(void)function;}

static void differences(const void *expected,const void *actual,size_t bytes,const char *label)
{
    const unsigned char *a=expected,*b=actual;unsigned printed=0;
    for(size_t i=0;i<bytes;++i)if(a[i]!=b[i]){
        fprintf(stderr,"%s+%04zx: %02x/%02x\n",label,i,a[i],b[i]);if(++printed==16)break;
    }
}
static void record(uint32_t kind,uint32_t argument)
{
    assert(event_count<64);static event current;memset(&current,0,sizeof current);
    current.kind=kind;current.argument=argument;current.allocator=MEM32(0x1DAC458);void_regs_save(&current.regs);
    memcpy(current.data,GPTR(DATA),DATA_BYTES);memcpy(current.stack,GPTR(STACK_LO),STACK_BYTES);
    if(!checking)expected_events[event_count]=current;
    else if(event_count>=expected_count||memcmp(&expected_events[event_count],&current,sizeof current)){
        fprintf(stderr,"operation %u case %u event %u kind %08x/%08x\n",operation,case_number,event_count,
            event_count<expected_count?expected_events[event_count].kind:0,kind);
        if(event_count<expected_count){
            differences(&expected_events[event_count].regs,&current.regs,sizeof current.regs,"regs");
            differences(expected_events[event_count].data,current.data,DATA_BYTES,"data");
            differences(expected_events[event_count].stack,current.stack,STACK_BYTES,"stack");
        }
        exit(1);
    }
    ++event_count;++total_events;
}
static void yield_probe(void)
{
    record(YIELD,0);++total_yields;
    if(case_number%23==0){MEM32(SELF+4)=7;++total_mutations;}
}
static void clobber(unsigned tag)
{
    g_ecx=0xC0000000u+tag;g_edx=0xD0000000u+tag;
    g_xmm[0].u32[0]=0x3F800000u+tag;g_xmm[7].u32[3]=0xD5000000u+tag;
    g_flag_k=FK_CMP;g_flag_a=tag;g_flag_b=31;g_flag_cf=tag<31;
    if((++callback_count+case_number)%4==0)recomp_yield_hook();
}
static void ret(unsigned args){g_esp+=4+4*args;}
void sub_004055A0(void)
{record(0x004055A0,0);MEM32(0x1DAC458)=ALLOCATOR;g_eax=ALLOCATOR;clobber(1);ret(0);}
static void allocate(void)
{
    record(MALLOC,0);assert(ARG(1)==8);
    memset(GPTR(NEW_HEAP),0x5C,0x400);g_eax=NEW_HEAP;clobber(2);ret(2);
}
static void reallocate(void)
{
    record(REALLOC,0);assert(ARG(2)==8);
    const uint32_t old=ARG(0),bytes=ARG(1);
    memset(GPTR(NEW_HEAP),0x6D,0x400);
    if(old&&bytes<=0x400)memmove(GPTR(NEW_HEAP),GPTR(old),bytes);
    g_eax=operation==0&&case_number%41==0?0:NEW_HEAP;clobber(3);ret(3);
}
static void release(void)
{
    record(FREE,0);uint32_t pointer=ARG(0);
    if(pointer>=HEAP&&pointer<HEAP+0x1000)memset(GPTR(pointer),0xDD,16);
    g_eax=0xABCD1234;clobber(4);ret(1);
}
void sub_012890DE(void)
{
    record(0x012890DE,0);uint32_t destination=ARG(0),source=ARG(1),bytes=ARG(2);
    /* Huge/invalid bit counts exercise ABI arithmetic only, with a bounded callback. */
    if(bytes<=0x400)memmove(GPTR(destination),GPTR(source),bytes);else ++oversize_requests;
    g_eax=destination;clobber(5);ret(0);
}
void sub_01288FD4(void)
{
    record(0x01288FD4,0);uint32_t destination=ARG(0),value=ARG(1),bytes=ARG(2);
    if(bytes<=0x400)memset(GPTR(destination),value,bytes);else ++oversize_requests;
    g_eax=destination;clobber(6);ret(0);
}
recomp_func_t void_resolve(uint32_t address)
{
    record(RESOLVE,address);
    if(address==MALLOC)return allocate;if(address==REALLOC)return reallocate;if(address==FREE)return release;
    fprintf(stderr,"unknown callback %08x\n",address);abort();
}

static void fixture(unsigned n,unsigned op)
{
    case_number=n;operation=op;event_count=callback_count=0;
    for(unsigned i=0;i<DATA_BYTES;i+=4)MEM32(DATA+i)=random_u32();
    memset(GPTR(STACK_LO),0xB7,STACK_BYTES);
    static const uint32_t sizes[]={0,1,31,32,33,127,128,129,255,256,257,1024};
    static const uint32_t extreme[]={0xFFFFFFFFu,0xFFFFFFE0u,0xFFFFFFE1u,0x80000000u,0x7FFFFFFFu,0x7FFFFFE1u};
    const uint32_t size=n<1024?sizes[n%12]:extreme[(n-1024)%6];
    MEM32(SELF)=n%3==0?0:n%3==1?SELF+12:HEAP;
    MEM32(SELF+4)=n<1024?(size<128?size:n%129):size;MEM32(SELF+8)=size;
    MEM32(SOURCE)=n%2?SOURCE+12:SOURCE_HEAP;MEM32(SOURCE+4)=size;MEM32(SOURCE+8)=size;
    MEM32(0x1DAC458)=n%5?ALLOCATOR:0;MEM32(ALLOCATOR)=VTABLE;
    MEM32(VTABLE+4)=MALLOC;MEM32(VTABLE+8)=REALLOC;MEM32(VTABLE+12)=FREE;
    g_eax=random_u32();g_ecx=SELF;g_edx=random_u32();g_ebx=random_u32();g_esp=STACK;
    g_esi=random_u32();g_edi=random_u32();g_ebp=random_u32();g_fs_base=0x1000;g_cur_func=0xAABBCCDD;
    g_flag_k=FK_TEST;g_flag_a=random_u32();g_flag_b=random_u32();g_flag_cf=1;
    g_backedges=n%7;recomp_yield_hook=yield_probe;
    for(unsigned i=0;i<8;++i){g_st[i]=0.25+(double)i;g_st_i64[i]=(int64_t)random_u32();g_mm[i]=random_u32();
        for(unsigned j=0;j<4;++j)g_xmm[i].u32[j]=random_u32();}
    g_fp_top=n%7;MEM32(STACK)=RECOMP_RETADDR;
    if(op==1){ARG(0)=n%4==0?0:n%4==1?1:n%4==2?UINT32_MAX:0x80000000u;ARG(1)=size;}
    if(op==3){
        ARG(0)=n%13==0?SELF:SOURCE;
        if(n%17==0){MEM32(SELF)=SOURCE_HEAP;MEM32(SOURCE)=SOURCE_HEAP;}
        /* Exact and partial object aliasing: destination clears can alter the source. */
        if(n%19==0){ARG(0)=SELF+4;MEM32(SELF+8)=size;MEM32(SELF+12)=size;}
        if(n%31==0){ARG(0)=SELF-4;MEM32(SELF-4)=SOURCE_HEAP;}
    }
    if(n%11==0){
        /* Visibility uses bit arrays placed in its caller's guest stack frame. */
        const uint32_t object=STACK+0x40;
        memcpy(GPTR(object),GPTR(SELF),28);if(MEM32(object)==SELF+12)MEM32(object)=object+12;
        g_ecx=object;if(op==3&&ARG(0)==SELF)ARG(0)=object;
    }
    if(op==3&&n%37==0){
        const uint32_t source=STACK+0x60;
        memcpy(GPTR(source),GPTR(SOURCE),28);if(MEM32(source)==SOURCE+12)MEM32(source)=source+12;
        ARG(0)=source;
    }
}

int main(void)
{
    assert(bitarray_load_mode()==0);
    void *memory=mmap((void *)VOID_GUEST_BASE,0x2000000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    assert(memory==(void *)VOID_GUEST_BASE);
    static uint8_t before_data[DATA_BYTES],before_stack[STACK_BYTES],expected_data[DATA_BYTES],expected_stack[STACK_BYTES];
    recomp_func_t functions[]={__wrap_sub_00BF81A0,__wrap_sub_00BF82A0,__wrap_sub_00BF82F0,__wrap_sub_00BF8340};
    for(unsigned op=0;op<4;++op)for(unsigned n=0;n<1152;++n){
        fixture(n,op);void_regs before={0},original={0},actual={0};void_regs_save(&before);
        memcpy(before_data,GPTR(DATA),DATA_BYTES);memcpy(before_stack,GPTR(STACK_LO),STACK_BYTES);
        const uint32_t allocator=MEM32(0x1DAC458);
        checking=0;render_bitarray_mode=0;functions[op]();void_regs_save(&original);expected_count=event_count;
        memcpy(expected_data,GPTR(DATA),DATA_BYTES);memcpy(expected_stack,GPTR(STACK_LO),STACK_BYTES);
        const uint32_t expected_allocator=MEM32(0x1DAC458);
        for(unsigned mode=1;mode<=2;++mode){
            memcpy(GPTR(DATA),before_data,DATA_BYTES);memcpy(GPTR(STACK_LO),before_stack,STACK_BYTES);
            MEM32(0x1DAC458)=allocator;void_regs_load(&before);event_count=callback_count=0;checking=1;
            render_bitarray_mode=(int)mode;functions[op]();void_regs_save(&actual);
            if(event_count!=expected_count||memcmp(&original,&actual,sizeof actual)||memcmp(expected_data,GPTR(DATA),DATA_BYTES)||
                memcmp(expected_stack,GPTR(STACK_LO),STACK_BYTES)||expected_allocator!=MEM32(0x1DAC458)){
                fprintf(stderr,"operation %u case %u mode %u final mismatch, events %u/%u\n",op,n,mode,expected_count,event_count);
                differences(&original,&actual,sizeof actual,"regs");differences(expected_data,GPTR(DATA),DATA_BYTES,"data");
                differences(expected_stack,GPTR(STACK_LO),STACK_BYTES,"stack");return 1;
            }
        }
    }
    assert(total_events&&total_yields&&total_mutations&&oversize_requests);
    printf("PASS native bitarray: 4608 cases x original/native/mode2, %llu callback/resolver/yield observations, %llu yields, %llu mutations, %llu bounded oversize requests; complete registers, flags, fixture memory and stack exact\n",
        (unsigned long long)total_events,(unsigned long long)total_yields,(unsigned long long)total_mutations,(unsigned long long)oversize_requests);
    munmap(memory,0x2000000);return 0;
}
