/* Exercise the actual production pacing and D3D9->EGL interval selection. */
#define clock_nanosleep test_clock_nanosleep
#define eglSwapInterval test_eglSwapInterval
#include "../android/host/d3d9_gl.c"
#include <assert.h>

static uint64_t test_now = UINT64_C(1000000000);
static unsigned waits, enters, leaves;
static int interrupt_once, swap_interval_seen = -1;

uint64_t void_perf_now_ns(void) { return test_now; }
void void_blocking_begin(void) { ++enters; }
void void_blocking_end(void) { ++leaves; }
void void_log(const char *format, ...) { (void)format; }
int test_clock_nanosleep(clockid_t clock, int flags, const struct timespec *request, struct timespec *remaining)
{
    (void)remaining;
    assert(clock == CLOCK_MONOTONIC && flags == TIMER_ABSTIME);
    ++waits;
    if (interrupt_once) { interrupt_once = 0; return EINTR; }
    test_now = (uint64_t)request->tv_sec * UINT64_C(1000000000) + request->tv_nsec;
    return 0;
}
EGLBoolean test_eglSwapInterval(EGLDisplay display, EGLint interval)
{
    (void)display; swap_interval_seen = interval; return EGL_TRUE;
}

int main(void)
{
    window_mode = 1; egl_surface = (EGLSurface)(uintptr_t)1;
    frame_limit = 120;
    d3d_gl_frame_pace();
    assert(frame_period_ns == 8333333 && !waits);
    uint64_t first = frame_deadline_ns;
    interrupt_once = 1;
    d3d_gl_frame_pace();
    assert(waits == 2 && enters == 1 && leaves == 1 && test_now == first);
    assert(frame_deadline_ns == first + 8333333);
    test_now += UINT64_C(1000000000);
    d3d_gl_frame_pace();
    assert(frame_deadline_ns == test_now + 8333333);
    frame_limit = 60;
    d3d_gl_frame_pace();
    assert(frame_period_ns == 16666666);
    d3d_gl_set_present_interval(0x80000000u);
    assert(swap_interval_seen == 0 && !frame_deadline_ns);
    d3d_gl_set_present_interval(0);
    assert(swap_interval_seen == 1);
    window_mode = 0;
    d3d_gl_frame_pace();
    assert(!frame_deadline_ns);
    puts("PASS GLES: actual120/60 pacing, EINTR retry, no catch-up burst, D3D9 immediate vs synchronized EGL interval");
    return 0;
}
