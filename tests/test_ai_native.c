#include "host.h"
#include <assert.h>
#include <sys/mman.h>
#include "../android/host/ai_native.c"

recomp_ehframe_t *g_eh_frames;
uint32_t g_eh_obj, g_eh_info;
enum { STACK=0x30008, DATA=0x100000, DATA_SIZE=0x18000,
    PAWN=DATA, CYLINDER=DATA+0x400, VTABLE=DATA+0x800, SPEC_VTABLE=DATA+0xC00,
    BEST=DATA+0xF00, NODES=DATA+0x1000, EDGES=DATA+0x9000,
    CAP_FN=0x7FF10000, EVAL_FN=0x7FF10004, COST_FN=0x7FF10008, FINAL_FN=0x7FF1000C };
typedef struct { unsigned kind; void_regs regs; uint32_t args[6], frame[30]; } event;
static event events[8192], expected_events[8192];
static unsigned case_number, event_count, eval_count, cost_count, yield_count, warning_count;
static uint32_t seed=0x9E4301D7;
static uint32_t random_u32(void) { seed=seed*1664525u+1013904223u; return seed; }
static uint32_t node(unsigned n) { return NODES+n*0x400u; }
static uint32_t edge(unsigned n) { return EDGES+n*0x100u; }
static void put_float(uint32_t at,float value) { *(float*)ADDR(at)=value; }
const char *void_data_dir(void) { return "/nonexistent"; }
void void_log(const char *fmt,...) { (void)fmt; }
void void_dispatch_add(uint32_t address,recomp_func_t fn) { (void)address; (void)fn; }

static void record(unsigned kind,unsigned argc)
{
    assert(event_count<8192);
    event *e=&events[event_count++]; memset(e,0,sizeof *e); e->kind=kind;
    void_regs_save(&e->regs);
    for(unsigned i=0;i<argc;++i)e->args[i]=ARG(i);
    memcpy(e->frame,GPTR(STACK-0x78),sizeof e->frame);
}
static void clobber(unsigned tag)
{
    g_eax=0xA1000000u+tag; g_ecx=0xC1000000u+tag; g_edx=0xD1000000u+tag;
    g_xmm[0].u32[0]=0x3F123456u+tag;
    g_flag_k=FK_CMP;g_flag_a=tag;g_flag_b=17;g_flag_cf=tag<17;
}
void sub_004050E0(void)
{
    record(1,1); assert(ARG(0)==0xB56);
    MEM32(g_ecx)=0xDECAFBAD; MEM32(g_ecx+4)=case_number;
    clobber(1);g_esp+=8;
}
void sub_00405190(void) { record(2,0);clobber(2);g_esp+=4; }
static void capability(void)
{ record(3,0);assert(g_ecx==PAWN);clobber(3);g_eax=MEM32(PAWN+0x3F0);g_esp+=4; }
static void evaluate(void)
{
    record(4,3);assert(ARG(1)==PAWN);++eval_count;
    const uint32_t current=ARG(0);
    /* Goal policies may mutate future graph state while returning a weight. */
    if(case_number%29==0 && eval_count==1)MEM32(node(2)+0x1B4)|=0x10;
    double score=sse_f32(MEM32(current+0x300));
    if(case_number%41==0)score=(double)eval_count*0.03125;
    if(case_number%43==0)score=0.1+(double)eval_count*0.0000000001;
    clobber(4);fp_push(score);g_esp+=4;
}
static void cost(void)
{
    record(5,1);assert(ARG(0)==PAWN);++cost_count;
    const uint32_t spec=g_ecx;
    if(case_number%31==0 && cost_count==1)MEM32(spec+0x70)^=2;
    const uint32_t result=MEM32(spec+0x90);
    clobber(5);g_eax=result;g_esp+=8;
}
static void finish(void)
{
    record(6,3);assert(g_ecx==PAWN);MEM32(PAWN+0x3F4)=ARG(0);MEM32(PAWN+0x3F8)=ARG(1);
    const uint32_t result=ARG(0);clobber(6);g_eax=result;g_esp+=16;
}
void sub_0040D7E0(void)
{
    record(7,1);const uint32_t result=ARG(0);
    MEM32(result)=g_ecx+0x304;MEM32(result+4)=case_number%2;MEM32(result+8)=1;
    clobber(7);g_eax=result;g_esp+=8;
}
void sub_00B54280(void) {record(8,6);++warning_count;clobber(8);g_esp+=4;}
void sub_00404A00(void) {record(9,0);MEM32(g_ecx)=0;clobber(9);g_esp+=4;}
recomp_func_t void_resolve(uint32_t address)
{
    switch(address){case CAP_FN:return capability;case EVAL_FN:return evaluate;
        case COST_FN:return cost;case FINAL_FN:return finish;default:assert(0);return 0;}
}
static void yield_probe(void)
{
    record(10,0);++yield_count;
    if(case_number%37==0)MEM32(node(3)+0x300)=0x3F800000;
    if(case_number%67==0 && yield_count==1){
        const uint32_t current=MEM32(STACK-0x78+0x14);
        if(current && MEM32(current+0x1F4))--MEM32(current+0x1F4);
    }
}

