#include "host.h"
#include <assert.h>
#include <sys/mman.h>
#include "render_sparse_native_under_test.c"

recomp_ehframe_t *g_eh_frames;
uint32_t g_eh_obj,g_eh_info;
enum { DATA=0x100000, NORMAL_SIZE=0x10000, MAX_SIZE=0x60000,
       ITER=DATA+0x100, DESC=DATA+0x200, WORDS=DATA+0x1000, STACK=DATA+0xF000 };
typedef struct { void_regs regs;uint8_t memory[MAX_SIZE]; } yield_event;
static yield_event events[4],expected_events[4];
static uint8_t initial[MAX_SIZE],expected_memory[MAX_SIZE];
static unsigned event_count,total_yields,case_number,memory_size,mutation;
static uint32_t initial_extent;
static uint32_t random_state=0x37BC9A51;
static uint32_t next_u32(void){random_state=random_state*1664525u+1013904223u;return random_state;}
const char *void_data_dir(void){return "/nonexistent";}
void void_log(const char *text,...){(void)text;}
void void_dispatch_add(uint32_t address,recomp_func_t function){(void)address;(void)function;}

static void yielded(void)
{
    assert(event_count<4);yield_event *e=&events[event_count++];memset(&e->regs,0,sizeof e->regs);
    void_regs_save(&e->regs);memcpy(e->memory,GPTR(DATA),memory_size);++total_yields;
    /* Mutations exercise the original pre-yield branch predicate and resumed
     * GPRs, while staying within memory that the original implementation owns. */
    if(event_count==1){
        if(mutation==1)MEM32(g_esi+20)=0;
        if(mutation==2){g_edx=2;g_edi=64;g_ecx=2;}
        if(mutation==3){memcpy(GPTR(DATA+0x300),GPTR(g_esi),28);g_esi=DATA+0x300;}
        if(mutation==4){memmove(GPTR(g_esp+64),GPTR(g_esp),24);g_esp+=64;}
        if(mutation==5){g_eax=0xF1234567;g_ebx=0xB1234567;g_ebp=0xAA551122;}
        if(mutation==6){MEM32(g_esi+16)=0;MEM32(g_esi+4)=1;}
        if(mutation==7){g_flag_k=FK_ADD;g_flag_a=UINT32_MAX;g_flag_b=1;g_flag_cf=1;
            g_xmm[3].u32[2]^=0x12345678;g_st[4]+=0.125;}
    }
}

static void setup(uint32_t extent,unsigned style,uint32_t iterator,uint32_t source,unsigned mutate,int long_scan)
{
    memory_size=long_scan?MAX_SIZE:NORMAL_SIZE;mutation=mutate;event_count=0;initial_extent=extent;
    memset(GPTR(DATA),0xA5,memory_size);
    const uint32_t count=(uint32_t)((int32_t)(extent+31u)/32);
    uint32_t words=long_scan?DATA+0x10000:WORDS;
    if((int32_t)count< -256)words=WORDS-count*4u+4u;  /* wrapped tail address remains mapped */
    const unsigned initialize=long_scan?70000:2048;
    for(unsigned i=0;i<initialize;++i){
        uint32_t value=style==0?0:style==1?UINT32_MAX:style==2?(1u<<(i&31u)):next_u32();
        if(style>=4)value=i==style-4?next_u32()|1u:0;
        MEM32((long_scan?DATA+0x10000:WORDS)+i*4u)=value;
    }
    if(long_scan)MEM32(words+69999*4u)=1;
    g_eax=next_u32();g_ecx=iterator;g_edx=next_u32();g_ebx=next_u32();
    g_esp=STACK;g_ebp=next_u32();g_esi=next_u32();g_edi=next_u32();
    g_flag_k=FK_TEST;g_flag_a=next_u32();g_flag_b=next_u32();g_flag_cf=1;
    g_backedges=case_number%3?0:RECOMP_YIELD_EVERY-1-(case_number%5);
    if(mutate)g_backedges=RECOMP_YIELD_EVERY-1;
    if(case_number%97==0 && !mutate)g_backedges=UINT32_MAX;
    recomp_yield_hook=case_number%11==0&&!mutate?NULL:yielded;
    for(unsigned i=0;i<8;++i){g_st[i]=(double)(int32_t)next_u32()/3.;
        for(unsigned j=0;j<4;++j)g_xmm[i].u32[j]=next_u32();}
    MEM32(source)=words;MEM32(source+4)=extent;MEM32(STACK)=RECOMP_RETADDR;ARG(0)=source;
}

