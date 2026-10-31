/* Production playback A/W callbacks and honest unavailable endpoint IDs. */
#include "../android/host/dsound.c"
#include <assert.h>
#include <unistd.h>

void void_process_exit(uint32_t code, int detach) { (void)detach; _exit((int)code); }
static int callback_wide, callback_result, callbacks;
static uint32_t callback_context;
static void playback_callback(void)
{
    assert(ARG(0) == 0 && ARG(3) == callback_context);
    char *description = callback_wide ? void_wstr_utf8(ARG(1)) : void_astr_utf8(ARG(1));
    char *module = callback_wide ? void_wstr_utf8(ARG(2)) : void_astr_utf8(ARG(2));
    assert(description && !strcmp(description,"Android DirectSound"));
    assert(module && !strcmp(module,"dsound.dll"));
    free(description); free(module); ++callbacks;
    void_set_last_error(0x7777); g_eax=callback_result; g_esp+=20;
}
static uint32_t api(const char *name, uint32_t ordinal, const uint32_t args[2])
{
    const void_shim *shim = void_shim_find("dsound.dll",name,ordinal); assert(shim);
    uint32_t before=g_esp;
    uint32_t result=void_call_guest(void_thunk(shim->fn,shim->name),2,args,1);
    assert(before==g_esp); return result;
}
int main(void)
{
    assert(void_guest_reserve()); void_threads_init(); void_dispatch_init(); void_machine_enter();
    uint32_t cb=void_thunk(playback_callback,"playback-callback");
    callback_context=0xF00DCAFE;
    for (callback_wide=0;callback_wide<2;++callback_wide) {
        const char *name=callback_wide ? "DirectSoundEnumerateW" : "DirectSoundEnumerateA";
        uint32_t ordinal=callback_wide ? 3 : 2;
        assert(void_shim_find("dsound.dll",name,0)->fn==void_shim_find("dsound.dll",NULL,ordinal)->fn);
        for (callback_result=0;callback_result<2;++callback_result) {
            callbacks=0; uint32_t args[2]={cb,callback_context}; void_set_last_error(0x1234);
            assert(api(name,0,args)==DS_OK && callbacks==1 && !void_get_last_error());
            callbacks=0; assert(api(NULL,ordinal,args)==DS_OK && callbacks==1);
        }
        uint32_t args[2]={0,callback_context}; void_set_last_error(0x1234);
        assert(api(name,0,args)==DSERR_INVALIDPARAM && void_get_last_error()==0x1234);
    }
    uint32_t source=void_galloc(16), allocation=void_galloc(24), destination=allocation+1;
    const uint8_t suffix[]={0x6D,0x9C,0xED,0x47,0xAA,0xF1,0x4D,0xDA,0x8F,0x2B,0x5C,0x03};
    memcpy((uint8_t *)GPTR(source)+4,suffix,12);
    for (uint32_t id=0;id<6;++id) {
        MEM32(source)=id<4 ? 0xDEF00000u+id : id==4 ? 0x12345678 : 0;
        if (id==5) memset(GPTR(source),0,16);
        memset(GPTR(allocation),0xA5,24);
        uint32_t args[2]={source,destination}; void_set_last_error(0x1234);
        assert(api(id&1 ? NULL : "GetDeviceID",id&1 ? 9 : 0,args)==0x88780078u);
        for (unsigned i=0;i<24;++i) assert(MEM8(allocation+i)==0xA5);
    }
    uint32_t args[2]={0,destination};
    assert(api("GetDeviceID",0,args)==DSERR_INVALIDPARAM);
    args[0]=source;args[1]=0;assert(api(NULL,9,args)==DSERR_INVALIDPARAM);
    args[1]=0x1000;assert(api(NULL,9,args)==DSERR_INVALIDPARAM);
    uint32_t page=void_vm_alloc(0,4096,0x3000,4);assert(page);
    args[1]=page+4081;memset(GPTR(page+4081),0xA5,15);
    assert(api(NULL,9,args)==DSERR_INVALIDPARAM);
    for(unsigned i=0;i<15;++i) assert(MEM8(page+4081+i)==0xA5);
    assert(!mixer_running && !buffers);
    puts("PASS DirectSound playback A/W ordinals2/3 UTF16/ANSI real callback/context/stop/HRESULT ABI; GetDeviceID9 default/voice/unknown unavailable, outputguards/NULL/bounds; no fabricated endpoints");
    return 0;
}