static void fixture(unsigned n)
{
    case_number=n;
    memset(GPTR(DATA),0,DATA_SIZE);memset(GPTR(STACK-0x200),0xB7,0x300);
    MEM32(PAWN)=VTABLE;MEM32(PAWN+0x358)=CYLINDER;
    MEM32(VTABLE+0x390)=CAP_FN;MEM32(VTABLE+0x38C)=FINAL_FN;MEM32(SPEC_VTABLE+0x10C)=COST_FN;
    put_float(CYLINDER+0x1BC,40.25f);put_float(CYLINDER+0x1B8,25.75f);
    put_float(PAWN+0x280,2);put_float(PAWN+0x27C,48.5f);
    put_float(PAWN+0x1E0,20);put_float(PAWN+0x1E4,30);
    MEM32(PAWN+0x1D4)=n%3==0?0x20:0;MEM32(PAWN+0x3F0)=n%5?0x7:0x3;
    put_float(BEST,n%47==0?-1:0);
    const unsigned count=n<10?n:4+random_u32()%25;
    unsigned edges=0;
    for(unsigned i=0;i<count;++i){
        uint32_t nav=node(i);MEM32(nav+0x1F0)=nav+0x340;
        unsigned degree=n%7==0?count-1:random_u32()%5;
        if(degree>4)degree=4;
        MEM32(nav+0x1F4)=degree;MEM32(nav+0x220)=i?10000000:0;
        MEM32(nav+0x1B4)=i && n%13==0 && i%4==0?0x10:0;
        put_float(nav+0x300,(float)(random_u32()%31)/32.0f);
        if(i==count-1 && n%4==0)put_float(nav+0x300,1);
        for(unsigned j=0;j<degree;++j){
            uint32_t spec=edge(edges++);assert(edges<200);
            MEM32(nav+0x340+j*4)=n%19==0 && j==2?0:spec;
            MEM32(spec)=SPEC_VTABLE;
            unsigned destination=(n%7==0?(i+j+1):random_u32())%count;
            MEM32(spec+0x54)=n%23==0 && j==1?0:node(destination);
            MEM32(spec+0x68)=n%11==0?random_u32()%60:50;
            MEM32(spec+0x6C)=n%17==0?random_u32()%40:30;
            MEM32(spec+0x70)=n%5?1:1u<<(random_u32()%4);
            MEM32(spec+0x74)=n%13==0?random_u32()%120:50;
            uint32_t weight=n%2?1+random_u32()%100:1;
            if(n%53==0)weight=0;
            if(n%59==0)weight=UINT32_MAX;
            if(n%61==0)weight=10000000;
            MEM32(spec+0x90)=weight;
        }
    }
    /* Raw floating-point values cover dimensions, jump products and evaluator
     * scores; integer limits exercise the original signed comparisons. */
    if(n>=10000){
        static const uint32_t bits[]={0,0x80000000,1,0x007FFFFF,0x00800000,
            0x3F800000,0x7F7FFFFF,0xFF7FFFFF,0x7F800000,0xFF800000,
            0x7FC00001,0x7FA12345,0xFFC54321,0x4F000000,0xCF000000};
        const unsigned index=n-10000;
        MEM32(CYLINDER+0x1BC)=bits[index%15];
        MEM32(CYLINDER+0x1B8)=bits[index/15%15];
        MEM32(PAWN+0x280)=bits[index%15];MEM32(PAWN+0x27C)=bits[index/15%15];
        MEM32(PAWN+0x1E0)=bits[index/3%15];MEM32(PAWN+0x1E4)=bits[index/5%15];
        MEM32(BEST)=bits[index/7%15];
        for(unsigned i=0;i<count;++i)MEM32(node(i)+0x300)=bits[(index+i)%15];
        if(index%5==0)for(unsigned i=0;i<edges;++i)MEM32(edge(i)+0x90)=0x80000000u;
    }
    g_eax=random_u32();g_ecx=PAWN;g_edx=random_u32();g_ebx=random_u32();
    g_esp=STACK;g_esi=random_u32();g_edi=random_u32();g_ebp=random_u32();
    g_fs_base=0x2000;MEM32(FS_BASE)=0x87654321;MEM32(0x1D324E0)=0x91A2B3C4;
    g_flag_k=FK_TEST;g_flag_a=random_u32();g_flag_b=random_u32();g_flag_cf=1;
    g_backedges=n%3?0:RECOMP_YIELD_EVERY-1-(n%9);recomp_yield_hook=yield_probe;
    for(unsigned j=0;j<8;++j){g_st[j]=(double)j+0.125;g_st_i64[j]=j+100;
        for(unsigned k=0;k<4;++k)g_xmm[j].u32[k]=random_u32();}
    MEM32(STACK)=RECOMP_RETADDR;ARG(0)=EVAL_FN;ARG(1)=count?NODES:0;ARG(2)=BEST;
    ARG(3)=0x1234;ARG(4)=n%4?10000000:1+random_u32()%101;
    ARG(5)=n%3?10000:random_u32()%4;
    if(n>=10000){
        static const uint32_t limits[]={0,1,2,9999999,10000000,0x7FFFFFFF,0x80000000,0xFFFFFFFF};
        ARG(4)=limits[n%8];ARG(5)=limits[n/8%8];
    }
    eval_count=cost_count=yield_count=warning_count=event_count=0;
}

