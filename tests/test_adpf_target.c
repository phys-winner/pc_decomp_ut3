/* Real own-PID API sessions: verify the actual duration passed to Android. */
#include "../android/host/adpf.c"
#include <assert.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>

void void_log(const char *format, ...)
{
    va_list args;va_start(args,format);vfprintf(stderr,format,args);fputc('\n',stderr);va_end(args);
}
static APerformanceHintSession *(*real_create)(APerformanceHintManager *,const int32_t *,size_t,int64_t);
static int (*real_report)(APerformanceHintSession *,int64_t);
static int64_t expected_target;
static unsigned creates,reports;
static APerformanceHintSession *capture_create(APerformanceHintManager *manager,const int32_t *tids,size_t count,int64_t target)
{
    assert(count==1&&tids[0]==syscall(SYS_gettid)&&target==expected_target);
    APerformanceHintSession *session=real_create(manager,tids,count,target);
    printf("native create target_ns=%lld tid=%d session=%p\n",(long long)target,tids[0],(void *)session);
    ++creates;return session;
}
static int capture_report(APerformanceHintSession *session,int64_t actual)
{
    assert(actual>0);int result=real_report(session,actual);assert(!result);++reports;return result;
}
static void work(void)
{
    volatile uint32_t sum=0;for(uint32_t i=0;i<5000;++i)sum+=i;assert(sum==12497500);
}
static int session(int64_t target)
{
    expected_target=target;errno=1234;
    ut3_adpf_cancel(0);ut3_adpf_work_begin();
    if(!adpf_tls.session) {
        fprintf(stderr,"UNAVAILABLE native session target_ns=%lld pid=%d uid=%d\n",(long long)target,getpid(),getuid());return 0;
    }
    assert(!adpf_tls.failed&&errno==1234);
    unsigned before=reports;ut3_adpf_work_begin();work();ut3_adpf_work_end();
    assert(reports==before+1&&errno==1234);
    ut3_adpf_work_begin();work();ut3_adpf_cancel(1);ut3_adpf_work_end();assert(reports==before+1);
    ut3_adpf_thread_close();assert(!adpf_tls.session&&errno==1234);
    return 1;
}
static void *owner_session(void *target)
{
    return (void *)(uintptr_t)session(*(int64_t *)target);
}
int main(void)
{
    setvbuf(stdout,NULL,_IONBF,0);alarm(10);assert(adpf_target_ns==16666666);
    const int invalid[]={0,-1,1001,INT_MIN,INT_MAX};
    for(unsigned i=0;i<sizeof invalid/sizeof *invalid;++i){ut3_adpf_set_target(invalid[i]);assert(adpf_target_ns==16666666);}
    pthread_once(&adpf_once,adpf_load);
    if(!adpf_api.ready){fprintf(stderr,"UNAVAILABLE real ADPF manager pid=%d uid=%d\n",getpid(),getuid());return 2;}
    real_create=adpf_api.create;real_report=adpf_api.report;
    adpf_api.create=capture_create;adpf_api.report=capture_report;
    int64_t targets[]={16666666,22222222,8333333};const int fps[]={60,45,120};unsigned accepted=0;
    for(unsigned i=0;i<3;++i) {
        if(i)ut3_adpf_set_target(fps[i]);
        assert(adpf_target_ns==targets[i]);
        pthread_t thread;assert(!pthread_create(&thread,NULL,owner_session,&targets[i]));
        void *result;assert(!pthread_join(thread,&result));accepted+=(unsigned)(uintptr_t)result;
    }
    ut3_adpf_set_target(0);assert(adpf_target_ns==8333333);
    assert(creates==3&&reports==accepted);
    if(accepted!=3) {
        fprintf(stderr,"UNAVAILABLE framework accepted %u/3 sessions; all 3 real native target arguments verified\n",accepted);return 2;
    }
    puts("PASS real Android ADPF targets: default60=16666666ns,45=22222222ns,120=8333333ns; invalid unchanged, 3 native owner sessions/reports, epoch cancellation, errno and close");
    return 0;
}
