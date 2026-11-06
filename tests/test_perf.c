/* Late combat hitches stay visible only while opt-in profiling is enabled. */
#define clock_gettime test_clock_gettime
#include "../android/host/perf.c"
#include <assert.h>
#include <stdarg.h>
#include <unistd.h>

static char directory[] = "/data/local/tmp/ut3-perf-XXXXXX";
static uint64_t now_ns = 1000000000ull;
static unsigned lines;

int test_clock_gettime(clockid_t clock, struct timespec *ts)
{
    assert(clock == CLOCK_MONOTONIC);
    ts->tv_sec = now_ns / 1000000000ull;
    ts->tv_nsec = now_ns % 1000000000ull;
    return 0;
}
const char *void_data_dir(void) { return directory; }
void void_log(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    assert(!strcmp(format, "%s"));
    const char *line = va_arg(args, const char *);
    assert(strstr(line, "100.0 ms;") && strstr(line, "texture 1/1.0 ms/1.0 MiB;"));
    ++lines;
    va_end(args);
}
int main(int argc, char **argv)
{
    (void)argv;
    assert(mkdtemp(directory));
    char flag_path[256];
    snprintf(flag_path, sizeof flag_path, "%s/gpu-profile", directory);
    if (argc > 1) { FILE *flag = fopen(flag_path, "wb"); assert(flag); fclose(flag); }
    void_perf_frame_end(0);
    for (unsigned frame = 1; frame <= 500; ++frame) {
        now_ns += 100000000;
        void_perf_add(VOID_PERF_TEXTURE, 1000000, 1048576);
        void_perf_frame_end(frame);
    }
    assert(lines == (argc > 1 ? 500u : 400u));
    assert(void_perf_calls(VOID_PERF_TEXTURE) == 500);
    if (argc > 1) assert(!unlink(flag_path));
    assert(!rmdir(directory));
    puts("PASS hitch attribution: 400 production limit, opt-in late frames, per-frame deltas");
    return 0;
}