int main(void)
{
    void *mapped=mmap((void*)VOID_GUEST_BASE,0x2000000,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    assert(mapped==(void*)VOID_GUEST_BASE);
    put_float(0x17343D8,1);put_float(0x1737E48,0);MEM32(0x1D13040)=0x12345678;
    static uint8_t before_data[DATA_SIZE],after_data[DATA_SIZE],before_stack[0x300];
    unsigned totals[4]={0};
    for(unsigned n=0;n<14000;++n){
        fixture(n);void_regs before={0},expected={0},actual={0};void_regs_save(&before);
        memcpy(before_data,GPTR(DATA),DATA_SIZE);memcpy(before_stack,GPTR(STACK-0x200),sizeof before_stack);
        ai_native_mode=1;__wrap_sub_010275E0();void_regs_save(&expected);
        memcpy(after_data,GPTR(DATA),DATA_SIZE);memcpy(expected_events,events,event_count*sizeof(event));
        const unsigned expected_count=event_count;
        totals[0]+=eval_count;totals[1]+=cost_count;totals[2]+=yield_count;totals[3]+=warning_count;
        memcpy(GPTR(DATA),before_data,DATA_SIZE);memcpy(GPTR(STACK-0x200),before_stack,sizeof before_stack);
        MEM32(FS_BASE)=0x87654321;void_regs_load(&before);
        eval_count=cost_count=yield_count=warning_count=event_count=0;
        ai_native_mode=n%2?0:2;__wrap_sub_010275E0();void_regs_save(&actual);
        if(expected_count!=event_count || memcmp(expected_events,events,event_count*sizeof(event)) ||
            memcmp(&expected,&actual,sizeof actual) || memcmp(after_data,GPTR(DATA),DATA_SIZE) || MEM32(FS_BASE)!=0x87654321){
            fprintf(stderr,"case %u failed events %u/%u\n",n,expected_count,event_count);
            for(unsigned i=0;i<expected_count && i<event_count;++i)if(memcmp(&expected_events[i],&events[i],sizeof(event))){
                fprintf(stderr,"event %u kind %u/%u\n",i,expected_events[i].kind,events[i].kind);
                for(unsigned j=0;j<sizeof(event)/4;++j)if(((uint32_t*)&expected_events[i])[j]!=((uint32_t*)&events[i])[j])
                    fprintf(stderr,"event byte %u: %08x/%08x\n",j*4,((uint32_t*)&expected_events[i])[j],((uint32_t*)&events[i])[j]);
                break;
            }
            for(unsigned j=0;j<sizeof(actual)/4;++j)if(((uint32_t*)&expected)[j]!=((uint32_t*)&actual)[j])
                fprintf(stderr,"state byte %u: %08x/%08x\n",j*4,((uint32_t*)&expected)[j],((uint32_t*)&actual)[j]);
            for(unsigned j=0;j<DATA_SIZE/4;++j)if(((uint32_t*)after_data)[j]!=MEM32(DATA+j*4)){
                fprintf(stderr,"data +%x: %08x/%08x\n",j*4,((uint32_t*)after_data)[j],MEM32(DATA+j*4));break;}
            return 1;
        }
    }
    assert(totals[0] && totals[1] && totals[2] && totals[3]);
    printf("PASS native AI: 14000 graphs, %u evaluations, %u costs, %u yields, %u nonpositive costs; callback state/order/args, graph mutations and outgoing state exact\n",totals[0],totals[1],totals[2],totals[3]);
    munmap(mapped,0x2000000);return 0;
}
