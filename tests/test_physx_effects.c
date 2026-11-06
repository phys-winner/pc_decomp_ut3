/* The pinned original SDK, including a real simulated hardware-requested scene. */
#include "host.h"
#include <assert.h>
#include <unistd.h>
#include "../android/host/features.c"

int ut3_game_main(const char *, const char *, int);
static uint32_t method(uint32_t object, uint32_t slot, unsigned count, uint32_t *args)
{
    g_ecx=object;
    return void_call_guest(MEM32(MEM32(object)+slot),count,args,1);
}
static void putf(uint32_t at,float value){memcpy(GPTR(at),&value,4);}
static void cloth_boundary(void)
{
    uint32_t desc=ARG(0);
    assert(MEM32(desc+0x100)==0x800 && MEM32(desc+0x10C)==0);
    g_eax=0x12345678;g_esp+=8;
}

int main(int argc,char **argv)
{
    assert(argc==3);
    assert(!ut3_game_main(argv[1],argv[2],1));
    void_machine_enter();
    uint32_t loader=void_module_load("PhysXLoader.dll"), name=void_gstrdup("NxCreatePhysicsSDK");
    assert(loader);
    uint32_t create=void_module_proc(loader,name);void_gfree(name);assert(create);
    uint32_t desc=void_galloc(32),error=void_galloc(4);
    const uint32_t sdk_desc[]={0x10000,0x100,0x800,0,0};
    memcpy(GPTR(desc),sdk_desc,sizeof sdk_desc);MEM32(error)=0xffffffff;
    uint32_t args[]={0x02070300,0,0,desc,error};
    uint32_t sdk=void_call_guest(create,5,args,0);
    assert(sdk&&MEM32(error)==0);
    assert(method(sdk,0x7c,0,NULL)==1);
    uint32_t engine=void_galloc(0x200),old_engine=MEM32(0x1DEB464),old_sdk=MEM32(0x1DF31C8);
    memset(GPTR(engine),0,0x200);MEM32(0x1DEB464)=engine;MEM32(0x1DF31C8)=sdk;
    assert(void_call_guest(0x010FC820,0,NULL,0)==1);
    MEM8(engine+0x192)=1;assert(void_call_guest(0x010FC820,0,NULL,0)==0);
    MEM32(0x1DEB464)=old_engine;MEM32(0x1DF31C8)=old_sdk;void_gfree(engine);
    uint32_t scene_desc=void_galloc(0x100);
    memset(GPTR(scene_desc),0,0x100);
    putf(scene_desc+0x24,1.f/60.f);MEM32(scene_desc+0x28)=8;
    MEM32(scene_desc+0x38)=1;MEM32(scene_desc+0x78)=3;
    args[0]=scene_desc;
    uint32_t scene=method(sdk,0x10,1,args);
    assert(scene&&MEM32(scene_desc+0x38)==1);
    /* NxScene::getSimType returns the real solver selected by the SDK. */
    assert(method(scene,8,0,NULL)==0);
    uint32_t cloth_desc=void_galloc(0x110);memset(GPTR(cloth_desc),0,0x110);
    MEM32(cloth_desc+0x100)=0x2800;MEM32(cloth_desc+0x10C)=0x12345000;
    void_fn saved_cloth=physx_cloth;physx_cloth=cloth_boundary;args[0]=cloth_desc;g_ecx=scene;
    assert(void_call_guest(0x1E13C930,1,args,1)==0x12345678);
    assert(MEM32(cloth_desc+0x100)==0x2800 && MEM32(cloth_desc+0x10C)==0x12345000);
    physx_cloth=saved_cloth;void_gfree(cloth_desc);
    printf("PASS real PhysX 2.7.3: hardware-requested scene %08X created with CPU solver; descriptor restored\n",scene);
    fflush(stdout);
    void_machine_leave();
    _exit(0);
}
