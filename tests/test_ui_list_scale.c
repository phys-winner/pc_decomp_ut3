/* Actual formatter wrapper and live typed getter; original formatter is the
 * fixture boundary. The separate PE oracle executes its real style/nodes. */
#include "ui_list_test_platform.h"
#include <assert.h>
#include <float.h>
#include "../android/host/touch_game.c"
#include "../android/host/ui_list_scale.c"

enum { LIST_TEST_SIZE=0x02000000, ENGINE=0x100000,VIEWPORT=0x110000,ENGINE_CLASS=0x120000,
    PORT_CLASS=0x121000,OBJECT_KIND=0x122000,INT_KIND=0x123000,FLOAT_KIND=0x124000,
    VP_FIELD=0x130000,MODE_FIELD=0x131000,EPOCH_FIELD=0x132000,VEHICLE_FIELD=0x133000,
    STORE_FIELD=0x134000,MENU_FIELD=0x135000,SETTINGS_FIELD=0x136000,SCALE_FIELD=0x137000,
    STORE_CLASS=0x140000,SETTINGS_CLASS=0x141000,STRING_CLASS=0x142000,LIST_CLASS=0x143000,
    STORE=0x150000,SETTINGS=0x151000,STRING=0x160000,LIST=0x161000,
    STACK=0x170000,BOUNDS=0x171000,NODES=0x180000,PROVIDER=0x181000 };
static int owned=1,calls,reads,deliveries,menu_checks,hud_checks,expect_scaled;
static uint32_t denied,readonly,names,storage;
static float expected;
static uint8_t original_policy[8];
static uint32_t original_flags;
typedef struct { void_regs regs;uint16_t segments[6];uint32_t gs,es;ptrdiff_t base;
    double fp[RECOMP_X87_SLOTS];int64_t integers[RECOMP_X87_SLOTS];int sp;
} snapshot;
static snapshot entry;

