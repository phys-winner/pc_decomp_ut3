#include "host.h"
#include <assert.h>
#include <sys/mman.h>
#include "render_occlusion_native_under_test.c"

recomp_ehframe_t *g_eh_frames;uint32_t g_eh_obj,g_eh_info;
enum { DATA=0x100000,ARENA_SIZE=0x6000,STATE=DATA,VIEW=DATA+0x800,PRIMITIVE=DATA+0x1000,
       HISTORY=DATA+0x1400,SETTINGS=DATA+0x1800,RHI=DATA+0x2000,VTABLE=DATA+0x2400,
       QUERY=DATA+0x2800,STACK=DATA+0x5000,RESULT=DATA+0x3000,QUERY_FUNCTION=0x7FF00010 };
static const uint32_t global_at[]={0x30000,0x1D324E0,0x1DEB3E8,0x1DEB464,0x1B1FCC4,
    0x1DF8B8C,0x1DF8B90,0x1DF8B94,0x1DF8B98,0x1DF8B9C,0x1DF8BD8,0x1DF8BDC,
    0x173D9B8,0x17343D8,0x175B5F8};
typedef struct {uint8_t data[ARENA_SIZE];uint32_t globals[sizeof global_at/sizeof global_at[0]];} memory_image;
typedef struct {unsigned kind;void_regs regs;uint32_t args[4];memory_image memory;} event;
static event events[16],expected_events[16];
static memory_image initial,expected_memory;
static unsigned event_count,case_number,total_events,total_yields,query_calls,submissions;
static uint32_t random_state=0x95DA71F3;
static uint32_t random_u32(void){random_state=random_state*1664525u+1013904223u;return random_state;}
static void save_memory(memory_image *image)
{memcpy(image->data,GPTR(DATA),ARENA_SIZE);for(unsigned i=0;i<sizeof global_at/sizeof global_at[0];++i)image->globals[i]=MEM32(global_at[i]);}
static void load_memory(const memory_image *image)
{memcpy(GPTR(DATA),image->data,ARENA_SIZE);for(unsigned i=0;i<sizeof global_at/sizeof global_at[0];++i)MEM32(global_at[i])=image->globals[i];}
const char *void_data_dir(void){return "/nonexistent";}
void void_log(const char *text,...){(void)text;}
void void_dispatch_add(uint32_t address,recomp_func_t function){(void)address;(void)function;}
static void record(unsigned kind)
{
    assert(event_count<16);event *e=&events[event_count++];memset(e,0,sizeof *e);e->kind=kind;
    void_regs_save(&e->regs);for(unsigned i=0;i<4;++i)e->args[i]=ARG(i);save_memory(&e->memory);
}
static void clobber(unsigned kind)
{
    g_eax=0xA1000000+kind;g_ecx=0xC1000000+kind;g_edx=0xD1000000+kind;
    g_flag_k=FK_CMP;g_flag_a=kind;g_flag_b=42;g_flag_cf=1;
    g_xmm[0].u32[0]=0x3F400000+kind;g_xmm[5].u32[1]=0x11220000+kind;
}
void sub_01178E50(void)
{record(1);assert(g_ecx==STATE+0x1E4);clobber(1);g_eax=case_number%7?HISTORY:0;g_esp+=8;}
void sub_0117ACC0(void)
{record(2);memcpy(GPTR(HISTORY),GPTR(ARG(0)),20);clobber(2);g_eax=HISTORY;g_esp+=8;}
void sub_00B55380(void)
{record(3);MEM32(0x1DEB3E8)=RHI;clobber(3);g_esp+=4;}
void sub_0117AA10(void)
{record(4);MEM32(ARG(0))=0;MEM32(HISTORY+24)++;clobber(4);g_esp+=8;}
void sub_01178C60(void){__real_sub_01178C60();}
void sub_005D75E0(void)
{
    record(5);MEM32(g_ecx)=MEM32(g_ecx)*196314165u+907633515u;
    clobber(5);fp_push((case_number%5)*0.25);g_esp+=4;
}
void sub_0117AAB0(void)
{record(6);++submissions;memcpy(GPTR(QUERY),GPTR(ARG(0)),28);clobber(6);g_eax=QUERY;g_esp+=8;}
void sub_01172010(void)
{record(7);MEM32(g_ecx)=ARG(0);clobber(7);g_esp+=8;}
static void query_result(void)
{
    record(8);++query_calls;assert(ARG(2)==1);
    static const uint32_t pixels[]={0,1,127,UINT32_MAX,0x80000000};
    MEM32(ARG(1))=pixels[(case_number/3)%5];
    if(case_number%23==0)MEM32(VIEW+0x59C)=1;
    if(case_number%29==0)MEM32(STATE+0x1D0)=1;
    clobber(8);g_eax=case_number%3?1:0;g_esp+=16;
}
recomp_func_t void_resolve(uint32_t address)
{record(9);assert(address==QUERY_FUNCTION);return query_result;}
static void yield_probe(void)
{
    record(10);++total_yields;
    if(case_number%13==0){g_flag_k=FK_TEST;g_flag_a=g_flag_b=g_flag_cf=0;}
    if(case_number%17==0){g_eax^=0x300;g_xmm[6].u32[1]^=0x55AA5500;}
}
static void put_float(uint32_t address,float value){MEMF(address)=value;}
static void setup(unsigned n,int bounds)
{
    case_number=n;event_count=0;g_fs_base=0x30000;memset(GPTR(DATA),0xA5,ARENA_SIZE);
    for(unsigned i=0;i<sizeof global_at/sizeof global_at[0];++i)MEM32(global_at[i])=0;
    MEM32(FS_BASE)=DATA+0x4800;MEM32(0x1D324E0)=0x35F51B88;MEM32(0x1DEB3E8)=n%19?RHI:0;MEM32(0x1DEB464)=SETTINGS;
    MEM32(RHI)=VTABLE;MEM32(VTABLE+0x90)=QUERY_FUNCTION;
    put_float(0x173D9B8,4294967296.0f);put_float(0x17343D8,1.0f);put_float(0x175B5F8,1000.0f);
    MEM32(0x1DF8B9C)=n%4;put_float(0x1B1FCC4,1.1f);put_float(0x1DF8B98,0.5f);
    put_float(0x1DF8B8C,0.3f);put_float(0x1DF8B90,0.4f);put_float(0x1DF8B94,0.5f);
    MEM32(0x1DF8BDC)=n%2;MEM32(0x1DF8BD8)=0x12345678;
    MEM32(STATE+4)=n%11==0;MEM32(STATE+0x1D0)=n%13==0;
    MEM32(VIEW+0x598)=n%17==0;MEM32(VIEW+0x59C)=n%31==0;
    put_float(VIEW+0x2C,n%37?1280.0f:0.0f);put_float(VIEW+0x30,n%41?720.0f:0.0f);
    MEM32(VIEW+0x1B8)=n%3?1:0;
    put_float(VIEW+0x1BC,0.2f);put_float(VIEW+0x1C0,-0.5f);put_float(VIEW+0x1C4,0.75f);put_float(VIEW+0x1C8,(n%5)*50.0f-100.0f);
    put_float(SETTINGS+0x334,0.1f);put_float(SETTINGS+0x338,(n%4)*0.25f);
    MEM32(PRIMITIVE+8)=0x12345000;MEM8(PRIMITIVE+0x34)=n%2;
    for(unsigned i=0;i<7;++i)put_float(PRIMITIVE+16+i*4u,(n%7)*20.0f+i);
    MEM32(HISTORY)=0x12345000;MEM32(HISTORY+4)=n%4?QUERY:0;put_float(HISTORY+8,(n%5)*0.2f);put_float(HISTORY+12,0.0f);put_float(HISTORY+16,0.0f);
    g_eax=bounds?RESULT:random_u32();g_ecx=bounds?PRIMITIVE+16:STATE;g_edx=random_u32();g_ebx=random_u32();
    g_esp=STACK;g_ebp=random_u32();g_esi=random_u32();g_edi=random_u32();
    g_flag_k=FK_TEST;g_flag_a=random_u32();g_flag_b=random_u32();g_flag_cf=1;
    g_backedges=n%3?0:RECOMP_YIELD_EVERY-1;recomp_yield_hook=yield_probe;
    for(unsigned i=0;i<8;++i){g_st[i]=(double)(int32_t)random_u32()/13.;
        for(unsigned j=0;j<4;++j)g_xmm[i].u32[j]=random_u32();}
    MEM32(STACK)=RECOMP_RETADDR;ARG(0)=PRIMITIVE;ARG(1)=VIEW;put_float(STACK+12,(n%11)*0.15f);
    if(bounds){
        static const uint32_t edge[]={0,0x80000000,1,0x80000001,0x3F800000,0xBF800000,0x7F7FFFFF,0xFF7FFFFF,
            0x7F800000,0xFF800000,0x7FC00001,0x7FC12345,0x7FA54321,0xFFC45678};
        if(n%3==0)for(unsigned i=0;i<7;++i)MEM32(g_ecx+i*4)=edge[(n+i)%14];
        if(n%5==0)for(unsigned i=0;i<7;++i)MEM32(g_ecx+i*4)=random_u32();
        if(n%7==0){MEM32(0x1B1FCC4)=edge[n%14];MEM32(0x1DF8B98)=edge[(n+1)%14];
            MEM32(0x1DF8B8C)=edge[(n+2)%14];MEM32(0x1DF8B90)=edge[(n+3)%14];MEM32(0x1DF8B94)=edge[(n+4)%14];}
        if(n%4==0)g_eax=g_ecx+(n%13)*4u-24u;
        if(n%11==0)g_eax=STACK+(n%7)*4u-12u;
    }
}
static int check(int bounds)
{
    void_regs before={0},expected={0},actual={0};void_regs_save(&before);save_memory(&initial);
    render_occlusion_mode=1;if(bounds)__wrap_sub_01178C60();else __wrap_sub_0117AEC0();
    void_regs_save(&expected);save_memory(&expected_memory);
    const unsigned expected_count=event_count;total_events+=event_count;
    memcpy(expected_events,events,event_count*sizeof(event));
    load_memory(&initial);void_regs_load(&before);event_count=0;render_occlusion_mode=case_number&1?0:2;
    if(bounds)__wrap_sub_01178C60();else __wrap_sub_0117AEC0();void_regs_save(&actual);
    memory_image actual_memory;save_memory(&actual_memory);
    if(memcmp(&expected,&actual,sizeof actual)||memcmp(&expected_memory,&actual_memory,sizeof actual_memory)||
        event_count!=expected_count||memcmp(events,expected_events,event_count*sizeof(event))){
        fprintf(stderr,"occlusion case %u bounds %d failed events %u/%u\n",case_number,bounds,expected_count,event_count);
        for(unsigned i=0;i<sizeof actual/4;++i)if(((uint32_t*)&expected)[i]!=((uint32_t*)&actual)[i])
            fprintf(stderr,"state+%u %08x != %08x\n",i*4,((uint32_t*)&expected)[i],((uint32_t*)&actual)[i]);
        for(unsigned e=0;e<expected_count&&e<event_count;++e)if(memcmp(&events[e],&expected_events[e],sizeof(event))){
            fprintf(stderr,"event %u kind %u/%u\n",e,expected_events[e].kind,events[e].kind);
            for(unsigned i=0;i<sizeof(void_regs)/4;++i)if(((uint32_t*)&expected_events[e].regs)[i]!=((uint32_t*)&events[e].regs)[i])
                fprintf(stderr,"event state+%u %08x != %08x\n",i*4,((uint32_t*)&expected_events[e].regs)[i],((uint32_t*)&events[e].regs)[i]);break;}
        for(unsigned i=0;i<sizeof actual_memory/4;++i)if(((uint32_t*)&expected_memory)[i]!=((uint32_t*)&actual_memory)[i]){
            fprintf(stderr,"memory+%x %08x != %08x\n",i*4,((uint32_t*)&expected_memory)[i],((uint32_t*)&actual_memory)[i]);break;}
        return 0;
    }
    return 1;
}
static void raw_main(unsigned n)
{
    static const uint32_t edge[]={0,0x80000000,1,0x80000001,0x3F800000,0xBF800000,0x7F7FFFFF,0xFF7FFFFF,
        0x7F800000,0xFF800000,0x7FC00001,0x7FC12345,0x7FA54321,0xFFC45678,0xFFA11111};
    static const uint32_t fields[]={VIEW+0x2C,VIEW+0x30,VIEW+0x1BC,VIEW+0x1C0,VIEW+0x1C4,VIEW+0x1C8,
        STACK+12,HISTORY+8,SETTINGS+0x334,SETTINGS+0x338,PRIMITIVE+16,PRIMITIVE+20,PRIMITIVE+24,
        PRIMITIVE+28,PRIMITIVE+32,PRIMITIVE+36,PRIMITIVE+40,0x1B1FCC4,0x1DF8B98,0x1DF8B8C,0x1DF8B90,0x1DF8B94};
    setup(100000+n,0);MEM32(STATE+4)=MEM32(STATE+0x1D0)=MEM32(VIEW+0x598)=MEM32(VIEW+0x59C)=0;
    MEM32(HISTORY+4)=n%3?QUERY:0;MEM32(VIEW+0x1B8)=n%2;
    for(unsigned i=0;i<sizeof fields/sizeof fields[0];++i)MEM32(fields[i])=n&1?random_u32():edge[(n/2+i)%15];
    g_backedges=RECOMP_YIELD_EVERY-1;
    for(unsigned i=0;i<8;++i){uint64_t bits=((uint64_t)random_u32()<<32)|random_u32();memcpy(&g_st[i],&bits,8);}
}
int main(void)
{
    assert(occlusion_load_mode()==0);
    void *mapped=mmap((void*)VOID_GUEST_BASE,0x2000000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    assert(mapped==(void*)VOID_GUEST_BASE);
    for(unsigned n=0;n<12000;++n){setup(n,0);if(!check(0))return 1;}
    for(unsigned n=0;n<24000;++n){setup(n,1);if(!check(1))return 1;}
    for(unsigned n=0;n<16000;++n){raw_main(n);if(!check(0))return 1;}
    /* Exercise both backedges independently of randomized branch correlations. */
    for(unsigned n=0;n<1000;++n){
        setup(16,0);g_backedges=RECOMP_YIELD_EVERY-1;
        if(n&1){MEM32(VIEW+0x1B8)=1;put_float(VIEW+0x1C8,-100000.0f);}
        else {MEM32(VIEW+0x1B8)=0;MEM8(PRIMITIVE+0x34)=1;}
        if(!check(0))return 1;
    }
    assert(total_events&&total_yields&&query_calls&&submissions);
    printf("PASS native occlusion: 53000 cases, %u boundaries, %u yield callbacks, %u query callbacks, %u submissions; full memory/state exact\n",
        total_events,total_yields,query_calls,submissions);
    munmap(mapped,0x2000000);return 0;
}
