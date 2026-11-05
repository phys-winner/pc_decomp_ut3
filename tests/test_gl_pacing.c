/* Exercise the actual production pacing and D3D9->EGL interval selection. */
#define clock_nanosleep test_clock_nanosleep
#define eglSwapInterval test_eglSwapInterval
#define glGetIntegerv test_glGetIntegerv
#define glGetQueryObjectuiv test_glGetQueryObjectuiv
#include "../android/host/d3d9_gl.c"
#include <assert.h>

static uint64_t test_now = UINT64_C(1000000000);
static unsigned waits, enters, leaves;
static int interrupt_once, swap_interval_seen = -1;
static unsigned gpu_begins, gpu_ends, gpu_reads;
static unsigned gpu_reported, gpu_invalid;
static int gpu_ready, gpu_disjoint;

void test_glGetIntegerv(GLenum name, GLint *value)
{
    assert(name == GL_GPU_DISJOINT_EXT); *value = gpu_disjoint; gpu_disjoint = 0;
}
void test_glGetQueryObjectuiv(GLuint query, GLenum name, GLuint *value)
{
    assert(query && name == GL_QUERY_RESULT_AVAILABLE); *value = gpu_ready;
}
static void test_gpu_begin(GLenum target, GLuint query)
{
    assert(target == GL_TIME_ELAPSED_EXT && query); ++gpu_begins;
}
static void test_gpu_end(GLenum target) { assert(target == GL_TIME_ELAPSED_EXT); ++gpu_ends; }
static void test_gpu_result(GLuint query, GLenum name, GLuint64 *value)
{
    assert(query && name == GL_QUERY_RESULT && gpu_ready); *value = UINT64_C(12345678901); ++gpu_reads;
}

uint64_t void_perf_now_ns(void) { return test_now; }
void void_blocking_begin(void) { ++enters; }
void void_blocking_end(void) { ++leaves; }
void void_log(const char *format, ...) {
    if (strstr(format, "render_ns")) ++gpu_reported;
    if (strstr(format, "invalid timer result")) ++gpu_invalid;
}
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
    /* No render thread here (d3d_rt_start never ran): the records run inline. */
    window_mode = 1; egl_surface = (EGLSurface)(uintptr_t)1; surface_live = 1;
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
    gpu_begin_query = test_gpu_begin; gpu_end_query = test_gpu_end; gpu_query_result = test_gpu_result;
    for (unsigned i = 0; i < GPU_PROFILE_QUERIES; ++i) gpu_queries[i] = i + 1;
    gpu_profile_begin(11); gpu_profile_begin(11); test_now += UINT64_C(20000000000); gpu_profile_end(11);
    assert(gpu_begins == 1 && gpu_ends == 1 && gpu_pending[0] && !gpu_reads);
    gpu_ready = 1; gpu_profile_end(12);
    assert(gpu_reads == 1 && gpu_reported == 1 && !gpu_pending[0]);
    gpu_ready = 0;
    for (unsigned i = 0; i < GPU_PROFILE_QUERIES + 2; ++i) { gpu_profile_begin(20+i); gpu_profile_end(20+i); }
    assert(gpu_begins == 1+GPU_PROFILE_QUERIES && gpu_ends == gpu_begins);
    gpu_disjoint = 1; gpu_profile_end(40);
    gpu_ready = 1; gpu_profile_end(41);
    assert(gpu_reads == 1);  /* Disjoint pending samples are freed, never reported as measurements. */
    for (unsigned i = 0; i < GPU_PROFILE_QUERIES; ++i) assert(!gpu_pending[i]);
    gpu_profile_begin(42); gpu_profile_end(42); assert(gpu_reads == 2 && gpu_reported == 1 && gpu_invalid == 1);
    puts("PASS GLES: actual120/60 pacing, EINTR retry, no catch-up burst, D3D9 immediate vs synchronized EGL interval");
    puts("PASS GPU profiler: one begin per frame, 64-bit ready results only, full pool skips without waiting, disjoint and impossible driver results discarded");
    return 0;
}
