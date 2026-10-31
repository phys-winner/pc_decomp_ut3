/* Isolated current-thread syscall cost, never linked into the game. */
#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

static cpu_set_t original_mask,big_cores;
static int original_valid,restored,big_cores_count=1,failures,fallback_setters;
static pthread_once_t big_cores_once=PTHREAD_ONCE_INIT;
static void big_cores_init(void) {}

static void restore(void)
{
    if(original_valid&&!restored) {
        if(sched_setaffinity(0,sizeof original_mask,&original_mask)) {
            static const char message[]="ERROR own affinity restore failed\n";
            (void)write(STDERR_FILENO,message,sizeof message-1);
            _exit(2);
        }
        restored=1;
    }
}
static void deadline(int signal_number)
{
    (void)signal_number;
    if(original_valid)(void)syscall(SYS_sched_setaffinity,0,sizeof original_mask,&original_mask);
    static const char message[]="ERROR 15-second deadline; own mask restore attempted\n";
    (void)write(STDERR_FILENO,message,sizeof message-1);_exit(124);
}
static void require(int condition,const char *message)
{ if(!condition){fprintf(stderr,"ERROR %s errno=%d\n",message,errno);exit(1);} }
static uint64_t ns(clockid_t clock)
{ struct timespec value;require(clock_gettime(clock,&value)==0,"clock_gettime");return (uint64_t)value.tv_sec*1000000000u+value.tv_nsec; }
static int64_t us(struct timeval value)
{ return (int64_t)value.tv_sec*1000000+value.tv_usec; }

/* The generated header contains the exact production pin_guest_thread body,
 * renamed only. Its pthread_once uses the initialized no-op fixture above. */
#include "production_pin_excerpt.h"

static __attribute__((noinline)) void checked_pin(void)
{
    pthread_once(&big_cores_once,big_cores_init);
    if(big_cores_count) {
        cpu_set_t current;
        int saved_errno=errno;
        int equal=sched_getaffinity(0,sizeof current,&current)==0 && CPU_EQUAL(&current,&big_cores);
        errno=saved_errno;
        if(!equal) { ++fallback_setters;if(sched_setaffinity(0,sizeof big_cores,&big_cores))++failures; }
    }
}
static __attribute__((noinline)) void get_only(void)
{ cpu_set_t current;if(sched_getaffinity(0,sizeof current,&current))++failures; }
static __attribute__((noinline)) void empty(void)
{ __asm__ volatile("" ::: "memory"); }

static void sample(const char *name,void (*operation)(void),unsigned count,unsigned repeat)
{
    struct rusage before,after;
    require(getrusage(RUSAGE_THREAD,&before)==0,"getrusage before");
    int first_cpu=sched_getcpu();uint64_t cpu_start=ns(CLOCK_THREAD_CPUTIME_ID),wall_start=ns(CLOCK_MONOTONIC);
    errno=EDOM;
    for(unsigned i=0;i<count;++i)operation();
    int operation_errno=errno;
    uint64_t wall_end=ns(CLOCK_MONOTONIC),cpu_end=ns(CLOCK_THREAD_CPUTIME_ID);int last_cpu=sched_getcpu();
    require(getrusage(RUSAGE_THREAD,&after)==0,"getrusage after");
    cpu_set_t current;require(sched_getaffinity(0,sizeof current,&current)==0 && CPU_EQUAL(&current,&big_cores),"sample changed own mask");
    require(!failures && !fallback_setters,"syscall failed or unexpected candidate setter");
    require(operation_errno==EDOM,"syscall failure changed errno");
    printf("{\"sample\":%u,\"variant\":\"%s\",\"iterations\":%u,\"wall_ns\":%"PRIu64",\"thread_cpu_ns\":%"PRIu64",\"user_us\":%"PRId64",\"kernel_us\":%"PRId64",\"voluntary_switches\":%ld,\"involuntary_switches\":%ld,\"cpu_before\":%d,\"cpu_after\":%d}\n",
        repeat,name,count,wall_end-wall_start,cpu_end-cpu_start,us(after.ru_utime)-us(before.ru_utime),us(after.ru_stime)-us(before.ru_stime),
        after.ru_nvcsw-before.ru_nvcsw,after.ru_nivcsw-before.ru_nivcsw,first_cpu,last_cpu);
}

int main(int argc,char **argv)
{
    require(argc==3,"usage: helper ITERATIONS REPEATS");
    char *end;unsigned long count=strtoul(argv[1],&end,10);require(*argv[1] && !*end && count>=1000 && count<=100000,"iterations bounds");
    unsigned long repeats=strtoul(argv[2],&end,10);require(*argv[2] && !*end && repeats>=3 && repeats<=7,"repeat bounds");
    struct sigaction action={0};action.sa_handler=deadline;sigemptyset(&action.sa_mask);
    require(sigaction(SIGALRM,&action,NULL)==0,"sigaction");alarm(15);
    require(atexit(restore)==0,"atexit");
    require(sched_getaffinity(0,sizeof original_mask,&original_mask)==0,"capture own mask");original_valid=1;
    int cpu=CPU_SETSIZE-1;while(cpu>=0&&!CPU_ISSET(cpu,&original_mask))--cpu;require(cpu>=0,"empty own mask");
    CPU_ZERO(&big_cores);CPU_SET(cpu,&big_cores);require(sched_setaffinity(0,sizeof big_cores,&big_cores)==0,"pin own thread");
    cpu_set_t current;require(sched_getaffinity(0,sizeof current,&current)==0 && CPU_EQUAL(&current,&big_cores),"verify own pin");
    struct utsname system;require(uname(&system)==0,"uname");
    printf("{\"helper\":\"ut3-affinity-syscall-micro\",\"uid\":%u,\"pid\":%u,\"cpu\":%d,\"mask_bytes\":%zu,\"kernel_release\":\"%s\",\"iterations\":%lu,\"repeats\":%lu,\"alarm_seconds\":15,\"original_cpus\":[",(unsigned)getuid(),(unsigned)getpid(),cpu,sizeof(cpu_set_t),system.release,count,repeats);
    for(int i=0,n=0;i<CPU_SETSIZE;++i)if(CPU_ISSET(i,&original_mask))printf("%s%d",n++?",":"",i);
    puts("]}");
    for(unsigned i=0;i<2000;++i){baseline_pin_guest_thread();checked_pin();get_only();}
    require(!failures&&!fallback_setters,"warmup failure");
    void (*operations[])(void)={baseline_pin_guest_thread,checked_pin,get_only,empty};
    const char *names[]={"set_same_production","get_equal_candidate","get_only","empty_loop"};
    const unsigned orders[][4]={{0,1,2,3},{2,1,0,3},{1,0,2,3},{2,0,1,3},{0,2,1,3},{1,2,0,3},{2,1,0,3}};
    for(unsigned r=0;r<repeats;++r)for(unsigned at=0;at<4;++at) {
        unsigned variant=orders[r][at];sample(names[variant],operations[variant],(unsigned)count,r);
    }
    restore();require(sched_getaffinity(0,sizeof current,&current)==0 && CPU_EQUAL(&current,&original_mask),"verify original mask restoration");alarm(0);
    printf("{\"result\":\"PASS\",\"own_original_mask_restored\":true,\"candidate_fallback_setters\":%d,\"syscall_failures\":%d}\n",fallback_setters,failures);
    return 0;
}
