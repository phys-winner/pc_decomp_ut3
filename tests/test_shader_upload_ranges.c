/* Exact upload ranges/calls against the previous scanner; optional CPU-only scanner benchmark. */
#include "../android/host/d3d9_internal.h"
#include <assert.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    unsigned kind, program;
    int location, count;
    char name[64];
    uint32_t words[256 * 4];
} upload_event;
typedef struct { unsigned count; upload_event events[2]; } upload_trace;
static upload_trace *observed;
static int missing_location;
static volatile uint32_t bench_sink;

static GLint observed_location(GLuint program, const char *name)
{
    int index = 0;
    const char *bracket = strchr(name, '[');
    assert(bracket && sscanf(bracket + 1, "%d", &index) == 1 && index > 0 && index < 256);
    if (observed) {
        assert(observed->count < 2);
        upload_event *event = &observed->events[observed->count++];
        event->kind = 1; event->program = program;
        snprintf(event->name, sizeof event->name, "%s", name);
    }
    return missing_location ? -1 : 1009 + 3 * index;
}

static void observed_uniform(GLint location, GLsizei count, const GLfloat *values)
{
    assert(count > 0 && count <= 256);
    if (observed) {
        assert(observed->count < 2);
        upload_event *event = &observed->events[observed->count++];
        event->kind = 2; event->location = location; event->count = count;
        memcpy(event->words, values, 16u * (unsigned)count);
    } else {
        uint32_t first;
        memcpy(&first, values, sizeof first);
        bench_sink += first ^ (unsigned)location ^ (unsigned)count;
    }
}

#define glGetUniformLocation observed_location
#define glUniform4fv observed_uniform
#include "../android/host/d3d9_shader.c"
#undef glUniform4fv
#undef glGetUniformLocation

void void_process_exit(uint32_t code, int detach) { (void)detach; _exit((int)code); }

static void original_upload(GLuint prog, const char *name, GLint base, GLint *locs, float *held,
    const float *fresh, int count)
{
    int first = 0, last = count - 1;
    while (first < count && !memcmp(held + 4 * first, fresh + 4 * first, 16)) ++first;
    if (first == count) return;
    while (last > first && !memcmp(held + 4 * last, fresh + 4 * last, 16)) --last;
    memcpy(held + 4 * first, fresh + 4 * first, 16u * (uint32_t)(last - first + 1));
    if (first && locs[first] == -2) {
        char element[64];
        snprintf(element, sizeof element, "%s[%d]", name, first);
        locs[first] = observed_location(prog, element);
    }
    if (!first) observed_uniform(base, last + 1, held);
    else if (locs[first] >= 0) observed_uniform(locs[first], last - first + 1, held + 4 * first);
    else observed_uniform(base, last + 1, held);
}

static uint32_t random_state = 0x12B71A03u;
static uint32_t next(void) { random_state = random_state * 1664525u + 1013904223u; return random_state; }