int void_machine_is_owned(void) { return owned; }
int void_touch_menu_pending(void) { ++menu_checks;return 1; }
void void_touch_menu_delivered(void) { ++deliveries; }
int void_ui_hud_scale_observe(float scale) { (void)scale;++hud_checks;return 1; }
int void_vm_readable(uint32_t a,uint32_t n)
{ ++reads;errno=ERANGE;return a&&n&&n<=LIST_TEST_SIZE&&a<=LIST_TEST_SIZE-n&&(!denied||a+n<=denied||a>=denied+4); }
int void_vm_writable(uint32_t a,uint32_t n)
{ return void_vm_readable(a,n)&&(!readonly||a+n<=readonly||a>=readonly+4); }
static void put(uint32_t a,uint32_t n) { memcpy(GPTR(a),&n,4); }
static void putf(uint32_t a,float n) { memcpy(GPTR(a),&n,4); }
static float getf(uint32_t a) { float n;memcpy(&n,GPTR(a),4);return n; }
static void name(uint32_t object,const char *s)
{
    put(object+NAME_OFFSET,names);put(0x1000+names++*4,storage);
    for(unsigned i=0;;++i) { uint16_t c=(unsigned char)s[i];memcpy(GPTR(storage+16+2*i),&c,2);if(!c)break; }
    storage+=0x100;
}
static void field(uint32_t a,const char *s,uint32_t kind,uint32_t off,uint32_t next)
{ name(a,s);put(a+CLASS_OFFSET,kind);put(a+PROPERTY_OFFSET,off);put(a+NEXT_OFFSET,next); }
static void save(snapshot *s)
{
    memset(s,0,sizeof *s);void_regs_save(&s->regs);
    uint16_t segments[]={g_seg_cs,g_seg_ds,g_seg_es,g_seg_fs,g_seg_gs,g_seg_ss};
    memcpy(s->segments,segments,sizeof segments);s->gs=g_gs_base;s->es=g_es_base;s->base=g_mem_base;
    memcpy(s->fp,g_st_win,sizeof s->fp);memcpy(s->integers,g_st_i64_win,sizeof s->integers);s->sp=g_fp_sp;
}
static void seed(void)
{
    g_eax=0x1234;g_ecx=STRING;g_edx=0x5678;g_ebx=0x4567;g_esp=STACK;g_ebp=0x789A;g_esi=0xABCD;g_edi=0xCDEF;
    g_flag_k=1;g_flag_a=2;g_flag_b=3;g_flag_cf=4;g_fs_base=0x45678;g_gs_base=0x56789;g_es_base=0x6789A;
    g_cur_func=0xF75DB0;g_seg_cs=1;g_seg_ds=2;g_seg_es=3;g_seg_fs=4;g_seg_gs=5;g_seg_ss=6;
    g_fp_sp=120;g_fp_top=3;void_fpu_cmp=9;g_fpu_cw=0x27F;g_fp_round_mask=~1ull;g_fp_store_mask=~2ull;
    for(unsigned i=0;i<RECOMP_X87_SLOTS;++i) { uint64_t bits=0x7FF8000000000123ull+i;memcpy(g_st_win+i,&bits,8);g_st_i64_win[i]=9007199254740993ll+i; }
    memset(g_xmm,0xA7,sizeof g_xmm);memset(g_mm,0xB8,sizeof g_mm);g_mxcsr=0x1F80;
    g_eh_frames=(void *)(uintptr_t)0xABCD0;g_eh_obj=0x12345;g_eh_info=0x23456;g_backedges=0x34567;
    save(&entry);errno=E2BIG;
}
static void fixture(float scale,float baseline)
{
    memset(GPTR(0),0,LIST_TEST_SIZE);memset(&touch_metadata,0,sizeof touch_metadata);list_format_ready=0;
    owned=1;calls=reads=deliveries=menu_checks=hud_checks=0;denied=readonly=0;names=1;storage=0x10000;
    put(0x1DD46E8,0x1000);put(0x1DEB464,ENGINE);
    name(ENGINE_CLASS,"Engine");name(PORT_CLASS,"AndroidPortViewportClient");
    name(OBJECT_KIND,"ObjectProperty");name(INT_KIND,"IntProperty");name(FLOAT_KIND,"FloatProperty");
    name(STORE_CLASS,"AndroidPortDataStore");name(SETTINGS_CLASS,"AndroidPortSettings");
    name(STRING_CLASS,"UIListString");name(LIST_CLASS,"UIList");
    put(ENGINE+CLASS_OFFSET,ENGINE_CLASS);put(VIEWPORT+CLASS_OFFSET,PORT_CLASS);
    put(ENGINE_CLASS+CHILDREN_OFFSET,VP_FIELD);field(VP_FIELD,"GameViewport",OBJECT_KIND,0x80,0);put(ENGINE+0x80,VIEWPORT);
    put(PORT_CLASS+CHILDREN_OFFSET,MODE_FIELD);
    field(MODE_FIELD,"PortInputMode",INT_KIND,0x90,EPOCH_FIELD);
    field(EPOCH_FIELD,"PortInputEpoch",INT_KIND,0x94,VEHICLE_FIELD);
    field(VEHICLE_FIELD,"PortVehicleProfile",INT_KIND,0x98,STORE_FIELD);
    field(STORE_FIELD,"PortStore",OBJECT_KIND,0xD0,MENU_FIELD);field(MENU_FIELD,"PortMenuRequest",INT_KIND,0xD4,0);
    put(VIEWPORT+0x90,2);put(VIEWPORT+0x94,1);put(VIEWPORT+0xD0,STORE);
    put(STORE+CLASS_OFFSET,STORE_CLASS);put(STORE_CLASS+CHILDREN_OFFSET,SETTINGS_FIELD);
    field(SETTINGS_FIELD,"Settings",OBJECT_KIND,0x80,0);put(STORE+0x80,SETTINGS);
    put(SETTINGS+CLASS_OFFSET,SETTINGS_CLASS);put(SETTINGS_CLASS+CHILDREN_OFFSET,SCALE_FIELD);
    field(SCALE_FIELD,"uiScale",FLOAT_KIND,0x80,0);putf(SETTINGS+0x80,scale);
    /* Independent original PE/header oracle establishes +28; +20 is linker
     * index, and must never be seeded from the production offset constant. */
    put(STRING+CLASS_OFFSET,STRING_CLASS);put(STRING+0x20,UINT32_MAX);put(STRING+0x24,UINT32_MAX);
    put(STRING+0x28,LIST);put(LIST+CLASS_OFFSET,LIST_CLASS);
    put(STACK,0xDEAD0000);put(STACK+4,BOUNDS);put(STACK+8,0);putf(BOUNDS+0x10,baseline);putf(BOUNDS+0x14,baseline);
    const uint8_t code[]={0x6A,0xFF,0x68,0xC5,0x9A,0x69,0x01,0x64,0xA1};memcpy(GPTR(0xF75DB0),code,sizeof code);
    const uint8_t mode[]={0x0F,0xB6,0x87,0xBC,0,0,0};memcpy(GPTR(0xF75E87),mode,sizeof mode);
    putf(STRING+0xB8,0.6f);put(STRING+0xBC,0xAABBCC00);put(STRING+0xE4,0xA5A50000);
    memcpy(original_policy,GPTR(STRING+0xB8),8);memcpy(&original_flags,GPTR(STRING+0xE4),4);
    memset(GPTR(NODES),0x6A,256);memset(GPTR(PROVIDER),0x7B,256);seed();
}
void __real_sub_00F75DB0(void)
{
    snapshot actual;save(&actual);assert(!memcmp(&actual,&entry,sizeof actual));assert(errno==E2BIG);++calls;
    if(expect_scaled) {
        assert(getf(STRING+0xB8)==expected&&getf(STRING+0xDC)==expected&&getf(STRING+0xE0)==expected);
        assert(MEM32(STRING+0xBC)==0xAABBCC01&&MEM32(STRING+0xE4)==(original_flags|1));
    } else assert(!memcmp(GPTR(STRING+0xB8),original_policy,8)&&MEM32(STRING+0xE4)==original_flags);
    putf(STRING+0xDC,expect_scaled?expected:0.875f);putf(STRING+0xE0,expect_scaled?expected:0.875f);
    putf(STRING+0xD4,100*(expect_scaled?expected:0.875f));putf(STRING+0xD8,20*(expect_scaled?expected:0.875f));
    g_eax^=0xAAAABBBB;g_ecx=0xFEEDFACE;g_esp+=12;g_flag_cf^=1;g_xmm[2].u64[0]^=0x1234;g_st_i64_win[123]+=7;
    errno=EINVAL;
}
static void invoke(int scaled)
{
    expect_scaled=scaled;__wrap_sub_00F75DB0();assert(calls==1&&errno==EINVAL);
    assert(!memcmp(GPTR(STRING+0xB8),original_policy,8)&&MEM32(STRING+0xE4)==original_flags);
    for(unsigned i=0;i<256;++i)assert(((uint8_t*)GPTR(NODES))[i]==0x6A&&((uint8_t*)GPTR(PROVIDER))[i]==0x7B);
    assert(!menu_checks&&!deliveries&&!hud_checks&&!MEM32(VIEWPORT+0xD4));
}
int main(void)
{
    assert(VirtualAlloc((void *)(uintptr_t)VOID_GUEST_BASE,LIST_TEST_SIZE,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==(void *)(uintptr_t)VOID_GUEST_BASE);
    for(unsigned n=0;n<3;++n)for(unsigned s=0;s<3;++s) {
        float baseline[]={0.75f,1,1.25f},scale[]={1.01f,1.25f,1.5f};fixture(scale[s],baseline[n]);expected=scale[s]*baseline[n];invoke(1);
        assert(getf(STRING+0xDC)==expected&&getf(STRING+0xD4)==100*expected);
    }
    fixture(1.25f,1);float factor=0;assert(void_touch_ui_list_scale(STRING,&factor)&&factor==1.25f);
    putf(SETTINGS+0x80,1.5f);assert(void_touch_ui_list_scale(STRING,&factor)&&factor==1.5f);assert(!hud_checks&&!menu_checks);
    puts("PASS live typed getter: first call, same-epoch slider change, no menu delivery/HUD observer/guest write");
    for(unsigned c=0;c<20;++c) {
        fixture(1.5f,1);
        switch(c) {
        case 0:putf(SETTINGS+0x80,1);break;case 1:put(VIEWPORT+0x90,1);break;case 2:owned=0;break;
        case 3:put(STRING+CLASS_OFFSET,LIST_CLASS);break;case 4:put(LIST+CLASS_OFFSET,STORE_CLASS);break;
        case 5:put(STRING+0x28,UINT32_MAX-12);break;case 6:put(VIEWPORT+0xD0,0);break;
        case 7:put(SCALE_FIELD+CLASS_OFFSET,INT_KIND);break;case 8:putf(SETTINGS+0x80,NAN);break;
        case 9:putf(SETTINGS+0x80,INFINITY);break;case 10:putf(SETTINGS+0x80,1.6f);break;
        case 11:putf(BOUNDS+0x14,0.75f);break;case 12:putf(BOUNDS+0x10,0);putf(BOUNDS+0x14,0);break;
        case 13:putf(BOUNDS+0x10,INFINITY);putf(BOUNDS+0x14,INFINITY);break;
        case 14:putf(BOUNDS+0x10,FLT_MAX);putf(BOUNDS+0x14,FLT_MAX);break;
        case 15:put(STRING+0xBC,0xAABBCC02);memcpy(original_policy,GPTR(STRING+0xB8),8);break;
        case 16:readonly=STRING+0xDC;break;case 17:denied=BOUNDS+0x10;break;
        case 18:put(0xF75DB0,0);break;case 19:put(0x1DEB464,0);break;
        }
        invoke(0);
    }
    fixture(1,1);expect_scaled=0;__real_sub_00F75DB0();snapshot reference;save(&reference);
    uint8_t object[0xE8];memcpy(object,GPTR(STRING),sizeof object);
    fixture(1,1);invoke(0);snapshot wrapped;save(&wrapped);
    assert(!memcmp(&reference,&wrapped,sizeof reference)&&!memcmp(object,GPTR(STRING),sizeof object));
    int cold_reads=reads;
    fixture(1,1);void_touch_ui_list_scale(STRING,&factor);reads=0;seed();invoke(0);int default_reads=reads;
    fixture(1.5f,1);put(VIEWPORT+0x90,1);void_touch_ui_list_scale(STRING,&factor);reads=0;seed();invoke(0);
    printf("PASS actual wrapper: 9 relative-factor cases, 20 bypasses, original once, policies/flags/nodes/provider/errno preserved; cold UI reads=%d, warmed UI reads=%d, warmed gameplay-mode reads=%d per formatter\n",cold_reads,default_reads,reads);
    puts("PASS S100 complete original core register/x87-window/segment/object result identical to direct original");
    return 0;
}
