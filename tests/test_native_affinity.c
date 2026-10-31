/* Production creation-scope affinity: real pthread inheritance and failures. */
#define _GNU_SOURCE
#include <sched.h>
#include <errno.h>
#include <fcntl.h>
static int fail_get,fail_set;
static int affinity_get(pid_t,size_t,cpu_set_t *);
static int affinity_set(pid_t,size_t,const cpu_set_t *);
#define sched_getaffinity affinity_get
#define sched_setaffinity affinity_set
#include "../android/host/threads.c"
#undef sched_getaffinity
#undef sched_setaffinity
#include <assert.h>

void void_process_exit(uint32_t code,int detach){(void)detach;_exit((int)code);}
static int affinity_get(pid_t tid,size_t bytes,cpu_set_t *mask)
{if(fail_get){--fail_get;errno=EACCES;return -1;}return sched_getaffinity(tid,bytes,mask);}
static int affinity_set(pid_t tid,size_t bytes,const cpu_set_t *mask)
{if(fail_set){--fail_set;errno=EPERM;return -1;}return sched_setaffinity(tid,bytes,mask);}
static cpu_set_t child_mask,child_initial_mask;
static int reset_child;
static void *native_worker(void *unused)
{
    (void)unused;assert(!sched_getaffinity(0,sizeof child_initial_mask,&child_initial_mask));
    if(reset_child) {
        errno=EDOM;
        if(reset_child==1)assert(void_native_worker_affinity_apply());
        else {fail_set=1;assert(!void_native_worker_affinity_apply());}
        assert(errno==EDOM);
    }
    assert(!sched_getaffinity(0,sizeof child_mask,&child_mask));return NULL;
}
static cpu_set_t current(void)
{cpu_set_t mask;assert(!sched_getaffinity(0,sizeof mask,&mask));return mask;}
static void same(cpu_set_t expected)
{cpu_set_t actual=current();assert(CPU_EQUAL(&actual,&expected));}
static void spawn(cpu_set_t expected)
{pthread_t thread;assert(!pthread_create(&thread,NULL,native_worker,NULL));assert(!pthread_join(thread,NULL));assert(CPU_EQUAL(&child_mask,&expected));}
static void print_mask(const char *label,cpu_set_t mask)
{printf("%s=",label);for(int i=0;i<CPU_SETSIZE;++i)if(CPU_ISSET(i,&mask))printf("%d,",i);puts("");}
int main(void)
{
    alarm(10);cpu_set_t original=current();
    assert(void_guest_reserve());void_threads_init();void_dispatch_init();void_machine_enter();
    cpu_set_t guest=current();assert(native_worker_original_valid);assert(CPU_EQUAL(&original,&native_worker_original_mask));
    const char *profile=getenv("VOID_BIG_CORES");
    int android_scheduling=profile && *profile=='0';
    if(android_scheduling)assert(!big_cores_count && CPU_EQUAL(&guest,&original));
    void_cpuid(1,0);assert(((g_ebx>>16)&255)==void_guest_cpu_count());
    void_cpuid(0x80000008u,0);assert(g_ecx+1==void_guest_cpu_count());
    print_mask("original",original);print_mask("guest",guest);spawn(guest);
    reset_child=1;spawn(original);assert(CPU_EQUAL(&child_initial_mask,&guest));same(guest);
    reset_child=2;spawn(guest);same(guest);reset_child=0;
    g_eax=0x12345678;uint32_t virtual_mask=void_guest_cpu_system_mask();
    errno=EDOM;void_native_worker_affinity_begin();assert(errno==EDOM);same(original);spawn(original);
    errno=E2BIG;void_native_worker_affinity_begin();assert(errno==E2BIG);same(original);spawn(original);
    errno=ENOENT;void_native_worker_affinity_end();assert(errno==ENOENT);same(original);
    void_native_worker_affinity_end();same(guest);assert(g_eax==0x12345678 && void_guest_cpu_system_mask()==virtual_mask);
    /* Failure of the enclosed native operation cannot bypass restoration. */
    void_native_worker_affinity_begin();assert(open("/ut3-native-affinity-no-such-file",O_RDONLY)==-1);
    int native_errno=errno;void_native_worker_affinity_end();assert(errno==native_errno);same(guest);
    fail_get=1;errno=ERANGE;void_native_worker_affinity_begin();assert(errno==ERANGE);same(guest);
    void_native_worker_affinity_end();same(guest);
    fail_set=1;errno=EINTR;void_native_worker_affinity_begin();assert(errno==EINTR);same(guest);
    void_native_worker_affinity_end();same(guest);
    /* Deep nesting skips excess scopes without corrupting the outer save. */
    for(unsigned i=0;i<NATIVE_AFFINITY_SCOPES+3;++i)void_native_worker_affinity_begin();same(original);
    for(unsigned i=0;i<NATIVE_AFFINITY_SCOPES+3;++i)void_native_worker_affinity_end();same(guest);
    void_native_worker_affinity_end();same(guest);assert(!native_affinity_depth);
    puts(android_scheduling ? "PASS Android scheduling profile: no guest prime pin, original allowed mask captured; native scopes/lazy inheritance, failure/errno/nesting and virtual CPUID/affinity preserved" : "PASS native-worker affinity: original mask captured before prime pin; creation scopes and lazy child-entry reset/apply restore native allowed mask while parent stays prime; failure/errno/nested/deep/guest-register/virtual-CPU preservation");
    return 0;
}
