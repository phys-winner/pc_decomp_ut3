/* Production IME absence with real virtual-window lifetime and guest ABI. */
#include "../android/host/user32.c"
#include "../android/host/imm32.c"
#include <assert.h>
#include <unistd.h>

void void_process_exit(uint32_t code, int detach) { (void)detach; _exit((int)code); }
static void wndproc(void) { g_eax=1; g_esp+=20; }
static uint32_t api(const char *dll, const char *name, unsigned count, const uint32_t *args)
{
    const void_shim *shim=void_shim_find(dll,name,0); assert(shim);
    uint32_t before=g_esp, result=void_call_guest(void_thunk(shim->fn,name),count,args,1);
    assert(before==g_esp); return result;
}
static void absent(uint32_t window, uint32_t error)
{
    void_set_last_error(0x1234);
    assert(!api("imm32.dll","ImmGetContext",1,&window) && void_get_last_error()==error);
}
int main(void)
{
    assert(void_guest_reserve()); void_threads_init(); void_dispatch_init(); void_machine_enter();
    uint32_t name=void_gstrdup("UT3ImeTest"), wc=void_galloc(40);
    MEM32(wc+4)=void_thunk(wndproc,"ime-test-wndproc"); MEM32(wc+36)=name;
    assert(api("user32.dll","RegisterClassA",1,&wc));
    uint32_t create[12]={0,name,name,0,20,30,100,80,0,0,0,0};
    uint32_t window=api("user32.dll","CreateWindowExA",12,create); assert(window);
    uint32_t guard=void_galloc(32); memset(GPTR(guard),0xA5,32);
    absent(window,0x1234); absent(0,0x1234); absent(0x12345678,1400);
    for (unsigned i=0;i<8;++i) {
        absent(window,0x1234);
        assert(!api("imm32.dll","ImmCreateContext",0,NULL) && void_get_last_error()==50);
        for (unsigned b=0;b<32;++b) assert(MEM8(guard+b)==0xA5);
    }
    assert(api("user32.dll","DestroyWindow",1,&window)); absent(window,1400);
    assert(!void_shim_find("imm32.dll","ImmAssociateContext",0));
    puts("PASS IME unavailable: real-window/NULL no-context preserves LastError, stale HWND1400, CreateContextNULL/NOT_SUPPORTED50, no fake HIMC or guarded companion, guards and stdcall");
    return 0;
}