static int check(void)
{
    void_regs before={0},native={0},original={0};void_regs_save(&before);memcpy(initial,GPTR(DATA),memory_size);
    render_sparse_mode=1;__wrap_sub_005DAF50();void_regs_save(&native);
    memcpy(expected_memory,GPTR(DATA),memory_size);
    const unsigned native_events=event_count;
    for(unsigned i=0;i<native_events;++i)expected_events[i]=events[i];
    memcpy(GPTR(DATA),initial,memory_size);void_regs_load(&before);event_count=0;
    render_sparse_mode=case_number&1?0:2;__wrap_sub_005DAF50();void_regs_save(&original);
    int failed=memcmp(&native,&original,sizeof native)||memcmp(expected_memory,GPTR(DATA),memory_size)||event_count!=native_events;
    for(unsigned i=0;i<native_events&&i<event_count;++i)
        failed|=memcmp(&expected_events[i].regs,&events[i].regs,sizeof(void_regs))||memcmp(expected_events[i].memory,events[i].memory,memory_size);
    if(failed){
        fprintf(stderr,"sparse case %u failed, extent %08x iterator %08x, mutation %u events %u/%u\n",
            case_number,initial_extent,before.ecx,mutation,native_events,event_count);
        for(unsigned i=0;i<sizeof native/4;++i)if(((uint32_t*)&native)[i]!=((uint32_t*)&original)[i])
            fprintf(stderr,"state+%u %08x != %08x\n",i*4,((uint32_t*)&native)[i],((uint32_t*)&original)[i]);
        for(unsigned i=0;i<memory_size/4;++i)if(((uint32_t*)expected_memory)[i]!=MEM32(DATA+i*4u)){
            fprintf(stderr,"memory+%x %08x != %08x\n",i*4,((uint32_t*)expected_memory)[i],MEM32(DATA+i*4u));break;}
        for(unsigned e=0;e<native_events&&e<event_count;++e)if(memcmp(&expected_events[e].regs,&events[e].regs,sizeof(void_regs))){
            for(unsigned i=0;i<sizeof(void_regs)/4;++i)if(((uint32_t*)&expected_events[e].regs)[i]!=((uint32_t*)&events[e].regs)[i])
                fprintf(stderr,"yield %u state+%u %08x != %08x\n",e,i*4,((uint32_t*)&expected_events[e].regs)[i],((uint32_t*)&events[e].regs)[i]);break;}
        return 0;
    }
    ++case_number;return 1;
}

int main(void)
{
    assert(sparse_load_mode()==0);
    void *mapped=mmap((void*)VOID_GUEST_BASE,0x200000,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);assert(mapped==(void*)VOID_GUEST_BASE);
    for(unsigned extent=0;extent<=256;++extent)for(unsigned style=0;style<12;++style){
        setup(extent,style,ITER,DESC,0,0);if(!check())return 1;
    }
    for(unsigned n=0;n<24000;++n){
        setup(next_u32()%65537u,n%5,ITER,DESC,n%17==0?1+n%7:0,0);if(!check())return 1;
    }
    for(int extent=-4096;extent<0;++extent){setup((uint32_t)extent,3,ITER,DESC,0,0);if(!check())return 1;}
    static const uint32_t edges[]={0x7FFFFFFF,0x80000000,0x80000001,0x8000001F,0xFFFFFFE0,0xFFFFFFE1,0xFFFFFFFF};
    for(unsigned i=0;i<sizeof edges/sizeof edges[0];++i){setup(edges[i],3,ITER,DESC,0,0);if(!check())return 1;}
    for(int offset=-24;offset<=8;offset+=4)for(unsigned extent=0;extent<=64;++extent){
        setup(extent,3,DESC+(uint32_t)offset,DESC,0,0);if(!check())return 1;
    }
    for(int offset=-32;offset<=16;offset+=4)for(unsigned extent=0;extent<=64;++extent){
        setup(extent,3,STACK+(uint32_t)offset,DESC,0,0);if(!check())return 1;
    }
    for(unsigned offset=0;offset<=32;offset+=4)for(unsigned style=0;style<12;++style){
        setup(512,style,WORDS+offset,DESC,0,0);if(!check())return 1;
    }
    /* Force every resumed-state mutation for both an empty and a nonempty first word. */
    for(unsigned mutate=1;mutate<=7;++mutate)for(unsigned style=0;style<4;++style){
        setup(2048,style,ITER,DESC,mutate,0);if(!check())return 1;
    }
    for(unsigned n=0;n<8;++n){setup(70000u*32u,0,ITER,DESC,0,1);if(!check())return 1;
        assert(MEM32(ITER+24)==69999u*32u);if(recomp_yield_hook)assert(event_count>0);}
    assert(total_yields>0);
    printf("PASS native sparse iterator: %u cases, %u yield callbacks; full memory, flags, GPR/XMM/x87 and stack exact\n",case_number,total_yields);
    munmap(mapped,0x200000);return 0;
}
