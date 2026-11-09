#include "host.h"
#include <assert.h>
#include <sys/mman.h>
#include "../android/host/ai_perception_native.c"

recomp_ehframe_t *g_eh_frames;
uint32_t g_eh_obj,g_eh_info;
extern void original_world_controllers(void);
enum { STACK=0x30008, DATA=0x100000, DATA_SIZE=0x18000, WORLD=DATA,
    LEVEL=DATA+0x400, ACTORS=DATA+0x800, INFO=DATA+0xC00, GAME=DATA+0x1000,
    VTABLE=DATA+0x1400, ALT_VTABLE=DATA+0x1800, CONTROLLERS=DATA+0x2000,
    PRIS=DATA+0xA000, SUBJECT=DATA+0x12000, SUBJECT_PRI=DATA+0x12400,
    BASE_GATE=0x00FC8DE0, OVERRIDE_GATE=0x7FF10000, VISIBILITY=0x7FF10004 };
typedef struct { unsigned kind;void_regs regs;uint32_t args[3],stack[10]; } event;
static event events[4096],expected_events[4096];
static unsigned event_count,case_number,probe_count,visibility_count,notification_count,yield_count;
static uint32_t seed=0x1AA3C659;
static uint32_t random_u32(void){seed=seed*1664525u+1013904223u;return seed;}
static uint32_t controller(unsigned i){return CONTROLLERS+0x400u*i;}
static void put_float(uint32_t at,float value){*(float*)ADDR(at)=value;}
const char *void_data_dir(void){return "/nonexistent";}
void void_log(const char *fmt,...){(void)fmt;}
void void_dispatch_add(uint32_t va,recomp_func_t fn){(void)va;(void)fn;}
static void record(unsigned kind,unsigned argc)
{
    assert(event_count<4096);event *e=&events[event_count++];memset(e,0,sizeof *e);
    e->kind=kind;void_regs_save(&e->regs);
    for(unsigned i=0;i<argc;++i)e->args[i]=ARG(i);
    memcpy(e->stack,GPTR(g_esp),sizeof e->stack);
}
static void clobber(unsigned tag)
{
    g_eax=0xA1000000+tag;g_ecx=0xC1000000+tag;g_edx=0xD1000000+tag;
    g_xmm[0].u32[0]=0x3F123456+tag;g_flag_k=FK_CMP;g_flag_a=tag;g_flag_b=17;g_flag_cf=tag<17;
}
void sub_00486FD0(void)
{
    record(1,2);++probe_count;assert(ARG(1)==0 && (ARG(0)==0x154||ARG(0)==0x158));
    const uint32_t bit=ARG(0)==0x154?1:2;
    const uint32_t result=(MEM32(g_ecx+0x3F0)&bit)!=0;
    if(case_number%43==0 && probe_count==1 && MEM32(g_ecx+0x1B8))
        MEM32(MEM32(g_ecx+0x1B8)+0x1F4)=MEM32(SUBJECT_PRI+0x1F4);
    clobber(1);g_eax=result;g_esp+=12;
}
void sub_00CE85C0(void){record(2,0);original_world_controllers();}
static void base_gate(void){record(3,1);__wrap_sub_00FC8DE0();}
static void override_gate(void)
{
    record(4,1);uint32_t result=MEM32(g_ecx+0x3F0)&1;
    clobber(4);g_eax=result;g_esp+=8;
}
static void visibility(void)
{
    record(5,2);++visibility_count;assert(ARG(1)==1);
    uint32_t result=MEM32(g_ecx+0x3F4)&1;
    if(case_number%29==0 && visibility_count==1){
        MEM32(SUBJECT+0x1B4)+=0x40;MEM32(SUBJECT+0x1C4)^=1;
    }
    clobber(5);g_eax=result;g_esp+=12;
}
static void notify(unsigned kind)
{
    record(kind,1);++notification_count;MEM32(g_ecx+0x3F8)++;MEM32(g_ecx+0x3FC)=ARG(0);
    if(case_number%31==0 && notification_count==1){
        uint32_t next=MEM32(g_ecx+0x1C0);
        if(next)MEM32(g_ecx+0x1C0)=MEM32(next+0x1C0);
    }
    clobber(kind);g_esp+=8;
}
void sub_00FC99E0(void){notify(6);}
void sub_00FC9980(void){notify(7);}
recomp_func_t void_resolve(uint32_t address)
{
    /* A resolver sees published registers before the synthetic return address. */
    record(9,0);
    switch(address){case BASE_GATE:return base_gate;case OVERRIDE_GATE:return override_gate;
        case VISIBILITY:return visibility;default:assert(0);return 0;}
}
static void yield_probe(void)
{
    record(8,0);++yield_count;
    if(g_esi && case_number%37==0)MEM32(g_esi+0x3F0)=0;
    /* Global flags can belong to another host action; original local flags
     * still govern the pending backward branch after the VM resumes. */
    if(case_number%41==0){g_flag_k=FK_TEST;g_flag_a=0;g_flag_b=0;g_flag_cf=0;}
}