static void differential(void)
{
    const uint32_t special[] = {0, 0x80000000u, 0x7FC00001u, 0x7FC00002u, 0xFFC12345u, 1, 0x3F800000u};
    for (unsigned test = 0; test < 257u * 96u; ++test) {
        const int count = (int)(test % 257u);
        float old_held[1024], new_held[1024], fresh[1024], saved[1024];
        GLint old_locs[257], new_locs[257];
        for (unsigned i = 0; i < 1024; ++i) {
            uint32_t word = i % 3 ? next() : special[(i + test) % (sizeof special / sizeof *special)];
            memcpy(old_held + i, &word, sizeof word);
        }
        memcpy(new_held, old_held, sizeof new_held);
        memcpy(fresh, old_held, sizeof fresh);
        unsigned edits = test % 8 == 0 ? 0 : test % 8 == 1 ? (unsigned)count : 1 + test % 19;
        for (unsigned edit = 0; count && edit < edits; ++edit) {
            unsigned index = test % 8 == 1 ? edit * 4 : next() % ((unsigned)count * 4);
            uint32_t word;
            memcpy(&word, fresh + index, sizeof word);
            word ^= 1u << (next() & 31u);
            memcpy(fresh + index, &word, sizeof word);
        }
        memcpy(saved, fresh, sizeof saved);
        for (unsigned i = 0; i < 257; ++i)
            old_locs[i] = new_locs[i] = (test + i) % 3 ? (GLint)(703 + i * 5) : test % 2 ? -1 : -2;
        missing_location = test % 5 == 0;
        const char *name = test % 2 ? "vs_uniforms_vec4" : "ps_uniforms_vec4";
        upload_trace reference = {0}, actual = {0};
        observed = &reference; original_upload(17, name, 97, old_locs, old_held, fresh, count);
        observed = &actual; upload_vec4(17, name, 97, new_locs, new_held, fresh, count);
        assert(!memcmp(&reference, &actual, sizeof actual));
        assert(!memcmp(old_held, new_held, sizeof new_held));
        assert(!memcmp(old_locs, new_locs, sizeof new_locs));
        assert(!memcmp(fresh, saved, sizeof fresh));
    }
    puts("PASS shader upload ranges: 24672 cases, counts 0..256, sparse/dense/unchanged bits, NaN/-0, cached/missing locations and exact GL traces");
}

typedef void (*upload_fn)(GLuint, const char *, GLint, GLint *, float *, const float *, int);
static uint64_t now_ns(void)
{
    struct timespec t; assert(!clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t));
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}

static __attribute__((noinline)) double measure(upload_fn upload, unsigned iterations, int count, int pattern)
{
    float held[1024] = {0}, fresh[1024] = {0};
    GLint locations[257];
    for (unsigned i = 0; i < 257; ++i) locations[i] = (GLint)(703 + 5 * i);
    unsigned start = pattern == 1 ? 0 : pattern == 2 ? (unsigned)(count - 4) / 2 : (unsigned)count - 4;
    observed = NULL;
    uint64_t begin = now_ns();
    for (unsigned iteration = 0; iteration < iterations; ++iteration) {
        if (pattern) {
            uint32_t word = 0x3F800000u | (iteration & 1u);
            for (unsigned i = 0; i < 16; ++i) memcpy(fresh + start * 4 + i, &word, sizeof word);
        }
        upload(17, "vs_uniforms_vec4", 97, locations, held, fresh, count);
    }
    uint64_t elapsed = now_ns() - begin;
    uint32_t final; memcpy(&final, held + start * 4, sizeof final); bench_sink ^= final;
    return (double)elapsed / iterations;
}

static void benchmark(unsigned iterations)
{
    const int counts[] = {4, 16, 64, 256};
    const char *patterns[] = {"unchanged", "matrix-begin", "matrix-middle", "matrix-end"};
    puts("count,pattern,old_ns,new_ns,change_percent");
    for (unsigned count = 0; count < sizeof counts / sizeof *counts; ++count)
        for (int pattern = 0; pattern < 4; ++pattern) {
            double old = 0, fresh = 0;
            for (unsigned pair = 0; pair < 3; ++pair) {
                if (pair & 1u) {
                    fresh += measure(upload_vec4, iterations, counts[count], pattern);
                    old += measure(original_upload, iterations, counts[count], pattern);
                } else {
                    old += measure(original_upload, iterations, counts[count], pattern);
                    fresh += measure(upload_vec4, iterations, counts[count], pattern);
                }
            }
            printf("%d,%s,%.3f,%.3f,%+.2f\n", counts[count], patterns[pattern], old / 3, fresh / 3, 100 * (fresh / old - 1));
        }
    printf("benchmark checksum %u\n", bench_sink);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    assert(argc == 1 || (argc >= 2 && argc <= 3 && !strcmp(argv[1], "--bench")));
    differential();
    if (argc > 1) {
        char *end = NULL;
        unsigned long iterations = argc == 3 ? strtoul(argv[2], &end, 10) : 200000;
        assert(iterations >= 1000 && iterations <= 5000000 && (!end || !*end));
        benchmark((unsigned)iterations);
    }
    return 0;
}
