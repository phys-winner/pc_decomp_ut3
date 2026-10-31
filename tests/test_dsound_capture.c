/* Actual capture exports: empty backend inventory, no synthetic microphone. */
#include "../android/host/dsound.c"
#include <assert.h>
#include <unistd.h>

void void_process_exit(uint32_t code, int detach) { (void)detach; _exit((int)code); }
static unsigned callbacks;
static void callback(void)
{
    ++callbacks; g_eax = 1; g_esp += 20;
}
static uint32_t enumerate(const char *name, uint32_t ordinal, uint32_t function, uint32_t context)
{
    const void_shim *shim = void_shim_find("dsound.dll",name,ordinal); assert(shim);
    uint32_t args[2] = {function,context}, before = g_esp;
    void_set_last_error(0x1234);
    uint32_t result = void_call_guest(void_thunk(shim->fn,shim->name),2,args,1);
    assert(g_esp == before && void_get_last_error() == (function ? 0 : 0x1234)); return result;
}

int main(void)
{
    assert(void_guest_reserve()); void_threads_init(); void_dispatch_init(); void_machine_enter();
    uint32_t cb = void_thunk(callback,"capture-test-callback");
    uint32_t context = void_galloc(32); memset(GPTR(context),0xA5,32);
    uint32_t page = void_vm_alloc(0,4096,0x3000,4), old; assert(page);
    assert(void_vm_protect(page,4096,1,&old));
    for (unsigned wide = 0; wide < 2; ++wide) {
        const char *name = wide ? "DirectSoundCaptureEnumerateW" : "DirectSoundCaptureEnumerateA";
        uint32_t ordinal = wide ? 8 : 7;
        assert(void_shim_find("dsound.dll",name,0)->fn == void_shim_find("dsound.dll",NULL,ordinal)->fn);
        assert(enumerate(name,0,cb,context) == DS_OK);
        assert(enumerate(NULL,ordinal,cb,page) == DS_OK); /* Opaque context must not be read. */
        assert(enumerate(NULL,ordinal,cb,UINT32_MAX) == DS_OK);
        assert(enumerate(name,0,0,context) == DSERR_INVALIDPARAM);
        assert(!callbacks && !mixer_running && !buffers);
        for (unsigned i = 0; i < 32; ++i) assert(MEM8(context+i) == 0xA5);
    }
    assert(void_shim_find("dsound.dll",NULL,2)->fn != void_shim_find("dsound.dll",NULL,7)->fn);
    assert(void_vm_protect(page,4096,4,&old));
    puts("PASS DirectSoundCaptureEnumerate A/W ordinals7/8: honest empty inventory/no callback, NULL invalidparam, opaque guarded context unchanged, playback untouched, HRESULT/LastError/stdcall");
    return 0;
}
