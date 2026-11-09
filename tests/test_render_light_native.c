#include "host.h"
#include <assert.h>
#include <sys/mman.h>
#include "render_light_native_under_test.c"
recomp_ehframe_t *g_eh_frames;uint32_t g_eh_obj,g_eh_info;
enum { DATA=0x100000,DATA_SIZE=0x10000,DESC=DATA+0x100,ALT=DATA+0x200,BUFFER=DATA+0x1000,
       SECOND=DATA+0x4000,STACK=DATA+0xF000,ALLOC=DATA+0x500,VTABLE=DATA+0x600,CB=0x7FF00100,YIELD=0xFFFFFFFFu };
typedef struct {void_regs regs;uint32_t kind,allocator;unsigned char memory[DATA_SIZE];} event;
static event events[512];static unsigned event_count,expected_count,checking,case_number,mutation,destructors;
static uint64_t boundaries,total_yields,total_destroys,total_allocs,total_resolves;
static uint32_t rng=0x81A93721;static uint32_t random_u32(void){rng=rng*1664525u+1013904223u;return rng;}
const char *void_data_dir(void){return "/nonexistent";}
void void_log(const char *fmt,...){(void)fmt;}
void void_dispatch_add(uint32_t address,recomp_func_t fn){(void)address;(void)fn;}
static void record(uint32_t kind)
{
    assert(event_count<512);static event actual;memset(&actual,0,sizeof actual);actual.kind=kind;
    actual.allocator=MEM32(0x1DAC458);void_regs_save(&actual.regs);memcpy(actual.memory,GPTR(DATA),DATA_SIZE);
    if(!checking)events[event_count]=actual;
    else if(event_count>=expected_count||memcmp(&events[event_count],&actual,sizeof actual)){
        fprintf(stderr,"light reset case%u mutation%u event%u kind%08x expected%08x\n",case_number,mutation,event_count,kind,events[event_count].kind);
        for(unsigned i=0;i<sizeof(void_regs)/4;++i)if(((uint32_t*)&actual.regs)[i]!=((uint32_t*)&events[event_count].regs)[i])
            fprintf(stderr,"regs+%x %08x/%08x\n",i*4,((uint32_t*)&events[event_count].regs)[i],((uint32_t*)&actual.regs)[i]);
        for(unsigned i=0;i<DATA_SIZE/4;++i)if(((uint32_t*)actual.memory)[i]!=((uint32_t*)events[event_count].memory)[i]){
            fprintf(stderr,"memory+%x %08x/%08x\n",i*4,((uint32_t*)events[event_count].memory)[i],((uint32_t*)actual.memory)[i]);break;}
        exit(1);
    }
    ++event_count;++boundaries;
}
static void clobber(uint32_t tag)
{
    g_eax=0xA0000000u+tag;g_ecx=0xC0000000u+tag;g_edx=0xD0000000u+tag;
    g_flag_k=FK_ADD;g_flag_a=UINT32_MAX;g_flag_b=tag;g_flag_cf=tag!=0;
    g_xmm[2].u32[3]^=tag;g_st[3]+=0.25;
}
static void yielded(void)
{
    record(YIELD);++total_yields;
    if(mutation==6)MEM32(g_esi+4)=0;
    if(mutation==7){memmove(GPTR(g_esp+64),GPTR(g_esp),20);g_esp+=64;}
    if(mutation==8){memcpy(GPTR(ALT),GPTR(g_esi),12);g_esi=ALT;}
    if(mutation==9){g_flag_k=FK_TEST;g_flag_a=9;g_flag_b=3;g_flag_cf=0;g_eax^=0x2345;g_edx^=0x1234;g_xmm[0].u32[0]^=0x321;}
}
void sub_01288F71(void)
{
    record(0x01288F71);++total_destroys;++destructors;
    assert(ARG(1)==0x1C&&ARG(2)==4&&ARG(3)==0xEFC5F0&&ARG(0)==g_ecx);
    if(ARG(0)>=DATA&&ARG(0)<DATA+DATA_SIZE-4)MEM32(ARG(0))^=0x100u+destructors;
    if(destructors==1){
        if(mutation==1)MEM32(g_esi+4)=0;
        if(mutation==2)MEM32(g_esi+4)=4;
        if(mutation==3)MEM32(g_esi)=SECOND;
        if(mutation==4){memcpy(GPTR(ALT),GPTR(g_esi),12);g_esi=ALT;}
        if(mutation==5){g_ebx=0x74;g_edi=1;}
        if(mutation==12)g_backedges=RECOMP_YIELD_EVERY-1;
    }
    clobber(destructors);g_esp+=20;
}
void sub_004055A0(void){record(0x004055A0);MEM32(0x1DAC458)=ALLOC;clobber(0x31);g_esp+=4;}
static void allocator(void)
{
    record(CB);++total_allocs;assert(ARG(2)==8);uint32_t size=ARG(1);
    MEM32(DATA+0x700)=ARG(0);MEM32(DATA+0x704)=size;clobber(0x41);g_eax=size?SECOND:0;g_esp+=16;
}
recomp_func_t void_resolve(uint32_t address)
{
    record(0xF0000000u|address);++total_resolves;assert(address==CB);
    if(mutation==10){g_eax^=0x1234;g_edx^=0x4321;g_ecx^=0x1234;}
    if(mutation==11){memmove(GPTR(g_esp+64),GPTR(g_esp),32);g_esp+=64;}
    return allocator;
}
static void setup(uint32_t count,uint32_t capacity,uint32_t requested,unsigned null_data,unsigned initialize,unsigned mut,int alias)
{
    memset(GPTR(DATA),0xA5,DATA_SIZE);mutation=mut;event_count=destructors=0;
    MEM32(ALLOC)=VTABLE;MEM32(VTABLE+8)=CB;MEM32(0x1DAC458)=initialize?ALLOC:0;
    uint32_t descriptor=alias==1?STACK+4:alias==2?STACK-8:alias==3?STACK-12:alias==4?BUFFER:DESC;
    g_eax=random_u32();g_ecx=descriptor;g_edx=random_u32();g_ebx=random_u32();g_esp=STACK;
    g_ebp=random_u32();g_esi=random_u32();g_edi=random_u32();g_backedges=case_number%3?0:RECOMP_YIELD_EVERY-1;
    if(mut>=6&&mut<=9)g_backedges=RECOMP_YIELD_EVERY-1;
    recomp_yield_hook=case_number%17==0&&mut==0?NULL:yielded;
    g_flag_k=FK_TEST;g_flag_a=random_u32();g_flag_b=random_u32();g_flag_cf=1;
    for(unsigned i=0;i<8;++i){g_st[i]=(double)i+0.125;for(unsigned j=0;j<4;++j)g_xmm[i].u32[j]=random_u32();}
    MEM32(descriptor)=null_data?0:BUFFER;MEM32(descriptor+4)=count;MEM32(descriptor+8)=capacity;
    MEM32(STACK)=RECOMP_RETADDR;ARG(0)=requested;
    if(alias==1){MEM32(STACK+4)=BUFFER;MEM32(STACK+8)=count;MEM32(STACK+12)=capacity;}
    if(alias==2){g_esi=BUFFER;g_ebx=count;MEM32(STACK)=capacity;}
    if(alias==3){g_edi=BUFFER;g_esi=count;g_ebx=capacity;}
}
static int check(void)
{
    static unsigned char before[DATA_SIZE],after[DATA_SIZE];void_regs initial={0},original={0},native={0};
    void_regs_save(&initial);memcpy(before,GPTR(DATA),DATA_SIZE);uint32_t initial_alloc=MEM32(0x1DAC458);
    checking=0;render_light_array_mode=0;__wrap_sub_00EFF830();void_regs_save(&original);memcpy(after,GPTR(DATA),DATA_SIZE);
    const uint32_t final_alloc=MEM32(0x1DAC458);expected_count=event_count;
    memcpy(GPTR(DATA),before,DATA_SIZE);MEM32(0x1DAC458)=initial_alloc;void_regs_load(&initial);event_count=destructors=0;
    checking=1;render_light_array_mode=1;__wrap_sub_00EFF830();void_regs_save(&native);
    if(event_count!=expected_count||memcmp(&original,&native,sizeof native)||memcmp(after,GPTR(DATA),DATA_SIZE)||MEM32(0x1DAC458)!=final_alloc){
        fprintf(stderr,"light reset final case%u mutation%u events%u/%u\n",case_number,mutation,event_count,expected_count);return 0;
    }
    ++case_number;return 1;
}
int main(void)
{
    assert(light_array_load_mode()==0);
    void *mapped=mmap((void*)VOID_GUEST_BASE,0x2000000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    assert(mapped==(void*)VOID_GUEST_BASE);
    for(unsigned count=0;count<=16;++count)for(unsigned capacity=0;capacity<=16;capacity+=4)
        for(unsigned requested=0;requested<=16;requested+=4)for(unsigned init=0;init<2;++init){
            setup(count,capacity,requested,0,init,0,0);if(!check())return 1;
        }
    for(unsigned n=0;n<1500;++n){
        const uint32_t count=random_u32()%20,capacity=random_u32(),requested=random_u32();
        setup(count,capacity,requested,0,n%2,n%13,0);if(!check())return 1;
    }
    static const uint32_t edge[]={0,1,0xFFFFFFFFu,0x80000000u,0x7FFFFFFFu,0x40000000u};
    for(unsigned i=0;i<6;++i)for(unsigned j=0;j<6;++j)for(unsigned zero=0;zero<2;++zero){
        setup(i%2?0xFFFFFFFFu:0,edge[i],edge[j],zero,j%2,0,0);if(!check())return 1;
    }
    for(unsigned mut=1;mut<=12;++mut)for(unsigned count=1;count<8;++count){setup(count,8,4,0,count%2,mut,0);if(!check())return 1;}
    for(int alias=1;alias<=4;++alias)for(unsigned count=0;count<5;++count)for(unsigned init=0;init<2;++init){
        setup(count,8,4,0,init,0,alias);if(!check())return 1;
    }
    printf("PASS native light-array reset: %u cases, %llu boundaries, %llu yields, %llu destructors, %llu reallocations, %llu resolver observations; exact memory/stack/GPR/flags/XMM/x87\n",case_number,
        (unsigned long long)boundaries,(unsigned long long)total_yields,(unsigned long long)total_destroys,
        (unsigned long long)total_allocs,(unsigned long long)total_resolves);munmap(mapped,0x2000000);return 0;
}
