/* Production stored window owner, virtual PID, optional output and x86 ABI. */
#include "../android/host/user32.c"
#include <assert.h>
#include <unistd.h>

void void_process_exit(uint32_t code, int detach) { (void)detach; _exit((int)code); }
static void wndproc(void) { g_eax=1; g_esp+=20; }
static uint32_t api(const char *dll,const char *name,unsigned count,const uint32_t *args)
{
    const void_shim *shim=void_shim_find(dll,name,0); assert(shim);
    uint32_t before=g_esp,result=void_call_guest(void_thunk(shim->fn,name),count,args,1);
    assert(before==g_esp); return result;
}
static uint32_t created_window,creator_thread,process_id;
static void *worker(void *unused)
{
    (void)unused; void_machine_enter(); assert(void_thread_id()!=creator_thread);
    uint32_t output=void_galloc(4),args[2]={created_window,output};
    assert(api("user32.dll","GetWindowThreadProcessId",2,args)==creator_thread && MEM32(output)==process_id);
    void_gfree(output); void_machine_leave(); return NULL;
}
int main(void)
{
    alarm(10); assert(void_guest_reserve()); void_threads_init(); void_dispatch_init(); void_machine_enter();
    creator_thread=api("kernel32.dll","GetCurrentThreadId",0,NULL);
    process_id=api("kernel32.dll","GetCurrentProcessId",0,NULL);
    uint32_t name=void_gstrdup("UT3WindowOwnerTest"),wc=void_galloc(40);
    MEM32(wc+4)=void_thunk(wndproc,"owner-test-wndproc"); MEM32(wc+36)=name;
    assert(api("user32.dll","RegisterClassA",1,&wc));
    uint32_t create[12]={0,name,name,0,20,30,100,80,0,0,0,0};
    created_window=api("user32.dll","CreateWindowExA",12,create); assert(created_window);
    uint32_t allocation=void_galloc(12),output=allocation+1,args[2]={created_window,output};
    memset(GPTR(allocation),0xA5,12); void_set_last_error(0x1234);
    assert(api("user32.dll","GetWindowThreadProcessId",2,args)==creator_thread);
    assert(MEM32(output)==process_id && void_get_last_error()==0x1234);
    assert(MEM8(allocation)==0xA5); for(unsigned i=5;i<12;++i) assert(MEM8(allocation+i)==0xA5);
    args[1]=0;void_set_last_error(0x1234);
    assert(api("user32.dll","GetWindowThreadProcessId",2,args)==creator_thread && void_get_last_error()==0x1234);
    pthread_t thread;assert(!pthread_create(&thread,NULL,worker,NULL));
    void_blocking_begin();assert(!pthread_join(thread,NULL));void_blocking_end();
    args[1]=0x1000;assert(!api("user32.dll","GetWindowThreadProcessId",2,args) && void_get_last_error()==998);
    uint32_t page=void_vm_alloc(0,4096,0x3000,4);assert(page);memset(GPTR(page+4093),0xA5,3);
    args[1]=page+4093;assert(!api("user32.dll","GetWindowThreadProcessId",2,args) && void_get_last_error()==998);
    for(unsigned i=0;i<3;++i) assert(MEM8(page+4093+i)==0xA5);
    uint32_t invalid[]={0,0x12345678,UINT32_MAX};
    for(unsigned i=0;i<3;++i) {
        args[0]=invalid[i];args[1]=output;MEM32(output)=0xA5A5A5A5;
        assert(!api("user32.dll","GetWindowThreadProcessId",2,args) && void_get_last_error()==1400 && MEM32(output)==0xA5A5A5A5);
    }
    assert(api("user32.dll","DestroyWindow",1,&created_window));args[0]=created_window;
    assert(!api("user32.dll","GetWindowThreadProcessId",2,args) && void_get_last_error()==1400);
    puts("PASS window owner: stored creator TID across threads, virtual PID matches Kernel32, optional/unaligned bounded output, invalid/stale HWND1400, guards, LastError/stdcall");
    return 0;
}
