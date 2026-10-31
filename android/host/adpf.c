/* Candidate only: observation of the verified UE3 producer-work interval.
 * No guest state, native CPU policy, thread affinity or pacing is changed. */
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#ifdef UT3_ADPF_TEST
#include "test_platform.h"
#else
#include <stdatomic.h>
#include <dlfcn.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

extern void void_log(const char *, ...);
extern void void_ui_hud_frame_begin(void) __attribute__((weak));
extern void void_ui_camera_frame_begin(void) __attribute__((weak));
typedef struct APerformanceHintManager APerformanceHintManager;
typedef struct APerformanceHintSession APerformanceHintSession;
/* Exact API33 C signatures from NDK27 performance_hint.h; dynamic lookup keeps
 * the existing API30 minimum. No API35/GPU/auto-timing dependency is required. */
static struct {
    APerformanceHintManager *(*manager)(void);
    APerformanceHintSession *(*create)(APerformanceHintManager *, const int32_t *, size_t, int64_t);
    int (*report)(APerformanceHintSession *, int64_t);
    void (*close)(APerformanceHintSession *);
    APerformanceHintManager *instance;
    int ready;
} adpf_api;
static pthread_once_t adpf_once = PTHREAD_ONCE_INIT;
static pthread_key_t adpf_key;
/* Low bit blocks work; upper bits cancel intervals across every lifecycle event.
 * Start blocked until Android publishes the first ready lifecycle snapshot. */
static _Atomic uint64_t adpf_epoch = 1;
static int64_t adpf_target_ns = INT64_C(16666666);

/* Set before starting the game thread and creating its hint session. */
void ut3_adpf_set_target(int fps)
{
    if (fps >= 1 && fps <= 1000) adpf_target_ns = INT64_C(1000000000) / fps;
}
typedef struct {
    APerformanceHintSession *session;
    int32_t tid;
    int failed, armed;
    uint64_t epoch;
    int64_t start;
} adpf_thread;
static _Thread_local adpf_thread adpf_tls;

static void adpf_destroy(void *opaque)
{
    adpf_thread *thread = opaque;
    if (thread->session) adpf_api.close(thread->session);
    memset(thread, 0, sizeof *thread);
}

static void adpf_load(void)
{
    /* Keep the system DSO loaded while TLS destructors may still use its API. */
    void *library = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
    if (!library) { void_log("[adpf] unavailable: libandroid.so"); return; }
    *(void **)&adpf_api.manager = dlsym(library, "APerformanceHint_getManager");
    *(void **)&adpf_api.create = dlsym(library, "APerformanceHint_createSession");
    *(void **)&adpf_api.report = dlsym(library, "APerformanceHint_reportActualWorkDuration");
    *(void **)&adpf_api.close = dlsym(library, "APerformanceHint_closeSession");
    if (!adpf_api.manager || !adpf_api.create || !adpf_api.report || !adpf_api.close) {
        void_log("[adpf] unavailable: native API33 entrypoints"); return;
    }
    adpf_api.instance = adpf_api.manager();
    if (!adpf_api.instance) { void_log("[adpf] unavailable: manager"); return; }
    int error = pthread_key_create(&adpf_key, adpf_destroy);
    if (error) { void_log("[adpf] TLS key failed %d", error); return; }
    adpf_api.ready = 1;
}

static int64_t adpf_now(void)
{
    struct timespec time;
    if (clock_gettime(CLOCK_MONOTONIC, &time) || time.tv_sec < 0 || time.tv_nsec < 0 ||
        time.tv_nsec >= 1000000000 || time.tv_sec > (INT64_MAX - time.tv_nsec) / INT64_C(1000000000)) return 0;
    return (int64_t)time.tv_sec * INT64_C(1000000000) + time.tv_nsec;
}

/* UI thread: a pure atomic cancellation, never an API close/report. `blocked`
 * is the combined (paused || Surface missing) snapshot from android_main. */
void ut3_adpf_cancel(int blocked)
{
    uint64_t before = atomic_load_explicit(&adpf_epoch, memory_order_acquire), after;
    do {
        after = ((before & ~UINT64_C(1)) + 2u) | (blocked ? 1u : 0u);
    } while (!atomic_compare_exchange_weak_explicit(&adpf_epoch, &before, after,
                                                   memory_order_acq_rel, memory_order_acquire));
}

/* Exact begin: original 00407E9D, after the tick limiter returned, including
 * its no-wait path. Never substitute Present-return or generic Sleep-return. */
void ut3_adpf_work_begin(void)
{
    int saved_errno = errno;
    if (void_ui_hud_frame_begin) void_ui_hud_frame_begin();
    if (void_ui_camera_frame_begin) void_ui_camera_frame_begin();
    adpf_thread *thread = &adpf_tls;
    thread->armed = 0;
    uint64_t epoch = atomic_load_explicit(&adpf_epoch, memory_order_acquire);
    if ((epoch & 1u) || thread->failed) goto done;
    pthread_once(&adpf_once, adpf_load);
    if (!adpf_api.ready) goto done;
    if (!thread->session) {
        thread->tid = (int32_t)syscall(SYS_gettid);
        if (thread->tid <= 0) { thread->failed = 1; goto done; }
        thread->session = adpf_api.create(adpf_api.instance, &thread->tid, 1, adpf_target_ns);
        if (!thread->session) {
            thread->failed = 1;
            void_log("[adpf] session unavailable for tid %d", thread->tid);
            goto done;
        }
        int error = pthread_setspecific(adpf_key, thread);
        if (error) {
            adpf_api.close(thread->session); thread->session = NULL; thread->failed = 1;
            void_log("[adpf] TLS registration failed %d", error); goto done;
        }
        void_log("[adpf] session tid %d target %lld ns", thread->tid, (long long)adpf_target_ns);
        /* The one-time lookup/session setup is not a steady workload sample. */
        goto done;
    }
    thread->start = adpf_now();
    if (thread->start > 0 && atomic_load_explicit(&adpf_epoch, memory_order_acquire) == epoch) {
        thread->epoch = epoch; thread->armed = 1;
    }
done:
    errno = saved_errno;
}

/* End of producer work: incoming guest Present, BEFORE the opaque native
 * Present/host limiter. This does not claim GPU or native-submission duration. */
void ut3_adpf_work_end(void)
{
    int saved_errno = errno;
    adpf_thread *thread = &adpf_tls;
    if (!thread->armed) goto done;
    thread->armed = 0; /* Multiple viewport Presents do not duplicate a sample. */
    int64_t end = adpf_now();
    uint64_t epoch = atomic_load_explicit(&adpf_epoch, memory_order_acquire);
    if (epoch != thread->epoch || (epoch & 1u) || end <= thread->start) goto done;
    int error = adpf_api.report(thread->session, end - thread->start);
    if (error) {
        /* Owner-only close; UI cancellation never races this API operation.
         * Disable after communication/argument failure rather than churn per frame. */
        adpf_api.close(thread->session); thread->session = NULL; thread->failed = 1;
        pthread_setspecific(adpf_key, NULL);
        void_log("[adpf] report failed tid %d status %d", thread->tid, error);
    }
done:
    errno = saved_errno;
}

/* Owner-only normal shutdown; pthread TLS destruction is the fallback. */
void ut3_adpf_thread_close(void)
{
    int saved_errno = errno;
    if (adpf_tls.session) {
        pthread_setspecific(adpf_key, NULL);
        adpf_destroy(&adpf_tls);
    }
    errno = saved_errno;
}