static void fixture(unsigned n,int scan)
{
    case_number=n;memset(GPTR(DATA),0,DATA_SIZE);memset(GPTR(STACK-0x200),0xB7,0x300);
    MEM32(0x1DEE5EC)=WORLD;MEM32(WORLD+0x50)=LEVEL;MEM32(LEVEL+0x3C)=ACTORS;MEM32(ACTORS)=INFO;
    MEM32(INFO+0x338)=n%11?GAME:0;MEM32(GAME+0x1B4)=n%3?4:0;
    MEM32(VTABLE+0x32C)=BASE_GATE;MEM32(VTABLE+0x330)=VISIBILITY;
    MEM32(ALT_VTABLE+0x32C)=OVERRIDE_GATE;MEM32(ALT_VTABLE+0x330)=VISIBILITY;
    MEM32(SUBJECT)=VTABLE;MEM32(SUBJECT+0x98)=INFO;MEM32(SUBJECT+0x1B8)=SUBJECT_PRI;
    MEM32(SUBJECT+0x1B4)=n%17?DATA+0x13000:0;MEM32(SUBJECT+0x1C4)=n&1;
    MEM32(SUBJECT_PRI+0x1F4)=n%7?0x12340000:0;
    unsigned count=scan?n%33:1;
    MEM32(INFO+0x354)=count?CONTROLLERS:0;
    for(unsigned i=0;i<count;++i){
        const uint32_t c=controller(i),pri=PRIS+0x300*i;
        MEM32(c)=i%5==0 && scan?ALT_VTABLE:VTABLE;
        MEM32(c+0x98)=INFO;MEM32(c+0x1B8)=n%13 && i%7?pri:0;
        MEM32(pri+0x1F4)=i%3==0?0:i%3==1?0x12340000:0x56780000;
        MEM32(c+0x1C0)=i+1<count?controller(i+1):0;
        MEM32(c+0x1C4)=(n+i)&1?1:0;if((n+i)%5==0)MEM32(c+0x1C4)|=0x20000;
        put_float(c+0x28C,(n+i)%3==0?-1.0f:(n+i)%3==1?0.0f:1.0f);
        MEM32(c+0x3F0)=(n+i)%4;MEM32(c+0x3F4)=(n+i)/3%2;
    }
    if(scan && count>2 && n%7==0){
        MEM32(controller(1)+0x1C0)=SUBJECT;MEM32(SUBJECT+0x1C0)=controller(2);
    }
    if(!scan){
        static const uint32_t float_bits[]={0,0x80000000,1,0x80000001,0x3F800000,0xBF800000,
            0x7F800000,0xFF800000,0x7FC00001,0x7FA12345,0xFFC54321,0xFF7FFFFF};
        MEM32(CONTROLLERS+0x28C)=n<12288?float_bits[n%12]:random_u32();
        MEM32(CONTROLLERS+0x1C4)=((n/12)&1)|((n/24)&1?0x20000:0);
        MEM32(SUBJECT+0x1C4)=(n/48)&1;
        MEM32(CONTROLLERS+0x3F0)=(n/96)%4;
        MEM32(INFO+0x338)=(n/384)%2?GAME:0;
        MEM32(GAME+0x1B4)=(n/768)%2?4:0;
        MEM32(CONTROLLERS+0x1B8)=(n/1536)%2?PRIS:0;
        MEM32(PRIS+0x1F4)=(n/3072)%2?0x12340000:0;
        MEM32(SUBJECT+0x1B8)=(n/6144)%2?SUBJECT_PRI:0;
        MEM32(SUBJECT_PRI+0x1F4)=(n/12288)%3==0?0:(n/12288)%3==1?0x12340000:0x56780000;
    }
    g_eax=random_u32();g_ecx=scan?SUBJECT:CONTROLLERS;g_edx=random_u32();g_ebx=random_u32();
    g_esp=STACK;g_esi=random_u32();g_edi=random_u32();g_ebp=random_u32();
    g_flag_k=FK_TEST;g_flag_a=random_u32();g_flag_b=random_u32();g_flag_cf=1;
    g_backedges=n%3?0:RECOMP_YIELD_EVERY-1-(n%7);recomp_yield_hook=yield_probe;
    for(unsigned i=0;i<8;++i)for(unsigned j=0;j<4;++j)g_xmm[i].u32[j]=random_u32();
    MEM32(STACK)=RECOMP_RETADDR;ARG(0)=SUBJECT;
    event_count=probe_count=visibility_count=notification_count=yield_count=0;
}

