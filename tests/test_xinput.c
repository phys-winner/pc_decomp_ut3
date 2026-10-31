/* Production disconnected XInput1_3 profile, names/ordinals and x86 ABI. */
#include "../android/host/xinput.c"
#include <assert.h>
#include <unistd.h>

void void_process_exit(uint32_t code, int detach) { (void)detach; _exit((int)code); }
static uint32_t get(unsigned ordinal,uint32_t index,uint32_t state)
{
    const void_shim *shim=void_shim_find("xinput1_3.dll",ordinal ? NULL : "XInputGetState",ordinal ? 2 : 0);assert(shim);
    uint32_t before=g_esp,args[2]={index,state};void_set_last_error(0x1234);
    uint32_t result=void_call_guest(void_thunk(shim->fn,shim->name),2,args,1);assert(before==g_esp);return result;
}
static uint32_t set(unsigned ordinal,uint32_t index,uint32_t vibration)
{
    const void_shim *shim=void_shim_find("xinput1_3.dll",ordinal ? NULL : "XInputSetState",ordinal ? 3 : 0);assert(shim);
    uint32_t before=g_esp,args[2]={index,vibration};void_set_last_error(0x1234);
    uint32_t result=void_call_guest(void_thunk(shim->fn,shim->name),2,args,1);assert(before==g_esp);return result;
}
int main(void)
{
    assert(void_guest_reserve());void_threads_init();void_dispatch_init();void_machine_enter();
    assert(void_shim_find("xinput1_3.dll","XInputGetState",0)->fn==void_shim_find("xinput1_3.dll",NULL,2)->fn);
    assert(void_shim_find("xinput1_3.dll","XInputSetState",0)->fn==void_shim_find("xinput1_3.dll",NULL,3)->fn);
    uint32_t allocation=void_galloc(24),state=allocation+1;
    for(unsigned ordinal=0;ordinal<2;++ordinal) {
        for(uint32_t index=0;index<4;++index) {
            memset(GPTR(allocation),0xA5,24);
            assert(get(ordinal,index,state)==1167 && !void_get_last_error());
            for(unsigned i=0;i<24;++i) assert(MEM8(allocation+i)==0xA5);
            assert(get(ordinal,index,0)==160 && void_get_last_error()==0x1234);
            assert(set(ordinal,index,state)==1167 && !void_get_last_error());
            for(unsigned b=0;b<24;++b) assert(MEM8(allocation+b)==0xA5);
            assert(set(ordinal,index,0)==160 && void_get_last_error()==0x1234);
        }
        const uint32_t invalid[]={4,5,0xFFFFFFFF};
        for(unsigned i=0;i<3;++i) {
            assert(get(ordinal,invalid[i],state)==160 && void_get_last_error()==0x1234);
            assert(set(ordinal,invalid[i],state)==160 && void_get_last_error()==0x1234);
            for(unsigned b=0;b<24;++b) assert(MEM8(allocation+b)==0xA5);
        }
        uint32_t page=void_vm_alloc(0,4096,0x3000,4),old;assert(page);
        assert(void_vm_protect(page,4096,1,&old));
        assert(get(ordinal,0,page)==1167 && !void_get_last_error()); /* Device absence needs no output access. */
        assert(get(ordinal,3,UINT32_MAX)==1167 && !void_get_last_error());
        assert(void_vm_protect(page,4096,4,&old));
    }
    puts("PASS XInput1_3 Get/SetState names/ordinals2/3: four disconnected slots1167, state/vibration/guards untouched, NULL/invalidindex160, native LastError/precedence, no input/output or haptics, stdcall");
    return 0;
}
