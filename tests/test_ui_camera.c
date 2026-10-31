/* Production Hor+ guard/ABI fixture. The original PE oracle is separate. */
#include "ui_camera_test_platform.h"
#include <assert.h>
#include <float.h>
#include <setjmp.h>
#define RECOMP_RETADDR 0xDEAD0000u
int void_machine_is_owned(void);
#include "../android/host/ui_camera.c"

enum { TEST_SIZE=0x01000000, STACK=0x20008, MATRIX=STACK+0x188, CALLER=STACK+0x1E0, PLAYER=0x567800 };
static int owned=1,calls,reads,expected_entered,view_calls,direct,scope_case;

static uint32_t denied,readonly;
static float initial_args[4],expected_half;
static uint32_t original_near_slot;
typedef struct { void_regs regs;uint16_t segments[6];uint32_t gs,es;ptrdiff_t base;
    double fp[RECOMP_X87_SLOTS];int64_t integers[RECOMP_X87_SLOTS];int sp;
} snapshot;
static snapshot entry,outer_entry,original_output;
static jmp_buf abandoned;

int void_machine_is_owned(void) { return owned; }
int void_vm_readable(uint32_t a,uint32_t n)
{ ++reads;errno=ERANGE;return a&&n&&n<=TEST_SIZE&&a<=TEST_SIZE-n&&(!denied||a+n<=denied||a>=denied+4); }
int void_vm_writable(uint32_t a,uint32_t n)
{ return void_vm_readable(a,n)&&(!readonly||a+n<=readonly||a>=readonly+4); }
static void put(uint32_t a,uint32_t n) { memcpy(GPTR(a),&n,4); }
static void putf(uint32_t a,float n) { memcpy(GPTR(a),&n,4); }
static void save(snapshot *s)
{
    memset(s,0,sizeof *s);void_regs_save(&s->regs);
    uint16_t segments[]={g_seg_cs,g_seg_ds,g_seg_es,g_seg_fs,g_seg_gs,g_seg_ss};
    memcpy(s->segments,segments,sizeof segments);s->gs=g_gs_base;s->es=g_es_base;s->base=g_mem_base;
    memcpy(s->fp,g_st_win,sizeof s->fp);memcpy(s->integers,g_st_i64_win,sizeof s->integers);s->sp=g_fp_sp;
}
static void fixture(float angle,float width,float height,float near_plane)
{
    memset(GPTR(0),0xA6,TEST_SIZE);camera_projection_ready=0;
    owned=1;calls=reads=view_calls=direct=scope_case=0;denied=readonly=0;expected_entered=0;
    memset(&camera_call,0,sizeof camera_call);camera_frame=0;
    const uint8_t code[]={0xD9,0x44,0x24,0x04,0xF3,0x0F,0x10,0x54,0x24,0x10,0xD9,0xF2};
    const uint8_t caller[]={0xE8,0x62,0x06,0x80,0xFF,0xB9,0x10,0,0,0};
    memcpy(GPTR(0x486270),code,sizeof code);memcpy(GPTR(0xC85C09),caller,sizeof caller);
    const uint8_t frame[]={0x55,0x8B,0xEC,0x83,0xE4,0xF0,0x6A,0xFF,0x68,0x89,0xD1,0x66,0x01};
    const uint8_t output[]={0x8D,0x8C,0x24,0x84,0x01,0,0};
    memcpy(GPTR(0xC85710),frame,sizeof frame);memcpy(GPTR(0xC85BF7),output,sizeof output);
    initial_args[0]=angle;initial_args[1]=width;initial_args[2]=height;initial_args[3]=near_plane;
    put(STACK,RECOMP_RETADDR);memcpy(GPTR(STACK+4),initial_args,16);
    g_eax=0x1234;g_ecx=MATRIX;g_edx=0x5678;g_ebx=PLAYER;g_esp=STACK;g_ebp=CALLER-4;g_esi=0xABCD;g_edi=0xCDEF;
    g_flag_k=1;g_flag_a=2;g_flag_b=3;g_flag_cf=4;g_fs_base=0x45678;g_gs_base=0x56789;g_es_base=0x6789A;
    g_cur_func=0x486270;g_seg_cs=1;g_seg_ds=2;g_seg_es=3;g_seg_fs=4;g_seg_gs=5;g_seg_ss=6;
    g_fp_sp=120;g_fp_top=3;void_fpu_cmp=9;g_fpu_cw=0x27F;g_fp_round_mask=~1ull;g_fp_store_mask=~2ull;
    for(unsigned i=0;i<RECOMP_X87_SLOTS;++i) { uint64_t bits=0x7FF8000000000123ull+i;memcpy(g_st_win+i,&bits,8);g_st_i64_win[i]=9007199254740993ll+i; }
    memset(g_xmm,0xA7,sizeof g_xmm);memset(g_mm,0xB8,sizeof g_mm);g_mxcsr=0x1F80;
    g_eh_frames=(void *)(uintptr_t)0xABCD0;g_eh_obj=0x12345;g_eh_info=0x23456;g_backedges=0x34567;
    save(&entry);errno=E2BIG;
}
void __real_sub_00486270(void)
{
    snapshot actual;save(&actual);assert(!memcmp(&actual,&entry,sizeof actual));assert(errno==E2BIG);++calls;
    float args[4];memcpy(args,GPTR(g_esp+4),16);
    assert(!memcmp(args+1,initial_args+1,12));
    if(expected_entered)assert(args[0]==expected_half);
    else assert(!memcmp(args,initial_args,4));
    /* Different outgoing state must survive restoration, including errno. */
    memset(GPTR(g_ecx),0xB7,64);putf(g_ecx,args[0]);putf(g_ecx+20,args[1]/args[2]);
    /* The real ctor overwrites its near argument with cot(half). */
    put(STACK+16,0x3F7ABCDE);original_near_slot=MEM32(STACK+16);
    g_eax^=0xAAAABBBB;g_ecx=0xFEEDFACE;g_esp+=20;g_flag_cf^=1;g_xmm[2].u64[0]^=0x1234;
    g_st_i64_win[123]+=7;g_st_win[124]=3.25;errno=EINVAL;save(&original_output);
}
void __real_sub_00C85710(void)
{
    snapshot actual;save(&actual);assert(!memcmp(&actual,&outer_entry,sizeof actual));assert(errno==E2BIG);++view_calls;
    if(scope_case==1) {
        camera_call_scope previous=camera_call;
        snapshot saved=outer_entry;
        g_esp=CALLER-0x1000;g_ecx=PLAYER+0x1000;save(&outer_entry);
        scope_case=4;__wrap_sub_00C85710();scope_case=1;
        assert(!memcmp(&camera_call,&previous,sizeof previous));
        void_regs_load(&saved.regs);outer_entry=saved;return;
    }
    if(scope_case==4) { assert(camera_call.player==PLAYER+0x1000);return; }
    if(scope_case==2)longjmp(abandoned,1);
    if(scope_case==3) { void_ui_camera_frame_begin();return; }
    void_regs_load(&entry.regs);__wrap_sub_00486270();
}
static void invoke(int entered)
{
    uint32_t output=g_ecx;
    expected_entered=entered;save(&entry);
    if(direct)__wrap_sub_00486270();
    else {
        g_esp=CALLER;g_ecx=PLAYER;save(&outer_entry);__wrap_sub_00C85710();
        assert(view_calls==1&&!camera_call.stack);
    }
    snapshot actual;save(&actual);assert(calls==1&&errno==EINVAL&&!memcmp(&actual,&original_output,sizeof actual));
    assert(!memcmp(GPTR(STACK+4),initial_args,12)&&MEM32(STACK+16)==original_near_slot);
    for(unsigned i=24;i<64;++i)assert(((uint8_t*)GPTR(output))[i]==0xB7);
}
int main(void)
{
    assert(VirtualAlloc((void *)(uintptr_t)VOID_GUEST_BASE,TEST_SIZE,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==(void *)(uintptr_t)VOID_GUEST_BASE);
    unsigned scaled=0,bypass=0;
    const float fovs[]={20,45,60,90,100,120};
    const float sizes[][2]={{1024,768},{1920,1080},{2400,1080},{800,900}};
    for(unsigned s=0;s<4;++s)for(unsigned f=0;f<6;++f) {
        fixture(fovs[f]*0.008726646259971648f,sizes[s][0],sizes[s][1],10);
        int wide=(double)sizes[s][0]/sizes[s][1]>4.0/3.0;
        expected_half=(float)atan(tan((double)initial_args[0])*(double)sizes[s][0]/sizes[s][1]/(4.0/3.0));
        invoke(wide);if(wide)++scaled;else++bypass;
        fixture(fovs[f]*0.008726646259971648f,sizes[s][0],sizes[s][1],10);g_ecx=STACK+0x148;invoke(0);++bypass;
    }
    for(unsigned n=0;n<18;++n) {
        fixture(0.785398185f,2400,1080,10);
        switch(n) {
        case 0:owned=0;break;
        case 1:denied=STACK;break;
        case 2:readonly=STACK+4;break;
        case 3:readonly=MATRIX;break;
        case 4:denied=0x486270;break;
        case 5:denied=0xC85C09;break;
        case 6:put(0x486270,0);break;
        case 7:put(0xC85C09,0);break;
        case 8:put(STACK,0x12345678);break;
        case 9:g_ecx=MATRIX+64;break;
        case 10:put(STACK,0);break;
        case 11:readonly=MATRIX+60;break;
        case 12:put(0xC85710,0);break;
        case 13:put(0xC85BF7,0);break;
        case 14:g_ebp++;break;
        case 15:g_ebx++;break;
        case 16:direct=1;break;
        case 17:direct=1;camera_call=(camera_call_scope){STACK+16,CALLER-4,PLAYER};break;
        }
        invoke(0);if(!owned)assert(!reads);++bypass;
    }
    const float invalid[]={0,-0.0f,-1,NAN,INFINITY,-INFINITY,FLT_MAX};
    for(unsigned a=0;a<4;++a)for(unsigned i=0;i<7;++i) {
        /* FLT_MAX is a valid positive height/near input; exercise extremes
         * separately without inventing an arbitrary range clamp. */
        if(i==6&&a!=0)continue;
        float args[]={0.785398185f,2400,1080,10};args[a]=invalid[i];fixture(args[0],args[1],args[2],args[3]);invoke(0);++bypass;
    }
    fixture(1.57079637f,2400,1080,10);invoke(0);++bypass;
    fixture(0.785398185f,FLT_MAX,FLT_MIN,10);invoke(0);++bypass; /* rounded half reaches pi/2 */
    fixture(0.785398185f,2400,1080,10);scope_case=1;
    g_esp=CALLER;g_ecx=PLAYER;save(&outer_entry);__wrap_sub_00C85710();
    assert(view_calls==2&&!camera_call.stack&&errno==E2BIG);
    fixture(0.785398185f,2400,1080,10);scope_case=2;
    g_esp=CALLER;g_ecx=PLAYER;save(&outer_entry);
    if(!setjmp(abandoned)) { __wrap_sub_00C85710();assert(0); }
    assert(camera_call.stack==STACK);
    camera_call_scope previous=camera_call;uint64_t epoch=camera_frame;
    owned=0;void_ui_camera_frame_begin();assert(camera_frame==epoch&&!memcmp(&previous,&camera_call,sizeof previous));
    owned=1;save(&outer_entry);void_ui_camera_frame_begin();snapshot current;save(&current);
    assert(!memcmp(&outer_entry,&current,sizeof current)&&!camera_call.stack&&camera_frame==epoch+1);
    scope_case=0;direct=1;void_regs_load(&entry.regs);invoke(0);++bypass;
    fixture(0.785398185f,2400,1080,10);camera_call=(camera_call_scope){123,456,789};scope_case=3;
    g_esp=CALLER;g_ecx=PLAYER;save(&outer_entry);__wrap_sub_00C85710();assert(!camera_call.stack);
    printf("PASS production Hor+ wrapper: %u scaled/%u bypass; original once; full guest state/errno/output and input restoration\n",scaled,bypass);
    return 0;
}
