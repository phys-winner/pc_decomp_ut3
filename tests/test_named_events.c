/* Production process-local A/W event namespace and per-handle rights. */
#include "../android/host/threads.c"
#include <assert.h>

void void_process_exit(uint32_t code, int detach) { (void)detach; _exit((int)code); }
static uint32_t api(const char *name, unsigned count, const uint32_t *args)
{
    const void_shim *shim = void_shim_find("kernel32.dll", name, 0); assert(shim);
    uint32_t before = g_esp, result = void_call_guest(void_thunk(shim->fn, name), count, args, 1);
    assert(g_esp == before); return result;
}
static uint32_t text(const char *name, int wide)
{
    uint32_t units = wide ? void_utf8_to_gw(name, 0, 0) : void_utf8_to_ga(name, 0, 0);
    uint32_t out = void_galloc(units * (wide ? 2 : 1)); assert(out);
    if (wide) void_utf8_to_gw(name, out, units); else void_utf8_to_ga(name, out, units);
    return out;
}
static uint32_t wait(uint32_t event) { uint32_t args[] = {event,0}; return api("WaitForSingleObject",2,args); }

int main(void)
{
    alarm(10); assert(void_guest_reserve()); void_threads_init(); void_dispatch_init(); void_machine_enter();
    uint32_t a = text("UT3-Named-Event-Test",0), w = text("UT3-Named-Event-Test",1);
    uint32_t open[3] = {0x100000,0,a};
    assert(!api("OpenEventA",3,open) && void_get_last_error() == 2);
    uint32_t create[4] = {0,1,0,a};
    uint32_t first = api("CreateEventA",4,create); assert(first && !void_get_last_error());
    create[1] = 0; create[2] = 1; create[3] = w;
    uint32_t duplicate = api("CreateEventW",4,create); assert(duplicate && duplicate != first && void_get_last_error() == 183);
    assert(void_handle_get(first,VOID_OBJ_EVENT) == void_handle_get(duplicate,VOID_OBJ_EVENT));
    assert(wait(duplicate) == WAIT_TIMEOUT_); /* Existing initial/manual state wins. */
    open[2] = w; open[1] = 1;
    void_set_last_error(0x1234);
    uint32_t observer = api("OpenEventW",3,open); assert(observer && void_get_last_error() == 0x1234);
    assert(!api("SetEvent",1,&observer) && void_get_last_error() == 5);
    assert(api("SetEvent",1,&first) && wait(observer) == 0 && wait(observer) == 0);
    assert(api("ResetEvent",1,&duplicate) && wait(observer) == WAIT_TIMEOUT_);
    open[0] = 2;
    uint32_t modifier = api("OpenEventW",3,open); assert(modifier);
    assert(wait(modifier) == WAIT_FAILED_ && void_get_last_error() == 5);
    assert(api("SetEvent",1,&modifier) && wait(observer) == 0);
    uint32_t copied = void_handle_dup(observer);
    assert(copied && !api("ResetEvent",1,&copied) && void_get_last_error() == 5);
    assert(api("CloseHandle",1,&copied));
    uint32_t wrongcase = text("ut3-Named-Event-Test",0); open[0] = 0x100000; open[2] = wrongcase;
    assert(!api("OpenEventA",3,open) && void_get_last_error() == 2);
    open[2] = a; open[0] = 0x4000;
    assert(!api("OpenEventA",3,open) && void_get_last_error() == 5);
    assert(api("CloseHandle",1,&first) && api("CloseHandle",1,&duplicate));
    assert(wait(observer) == 0); /* Namespace/object survives original creator. */
    assert(api("CloseHandle",1,&modifier) && api("CloseHandle",1,&observer));
    open[0] = 0x100000;
    assert(!api("OpenEventA",3,open) && void_get_last_error() == 2 && !named_events);
    uint32_t local = text("Local\\UT3-Named-Event-Test",0); create[1]=1; create[2]=0; create[3]=local;
    first=api("CreateEventA",4,create); assert(first); open[2]=a;
    observer=api("OpenEventA",3,open); assert(observer);
    assert(api("CloseHandle",1,&first) && api("CloseHandle",1,&observer));
    uint32_t unicode_w = text("UT3-Игрок",1), unicode_a = text("UT3-Игрок",0);
    create[3]=unicode_w; first=api("CreateEventW",4,create); assert(first);
    open[2]=unicode_a; observer=api("OpenEventA",3,open); assert(observer);
    assert(api("CloseHandle",1,&first) && api("CloseHandle",1,&observer));
    create[3]=0; first=api("CreateEventA",4,create); duplicate=api("CreateEventA",4,create);
    assert(first && duplicate && void_handle_get(first,VOID_OBJ_EVENT) != void_handle_get(duplicate,VOID_OBJ_EVENT));
    assert(api("CloseHandle",1,&first) && api("CloseHandle",1,&duplicate));
    open[2]=0; assert(!api("OpenEventA",3,open) && void_get_last_error() == 87);
    uint32_t empty=text("",0); open[2]=empty;
    assert(!api("OpenEventA",3,open) && void_get_last_error() == 6);
    create[3]=empty; first=api("CreateEventA",4,create);
    assert(first && !void_get_last_error() && api("CloseHandle",1,&first));
    uint32_t page=void_vm_alloc(0,4096,0x3000,4); assert(page); MEM8(page+4095)='x'; open[2]=page+4095;
    assert(!api("OpenEventA",3,open) && void_get_last_error() == 998);
    create[3]=page+4095; assert(!api("CreateEventA",4,create) && void_get_last_error() == 998);
    assert(!named_events);
    puts("PASS named events: missing external name2, A/W same object183, original state/manual reset, restricted open/duplicate rights, case/Unicode/Local alias, last-close cleanup, guest bounds and stdcall");
    return 0;
}