int main(void)
{
    void *mapped=mmap((void*)VOID_GUEST_BASE,0x2000000,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);assert(mapped==(void*)VOID_GUEST_BASE);
    static uint8_t before_data[DATA_SIZE],after_data[DATA_SIZE],before_stack[0x300],after_stack[0x300];
    unsigned totals[4]={0};
    for(unsigned n=0;n<62000;++n){
        const int scan=n>=50000;fixture(n,scan);
        void_regs before={0},expected={0},actual={0};void_regs_save(&before);
        memcpy(before_data,GPTR(DATA),DATA_SIZE);memcpy(before_stack,GPTR(STACK-0x200),sizeof before_stack);
        ai_perception_mode=1;if(scan)__wrap_sub_00FCBCB0();else __wrap_sub_00FC8DE0();
        void_regs_save(&expected);memcpy(after_data,GPTR(DATA),DATA_SIZE);
        memcpy(after_stack,GPTR(STACK-0x200),sizeof after_stack);
        memcpy(expected_events,events,event_count*sizeof(event));const unsigned expected_count=event_count;
        totals[0]+=probe_count;totals[1]+=visibility_count;totals[2]+=notification_count;totals[3]+=yield_count;
        memcpy(GPTR(DATA),before_data,DATA_SIZE);memcpy(GPTR(STACK-0x200),before_stack,sizeof before_stack);
        void_regs_load(&before);event_count=probe_count=visibility_count=notification_count=yield_count=0;
        ai_perception_mode=n%2?0:2;if(scan)__wrap_sub_00FCBCB0();else __wrap_sub_00FC8DE0();
        void_regs_save(&actual);
        if(expected_count!=event_count || memcmp(expected_events,events,event_count*sizeof(event)) ||
            memcmp(&expected,&actual,sizeof actual) || memcmp(after_data,GPTR(DATA),DATA_SIZE) ||
            memcmp(after_stack,GPTR(STACK-0x200),sizeof after_stack)){
            fprintf(stderr,"case %u scan %d failed events %u/%u\n",n,scan,expected_count,event_count);
            for(unsigned i=0;i<expected_count && i<event_count;++i)if(memcmp(&expected_events[i],&events[i],sizeof(event))){
                fprintf(stderr,"event %u kind %u/%u\n",i,expected_events[i].kind,events[i].kind);
                for(unsigned j=0;j<sizeof(event)/4;++j)if(((uint32_t*)&expected_events[i])[j]!=((uint32_t*)&events[i])[j])
                    fprintf(stderr,"event byte %u: %08x/%08x\n",j*4,((uint32_t*)&expected_events[i])[j],((uint32_t*)&events[i])[j]);break;
            }
            for(unsigned j=0;j<sizeof(actual)/4;++j)if(((uint32_t*)&expected)[j]!=((uint32_t*)&actual)[j])
                fprintf(stderr,"state byte %u: %08x/%08x\n",j*4,((uint32_t*)&expected)[j],((uint32_t*)&actual)[j]);
            return 1;
        }
    }
    assert(totals[0] && totals[1] && totals[2] && totals[3]);
    printf("PASS native AI perception: 62000 cases, %u probes, %u visibility checks, %u notifications, %u yields; callback/resolver state, order, mutations, full stack and outgoing state exact\n",totals[0],totals[1],totals[2],totals[3]);
    munmap(mapped,0x2000000);return 0;
}
