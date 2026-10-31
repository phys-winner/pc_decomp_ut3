/* Actual production HUD helper + exact core register/dispatch/cache routines.
 * Only guest function execution is supplied by focused fixture callbacks. */
#include "ui_hud_test_platform.h"
#include <assert.h>
#include <setjmp.h>
#include <stdarg.h>
#include "ui_hud_core.inc"
#include "../android/host/ui_hud.c"

enum { HUD_TEST_SIZE=0x02000000, ROOT_CLASS=0x120000, CLASS_KIND=0x121000,
       FUNCTION_KIND=0x122000,FLOAT_KIND=0x123000,OBJECT_KIND=0x124000,STRUCT_KIND=0x125000,
       CANVAS_CLASS=0x126000,HUD_OBJECT=0x130000,CANVAS_OBJECT=0x131000,NATIVE_CANVAS=0x132000,
       ENTRIES=0x133000,FIELD=0x140000,FUNCTION_A=0x141000,FUNCTION_B=0x142000,
       FRAME=0x150000,CODE=0x151000,NEST_FRAME=0x152000,NEST_CODE=0x153000,GUEST_STACK=0x180000 };
static int owned=1,logs,pushes,pops,calls,nested,abrupt,expected_scaled,expected_aspect,bypass_geometry,deferred_default,recompute;
static uint32_t denied,readonly,names,name_storage;
static float expected_scale,physical_width;
static jmp_buf escaped;
static void run_crosshair_tests(void);
int void_machine_is_owned(void) { return owned; }
int void_vm_readable(uint32_t address,uint32_t bytes)
{ return address&&bytes&&bytes<=HUD_TEST_SIZE&&address<=HUD_TEST_SIZE-bytes&&(!denied||address+bytes<=denied||address>=denied+4); }
int void_vm_writable(uint32_t address,uint32_t bytes)
{ return void_vm_readable(address,bytes)&&(!readonly||address+bytes<=readonly||address>=readonly+4); }
void void_log(const char *format,...) { (void)format;++logs;errno=ERANGE; }
static void put(uint32_t address,uint32_t value) { memcpy(GPTR(address),&value,4); }
static void putf(uint32_t address,float value) { memcpy(GPTR(address),&value,4); }
static float getf(uint32_t address) { float f;memcpy(&f,GPTR(address),4);return f; }
static void object_name(uint32_t object,const char *text)
{
    put(object+0x2C,names);put(object+0x30,0);put(0x1000+4*names++,name_storage);
    for(unsigned i=0;;++i) { uint16_t c=(unsigned char)text[i];memcpy(GPTR(name_storage+16+2*i),&c,2);if(!c)break; }
    name_storage+=0x100;
}
static void field(uint32_t address,const char *text,uint32_t kind,uint32_t offset,uint32_t next)
{ object_name(address,text);put(address+0x34,kind);put(address+0x64,offset);put(address+0x40,next); }
static void seed(unsigned value)
{
    g_eax=value+1;g_ecx=HUD_OBJECT;g_edx=value+3;g_ebx=value+4;g_esi=value+5;g_edi=value+6;g_ebp=value+7;
    g_flag_k=value+8;g_flag_a=value+9;g_flag_b=value+10;g_flag_cf=value+11;
    g_seg_cs=value+12;g_seg_ds=value+13;g_seg_es=value+14;g_seg_fs=value+15;g_seg_gs=value+16;g_seg_ss=value+17;
    g_fs_base=value+18;g_gs_base=value+19;g_es_base=value+20;g_cur_func=value+21;g_mem_base=VOID_GUEST_BASE+value;
    g_fp_sp=120;g_fp_top=value&7;void_fpu_cmp=value+22;g_fpu_cw=(uint16_t)(0x27F+(value&3));
    g_fp_round_mask=0x1234000000000000ull+value;g_fp_store_mask=0xABCD000000000000ull+value;
    for(unsigned i=0;i<RECOMP_X87_SLOTS;++i) {
        uint64_t bits=0x7FF8000000000000ull+(uint64_t)value*1000+i;
        memcpy(g_st_win+i,&bits,8);g_st_i64_win[i]=INT64_C(9007199254740993)+(int64_t)value*1000+i;
    }
    memset(g_mm,(int)value,sizeof g_mm);memset(g_xmm,(int)value+1,sizeof g_xmm);g_mxcsr=0x1F80+value;
    g_eh_frames=(void *)(uintptr_t)(0xABC000+value);g_eh_obj=value+23;g_eh_info=value+24;g_backedges=value+25;
    errno=100+(int)value;
}
static void native_push(void)
{
    uint32_t sp=g_esp,arg=MEM32(sp+4),count=MEM32(NATIVE_CANVAS+0x28);
    assert(g_ecx==NATIVE_CANVAS&&count<8&&getf(arg)==expected_scale&&getf(arg+20)==expected_scale);
    assert(getf(arg+40)==1&&getf(arg+60)==1);++pushes;
    memcpy(GPTR(ENTRIES+80*count),GPTR(arg),64);put(NATIVE_CANVAS+0x28,count+1);
    memset(GPTR(sp-256),0xAB,256);seed(77);g_fp_sp=64;g_esp=sp+8;
}
static void native_pop(void)
{
    uint32_t sp=g_esp,arg=MEM32(sp+4),count=MEM32(NATIVE_CANVAS+0x28);
    assert(g_ecx==NATIVE_CANVAS+0x24&&count>1);++pops;
    memcpy(GPTR(arg),GPTR(ENTRIES+80*(count-1)),80);put(NATIVE_CANVAS+0x28,count-1);
    memset(GPTR(sp-256),0xCD,256);seed(88);g_fp_sp=160;g_esp=sp+8;
}
static void body(int final)
{
    uint32_t sp=g_esp,frame=MEM32(sp+4),code=MEM32(frame+0xC);
    ++calls;put(frame+0xC,code+(final?4:8));
    if(bypass_geometry) { /* Deliberately invalid draw metadata belongs to the original. */ }
    else if(expected_scaled) {
        assert(fabsf(getf(CANVAS_OBJECT+0x48)-physical_width/expected_scale)<.001f);
        assert(fabsf(getf(CANVAS_OBJECT+0x4C)-1080/expected_scale)<.001f);
        assert(fabsf(getf(HUD_OBJECT+0x504)-256*expected_scale)<.001f);
        assert(fabsf(getf(HUD_OBJECT+0x508)-128*expected_scale)<.001f);
        assert(getf(HUD_OBJECT+0x520)==1.40625f&&getf(HUD_OBJECT+0x524)==1.40625f);
        assert(MEM32(NATIVE_CANVAS+0x28)==2);
    } else assert(getf(CANVAS_OBJECT+0x48)==physical_width&&MEM32(NATIVE_CANVAS+0x28)==1);
    if(expected_aspect)assert(getf(HUD_OBJECT+0x524)==getf(HUD_OBJECT+0x520));
    if(recompute) { putf(HUD_OBJECT+0x520,2);putf(HUD_OBJECT+0x524,3);putf(HUD_OBJECT+0x528,4); }
    if(deferred_default) { deferred_default=0;assert(!void_ui_hud_scale_observe(1.0f)); }
    if(abrupt) { abrupt=0;longjmp(escaped,1); }
    if(nested) {
        nested=0;
        /* A callback may use a different/higher guest stack. There must be no
         * ESP-based false-unwind cleanup while the normal scope is active. */
        g_esp=sp+0x400;put(g_esp,RECOMP_RETADDR);put(g_esp+4,NEST_FRAME);put(g_esp+8,0);
        put(NEST_FRAME+0xC,NEST_CODE);put(NEST_CODE,FUNCTION_A);g_ecx=HUD_OBJECT;
        void_resolve(HUD_FINAL)();assert(MEM32(NATIVE_CANVAS+0x28)==(expected_scaled?2u:1u));
        assert(pushes==expected_scaled&&!pops);
    }
    seed(33);g_esp=sp+12;
}
static void original_virtual(void) { body(0); }
static void original_final(void) { body(1); }
static void fixture(void)
{
    memset(GPTR(0),0,HUD_TEST_SIZE);memset(&hud_metadata,0,sizeof hud_metadata);memset(&hud_scope,0,sizeof hud_scope);
    memset(&crosshair_metadata,0,sizeof crosshair_metadata);memset(&crosshair_scope,0,sizeof crosshair_scope);
    crosshair_original_tile=crosshair_original_position=NULL;crosshair_bound=0;
    memset(resolve_cache,0,sizeof resolve_cache);free(table);table=NULL;table_mask=table_used=0;
    hud_ready=hud_bound=0;hud_scale=hud_requested=1;denied=readonly=0;owned=1;
    pushes=pops=calls=nested=abrupt=expected_scaled=expected_aspect=bypass_geometry=deferred_default=recompute=0;
    physical_width=2400;names=1;name_storage=0x10000;
    put(0x1DD46E8,0x1000);put(HUD_CLASS_GLOBAL,ROOT_CLASS);
    object_name(ROOT_CLASS,"UTHUD");object_name(CLASS_KIND,"Class");object_name(FUNCTION_KIND,"Function");
    object_name(FLOAT_KIND,"FloatProperty");object_name(OBJECT_KIND,"ObjectProperty");object_name(STRUCT_KIND,"StructProperty");
    object_name(CANVAS_CLASS,"Canvas");put(ROOT_CLASS+0x34,CLASS_KIND);
    put(HUD_OBJECT+0x34,ROOT_CLASS);put(CANVAS_OBJECT+0x34,CANVAS_CLASS);
    put(ROOT_CLASS+0x4C,FUNCTION_A);
    field(FUNCTION_A,"DrawGameHUD",FUNCTION_KIND,0,FUNCTION_B);
    field(FUNCTION_B,"DrawPostGameHud",FUNCTION_KIND,0,FIELD);
    field(FIELD,"Canvas",OBJECT_KIND,0x500,FIELD+0x100);
    field(FIELD+0x100,"MapDefaultSize",FLOAT_KIND,0x504,FIELD+0x200);
    field(FIELD+0x200,"CharPortraitSize",STRUCT_KIND,0x508,FIELD+0x300);
    const char *props[]={"ResolutionScale","ResolutionScaleX","HUDScaleX","HUDScaleY"};
    for(unsigned i=0;i<4;++i)field(FIELD+0x300+0x100*i,props[i],FLOAT_KIND,0x520+4*i,i==3?0:FIELD+0x400+0x100*i);
    put(HUD_OBJECT+0x500,CANVAS_OBJECT);putf(HUD_OBJECT+0x504,256);putf(HUD_OBJECT+0x508,128);putf(HUD_OBJECT+0x50C,96);
    putf(HUD_OBJECT+0x520,1.40625f);putf(HUD_OBJECT+0x524,2.34375f);putf(HUD_OBJECT+0x528,1.875f);putf(HUD_OBJECT+0x52C,1.875f);
    putf(CANVAS_OBJECT+0x40,120);putf(CANVAS_OBJECT+0x44,54);putf(CANVAS_OBJECT+0x48,2400);putf(CANVAS_OBJECT+0x4C,1080);
    put(CANVAS_OBJECT+0x64,2400);put(CANVAS_OBJECT+0x68,1080);put(CANVAS_OBJECT+0x6C,NATIVE_CANVAS);
    put(NATIVE_CANVAS+0x24,ENTRIES);put(NATIVE_CANVAS+0x28,1);put(NATIVE_CANVAS+0x2C,8);
    memcpy(GPTR(HUD_VIRTUAL),(uint8_t[]){0x56,0x57,0x8B,0x7C,0x24,0x0C,0x8B,0x47,0x0C},9);
    memcpy(GPTR(HUD_FINAL),(uint8_t[]){0x8B,0x44,0x24,4,0x8B,0x50,0x0C,0x56,0x8B,0x32},10);
    memcpy(GPTR(HUD_PUSH),(uint8_t[]){0x55,0x8B,0xEC,0x83,0xE4,0xF0,0x81,0xEC,0x94,0,0,0},12);
    memcpy(GPTR(HUD_POP),(uint8_t[]){0x55,0x8B,0xEC,0x83,0xE4,0xF0,0x51,0x53,0x8B,0xD9},10);
    void_dispatch_add(HUD_VIRTUAL,original_virtual);void_dispatch_add(HUD_FINAL,original_final);
    void_dispatch_add(HUD_PUSH,native_push);void_dispatch_add(HUD_POP,native_pop);
    seed(1);g_esp=GUEST_STACK;put(g_esp,RECOMP_RETADDR);put(g_esp+4,FRAME);put(g_esp+8,0);
    put(FRAME+0xC,CODE);memcpy(GPTR(CODE),GPTR(FUNCTION_A+0x2C),8);
    void_ui_hud_init();assert(hud_ready);
}
static void state(hud_call_state *value) { memset(value,0,sizeof *value);assert(hud_save(value)); }
static void restored(void)
{
    assert(!hud_scope.active&&MEM32(NATIVE_CANVAS+0x28)==1&&getf(CANVAS_OBJECT+0x40)==120);
    assert(getf(CANVAS_OBJECT+0x48)==physical_width&&getf(CANVAS_OBJECT+0x4C)==1080);
    assert(MEM32(CANVAS_OBJECT+0x64)==(uint32_t)physical_width&&MEM32(CANVAS_OBJECT+0x68)==1080);
    assert(getf(HUD_OBJECT+0x504)==256&&getf(HUD_OBJECT+0x508)==128&&getf(HUD_OBJECT+0x50C)==96);
}
int main(void)
{
    assert(VirtualAlloc((void *)(uintptr_t)VOID_GUEST_BASE,HUD_TEST_SIZE,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==(void *)(uintptr_t)VOID_GUEST_BASE);
    hud_call_state baseline,scaled,current;
    fixture();assert(void_resolve(HUD_VIRTUAL)==original_virtual);
    original_virtual();state(&baseline);assert(!pushes&&!pops);
    fixture();assert(void_ui_hud_scale_observe(1));
    int prior_logs=logs,prior_errno=errno;
    assert(void_ui_hud_scale_observe(1)&&logs==prior_logs&&errno==prior_errno);
    assert(void_resolve(HUD_VIRTUAL)==hud_virtual&&void_resolve(HUD_FINAL)==hud_final&&hud_bound);
    expected_aspect=1;
    uint8_t default_canvas[128],default_hud[64],default_entries[80];
    memcpy(default_canvas,GPTR(CANVAS_OBJECT),sizeof default_canvas);
    memcpy(default_hud,GPTR(HUD_OBJECT+0x500),sizeof default_hud);memcpy(default_entries,GPTR(ENTRIES),sizeof default_entries);
    void_resolve(HUD_VIRTUAL)();state(&scaled);assert(!memcmp(&baseline,&scaled,sizeof baseline));restored();
    assert(!pushes&&!pops&&!memcmp(default_canvas,GPTR(CANVAS_OBJECT),sizeof default_canvas));
    assert(!memcmp(default_hud,GPTR(HUD_OBJECT+0x500),sizeof default_hud));
    assert(!memcmp(default_entries,GPTR(ENTRIES),sizeof default_entries));
    for(unsigned step=1;step<=10;++step) {
        fixture();expected_scale=1+step*.05f;expected_scaled=1;
        assert(void_resolve(HUD_VIRTUAL)==original_virtual); /* Prime actual production cache. */
        assert(void_ui_hud_scale_observe(expected_scale));assert(void_resolve(HUD_VIRTUAL)==hud_virtual);
        void_resolve(HUD_VIRTUAL)();state(&scaled);assert(!memcmp(&baseline,&scaled,sizeof baseline));
        restored();assert(pushes==1&&pops==1&&calls==1);
        assert(void_ui_hud_scale_observe(1)&&void_resolve(HUD_VIRTUAL)==hud_virtual&&void_resolve(HUD_FINAL)==hud_final);
    }
    fixture();expected_scale=1.25f;expected_scaled=1;nested=1;assert(void_ui_hud_scale_observe(expected_scale));
    void_resolve(HUD_VIRTUAL)();restored();assert(pushes==1&&pops==1&&calls==2);
    fixture();expected_scale=1.25f;expected_scaled=1;deferred_default=1;assert(void_ui_hud_scale_observe(expected_scale));
    void_resolve(HUD_VIRTUAL)();restored();assert(hud_requested==1.0f&&hud_scale==1.25f&&hud_bound);
    assert(void_resolve(HUD_VIRTUAL)==hud_virtual&&void_resolve(HUD_FINAL)==hud_final);
    void_ui_hud_frame_begin();assert(hud_scale==1.0f&&hud_bound);
    assert(void_resolve(HUD_VIRTUAL)==hud_virtual&&void_resolve(HUD_FINAL)==hud_final);
    fixture();expected_scale=1.5f;expected_scaled=1;assert(void_ui_hud_scale_observe(expected_scale));abrupt=1;
    if(!setjmp(escaped)) { void_resolve(HUD_VIRTUAL)();assert(0); }
    assert(hud_scope.active&&pushes==1&&!pops);seed(55);g_esp=GUEST_STACK+0x800;state(&current);
    void_ui_hud_frame_begin();state(&scaled);assert(!memcmp(&current,&scaled,sizeof current));restored();assert(pops==1);
    void_ui_hud_frame_begin();assert(pops==1);
    fixture();expected_scale=1.25f;expected_scaled=0;assert(void_ui_hud_scale_observe(expected_scale));
    readonly=CANVAS_OBJECT+0x48;void_resolve(HUD_VIRTUAL)();assert(!pushes&&!pops);restored();
    fixture();expected_scale=1.25f;bypass_geometry=1;assert(void_ui_hud_scale_observe(expected_scale));
    put(NATIVE_CANVAS+0x28,256);put(NATIVE_CANVAS+0x2C,256);
    uint8_t full_canvas[0x80],full_entries[256*80],full_hud[0x40];
    memcpy(full_canvas,GPTR(CANVAS_OBJECT),sizeof full_canvas);memcpy(full_entries,GPTR(ENTRIES),sizeof full_entries);
    memcpy(full_hud,GPTR(HUD_OBJECT+0x500),sizeof full_hud);
    void_resolve(HUD_VIRTUAL)();assert(!pushes&&!pops&&!hud_scope.active&&calls==1);
    assert(MEM32(NATIVE_CANVAS+0x28)==256&&!memcmp(full_canvas,GPTR(CANVAS_OBJECT),sizeof full_canvas));
    assert(!memcmp(full_entries,GPTR(ENTRIES),sizeof full_entries)&&!memcmp(full_hud,GPTR(HUD_OBJECT+0x500),sizeof full_hud));
    fixture();expected_scale=1.25f;assert(void_ui_hud_scale_observe(expected_scale));owned=0;assert(!void_ui_hud_scale_observe(1));owned=1;
    assert(void_ui_hud_scale_observe(NAN)&&hud_bound&&hud_scale==1);assert(void_ui_hud_scale_observe(100)&&hud_bound&&hud_scale==1);
    fixture();put(HUD_CLASS_GLOBAL,0);assert(!void_ui_hud_scale_observe(1.25f)&&!hud_bound);
    fixture();expected_scale=1.25f;expected_scaled=1;assert(void_ui_hud_scale_observe(expected_scale));
    put(CODE,FUNCTION_B);void_resolve(HUD_FINAL)();restored();assert(pushes==1&&pops==1);
    fixture();expected_scale=1.25f;assert(void_ui_hud_scale_observe(expected_scale));
    put(CODE,0xFFFFFFFFu);void_resolve(HUD_VIRTUAL)();restored();assert(!pushes&&!pops);
    fixture();expected_scale=1.25f;bypass_geometry=1;assert(void_ui_hud_scale_observe(expected_scale));
    putf(CANVAS_OBJECT+0x48,NAN);void_resolve(HUD_VIRTUAL)();assert(!pushes&&!pops&&isnan(getf(CANVAS_OBJECT+0x48)));
    fixture();expected_scale=1.25f;expected_scaled=1;assert(void_ui_hud_scale_observe(expected_scale));abrupt=1;
    if(!setjmp(escaped)) { void_resolve(HUD_VIRTUAL)();assert(0); }
    put(HUD_OBJECT+0x500,CANVAS_OBJECT+0x200);put(CANVAS_OBJECT+0x200+0x34,CANVAS_CLASS);
    uint8_t other[128];memcpy(other,GPTR(CANVAS_OBJECT+0x200),sizeof other);
    seed(55);g_esp=GUEST_STACK+0x800;void_ui_hud_frame_begin();
    assert(!hud_scope.active&&!pops&&!memcmp(other,GPTR(CANVAS_OBJECT+0x200),sizeof other));
    fixture();hud_ready=0;put(HUD_PUSH,0);void_ui_hud_init();assert(!hud_ready&&!hud_bound);
    assert(void_resolve(HUD_VIRTUAL)==original_virtual&&!void_ui_hud_scale_observe(1.25f));
    fixture();put(FIELD+0x100+0x64,0x500);assert(!void_ui_hud_scale_observe(1.25f)&&!hud_bound);
    assert(MEM32(HUD_OBJECT+0x500)==CANVAS_OBJECT); /* Malformed typed aliases cannot corrupt the pointer. */
    fixture();expected_scale=1.25f;expected_scaled=1;assert(void_ui_hud_scale_observe(expected_scale));abrupt=1;
    if(!setjmp(escaped)) { void_resolve(HUD_VIRTUAL)();assert(0); }
    putf(ENTRIES+80,0);putf(CANVAS_OBJECT+0x48,999); /* Same address/class, different transform identity. */
    seed(55);g_esp=GUEST_STACK+0x800;void_ui_hud_frame_begin();
    assert(!hud_scope.active&&!pops&&getf(CANVAS_OBJECT+0x48)==999);

    /* Source-authored214x87 artwork must keep its ratio at4:3,16:9,20:9.
     * Full physical anchors/clip stay independent of the artwork scale. */
    for(unsigned ratio=0;ratio<3;++ratio)for(unsigned large=0;large<2;++large) {
        fixture();physical_width=(float[]){1440,1920,2400}[ratio];
        putf(CANVAS_OBJECT+0x48,physical_width);put(CANVAS_OBJECT+0x64,(uint32_t)physical_width);
        putf(HUD_OBJECT+0x524,physical_width/1024);expected_scale=large?1.5f:1;
        expected_scaled=(int)large;expected_aspect=1;
        assert(void_ui_hud_scale_observe(expected_scale));
        if(!ratio&&!large)assert(!hud_enter(HUD_OBJECT)); /* Identical4:3bits: no active scope. */
        void_resolve(HUD_VIRTUAL)();restored();assert(pushes==(int)large&&pops==(int)large);
        assert(getf(HUD_OBJECT+0x524)==physical_width/1024);
        assert(getf(HUD_OBJECT+0x520)==1.40625f&&getf(HUD_OBJECT+0x528)==1.875f&&getf(HUD_OBJECT+0x52C)==1.875f);
    }
    fixture();expected_aspect=1;assert(void_ui_hud_scale_observe(1));
    readonly=CANVAS_OBJECT+0x48;void_resolve(HUD_VIRTUAL)();restored();assert(!pushes&&!pops);
    fixture();expected_aspect=1;bypass_geometry=1;assert(void_ui_hud_scale_observe(1));
    put(NATIVE_CANVAS+0x28,256);put(NATIVE_CANVAS+0x2C,256);void_resolve(HUD_VIRTUAL)();
    assert(!pushes&&!pops&&!hud_scope.active&&MEM32(NATIVE_CANVAS+0x28)==256&&getf(HUD_OBJECT+0x524)==2.34375f);
    fixture();expected_aspect=1;nested=1;assert(void_ui_hud_scale_observe(1));
    void_resolve(HUD_VIRTUAL)();restored();assert(!pushes&&!pops&&calls==2&&getf(HUD_OBJECT+0x524)==2.34375f);
    fixture();expected_aspect=1;assert(void_ui_hud_scale_observe(1));put(CODE,FUNCTION_B);
    void_resolve(HUD_FINAL)();restored();assert(!pushes&&!pops&&getf(HUD_OBJECT+0x524)==2.34375f);
    fixture();expected_aspect=1;assert(void_ui_hud_scale_observe(1));abrupt=1;
    if(!setjmp(escaped)) { void_resolve(HUD_VIRTUAL)();assert(0); }
    assert(hud_scope.active&&getf(HUD_OBJECT+0x524)==1.40625f&&!pushes&&!pops);
    seed(55);g_esp=GUEST_STACK+0x800;state(&current);void_ui_hud_frame_begin();state(&scaled);
    assert(!memcmp(&current,&scaled,sizeof current));restored();assert(getf(HUD_OBJECT+0x524)==2.34375f&&!pushes&&!pops);
    for(unsigned large=0;large<2;++large) {
        fixture();expected_scaled=(int)large;expected_scale=large?1.5f:1;expected_aspect=1;recompute=1;
        assert(void_ui_hud_scale_observe(expected_scale));void_resolve(HUD_VIRTUAL)();restored();
        assert(getf(HUD_OBJECT+0x520)==2&&getf(HUD_OBJECT+0x524)==3&&getf(HUD_OBJECT+0x528)==4);
    }
    for(unsigned reason=0;reason<3;++reason) {
        fixture();expected_aspect=1;assert(void_ui_hud_scale_observe(1));putf(ENTRIES,1);abrupt=1;
        if(!setjmp(escaped)) { void_resolve(HUD_VIRTUAL)();assert(0); }
        if(reason==0)putf(ENTRIES,2); /* Same addresses but foreign transform identity. */
        else if(reason==1)put(HUD_OBJECT+0x500,CANVAS_OBJECT+0x200);
        else putf(HUD_OBJECT+0x524,3); /* Later original recalculation owns its result. */
        seed(55);g_esp=GUEST_STACK+0x800;void_ui_hud_frame_begin();
        assert(!hud_scope.active&&!pushes&&!pops);
        assert(getf(HUD_OBJECT+0x524)==(reason==2?3:1.40625f));
    }
    for(unsigned property=0;property<4;++property)for(unsigned bad=0;bad<5;++bad) {
        fixture();bypass_geometry=1;assert(void_ui_hud_scale_observe(1));
        putf(HUD_OBJECT+0x520+4*property,(float[]){NAN,INFINITY,0,-1,65537}[bad]);
        uint8_t before[64];memcpy(before,GPTR(HUD_OBJECT+0x500),sizeof before);
        void_resolve(HUD_VIRTUAL)();assert(!hud_scope.active&&!pushes&&!pops);
        assert(!memcmp(before,GPTR(HUD_OBJECT+0x500),sizeof before));
    }
    fixture();assert(void_ui_hud_scale_observe(1));readonly=HUD_OBJECT+0x524;
    void_resolve(HUD_VIRTUAL)();assert(!hud_scope.active&&!pushes&&!pops&&getf(HUD_OBJECT+0x524)==2.34375f);
    fixture();put(HUD_CLASS_GLOBAL,0);assert(!void_ui_hud_scale_observe(1)&&!hud_bound);
    puts("PASS production HUD: default aspect-only dispatch/cache, zero matrix/Canvas/map/portrait changes; 10scale steps; full guest GPR/flags/MMX/XMM/256x87+sp/segments/bases/EH/budget/errno and scratch preservation; anchors/compensation restore");
    puts("PASS production HUD: normal higher-stack nested callback single push/pop; abrupt nonlocal exit recovers only at trusted producer; identity/read-only/owner/NaN/missing-class guards");
    puts("PASS production HUD aspect: 4:3/16:9/20:9 at100/150percent; both drawtargets; default nested/nonlocal cleanup without matrix calls; foreign identity and original scale recomputation preserved;20invalid float cases delegated");
    run_crosshair_tests();
    return 0;
}
#include "test_ui_crosshair.inc"
