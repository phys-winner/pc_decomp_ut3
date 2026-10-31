/* x86 COM/guest memory boundary for native ARM64 DXVK. */
#include "com.h"
#include <errno.h>
#include <time.h>
#include "../dxvk/query_wait.inc"

typedef struct dxvk_object {void_com_object com;void *native;unsigned kind;struct dxvk_object *next;} dxvk_object;
#define DXVK_OBJECT_BUCKETS 1024u
static dxvk_object *dxvk_objects[DXVK_OBJECT_BUCKETS];
static unsigned dxvk_object_bucket(unsigned kind,void *native)
{
    uintptr_t key=(uintptr_t)native>>4;
    return (unsigned)(key^(key>>11)^((uintptr_t)kind*0x9E3779B1u))&(DXVK_OBJECT_BUCKETS-1u);
}
extern uint32_t ut3_dxvk_native_call(unsigned,void *,unsigned,const uint32_t *);
extern uint32_t ut3_dxvk_native_create9(uint32_t);
extern uint32_t ut3_dxvk_native_addref(void *);
extern uint32_t ut3_dxvk_native_release(void *);
static uint32_t dxvk_guest_call(uint32_t,unsigned,unsigned,const uint32_t *);
static void dxvk_return_float(uint32_t bits)
{
    float value;memcpy(&value,&bits,4);
#ifdef RECOMP_X87_WINDOW
    fp_window_push(value);
#else
    fp_push_impl(g_st,&g_fp_top,value);
#endif
}
#include "../../build/dxvk/guest_methods.inc"

void *ut3_dxvk_guest_memory(uint32_t va,uint32_t bytes,int write)
{
    if(!va||!(write?void_vm_writable(va,bytes):void_vm_readable(va,bytes)))
        void_lift_trap(g_cur_func,"DXVK invalid guest memory span");
    return GPTR(va);
}
uint32_t ut3_dxvk_guest_alloc(uint32_t bytes){return void_galloc(bytes);}
void ut3_dxvk_guest_free(uint32_t va){void_gfree(va);}
void ut3_dxvk_host_log(const char *text){void_log("[dxvk] %s",text);}
void *ut3_dxvk_unwrap_guest(uint32_t guest)
{
    if(!guest)return NULL;
    void_com_object *base=void_com_host(guest);
    for(unsigned kind=0;kind<sizeof(dxvk_classes)/sizeof(*dxvk_classes);++kind)
        if(base->cls==&dxvk_classes[kind]){
            dxvk_object *object=(dxvk_object*)base;
            if(object->kind!=kind)void_lift_trap(g_cur_func,"DXVK object kind mismatch");
            return object->native;
        }
    void_lift_trap(g_cur_func,"DXVK object has another COM owner");
    return NULL;
}
static void dxvk_destroy(void_com_object *base)
{
    dxvk_object *object=(dxvk_object*)base,**at=&dxvk_objects[dxvk_object_bucket(object->kind,object->native)];
    while(*at&&*at!=object)at=&(*at)->next;
    if(*at)*at=object->next;
    free(object);
}
/* Each incoming native pointer already owns one reference. */
uint32_t ut3_dxvk_wrap_native(unsigned kind,void *native)
{
    if(!native)return 0;
    if(kind>=sizeof(dxvk_classes)/sizeof(*dxvk_classes))void_lift_trap(g_cur_func,"DXVK unknown COM interface");
    unsigned bucket=dxvk_object_bucket(kind,native);
    for(dxvk_object *object=dxvk_objects[bucket];object;object=object->next)
        if(object->native==native&&object->kind==kind){void_com_addref(&object->com);return object->com.guest;}
    dxvk_object *object=calloc(1,sizeof(*object));
    if(!object)void_fatal("DXVK object allocation failed");
    object->native=native;object->kind=kind;object->com.destroy=dxvk_destroy;
    uint32_t guest=void_com_wrap(&object->com,&dxvk_classes[kind],0);
    object->next=dxvk_objects[bucket];dxvk_objects[bucket]=object;
    return guest;
}
static uint32_t dxvk_guest_call(uint32_t guest,unsigned kind,unsigned slot,const uint32_t *args)
{
    dxvk_object *object=void_com_host(guest);
    if(object->kind!=kind||object->com.cls!=&dxvk_classes[kind])void_lift_trap(g_cur_func,"DXVK vtable/interface mismatch");
    /* Verified guest_methods.inc: Device9 kind 2/Present 17; SwapChain9 kind 17/Present 3. */
    if((kind==2u&&slot==17u)||(kind==17u&&slot==3u)) {
        extern void ut3_adpf_work_end(void);
        ut3_adpf_work_end();
    }
    if(slot==1){ut3_dxvk_native_addref(object->native);return void_com_addref(&object->com);}
    if(slot==2){ut3_dxvk_native_release(object->native);return void_com_release(&object->com);}
    uint32_t result=ut3_dxvk_native_call(kind,object->native,slot,args);
    /* Verified guest_methods.inc: Query9 kind 16/GetData slot 7. No native
     * query or guest output pointer remains live across the machine unlock. */
    if(kind==16u&&slot==7u)dxvk_query_wait(result,args[2]);
    /* Device9 BeginScene 41 / EndScene 42 (bridge_slots.json), for loading.c. */
    if(kind==2u&&(slot==41u||slot==42u)&&!result){extern int void_d3d_scene_open;void_d3d_scene_open=slot==41u;}
    return result;
}
uint32_t ut3_dxvk_create9(uint32_t sdk){return ut3_dxvk_native_create9(sdk);}
