/* Optional diagnostic runtime: independent of guest globals and guest locking. */
#define _POSIX_C_SOURCE 200809L
#include "function_coverage_runtime.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Atomic uint32_t void_fc_state = VOID_FC_CUMULATIVE;
_Atomic uint64_t void_fc_cumulative[VOID_FC_WORDS];
_Atomic uint64_t void_fc_window[VOID_FC_WORDS];
static _Atomic int void_fc_armed;

__attribute__((noinline)) void void_function_coverage_mark(uint32_t word, uint64_t mask)
{
    uint32_t state = atomic_load_explicit(&void_fc_state, memory_order_relaxed);
    for (;;) {
        if (!(state & VOID_FC_FLAGS)) return;
        /* Admission and the open/closed state are one atomic operation. Stop
         * cannot miss a writer that was admitted before closing the banks. */
        if (atomic_compare_exchange_weak_explicit(&void_fc_state, &state,
                state + VOID_FC_WRITER, memory_order_acquire, memory_order_relaxed)) break;
    }
#ifdef VOID_FC_TEST_HOOK
    extern void void_fc_test_admitted(uint32_t word, uint64_t mask);
    void_fc_test_admitted(word, mask);
#endif
    if (state & VOID_FC_CUMULATIVE)
        atomic_fetch_or_explicit(&void_fc_cumulative[word], mask, memory_order_relaxed);
    if (state & VOID_FC_WINDOW)
        atomic_fetch_or_explicit(&void_fc_window[word], mask, memory_order_relaxed);
    atomic_fetch_sub_explicit(&void_fc_state, VOID_FC_WRITER, memory_order_release);
}

int void_function_coverage_arm(void)
{
    int expected = 0;
    if (!atomic_compare_exchange_strong(&void_fc_armed, &expected, 1)) return 0;
    atomic_fetch_or_explicit(&void_fc_state, VOID_FC_WINDOW, memory_order_acq_rel);
    return 1;
}

void void_function_coverage_stop(void)
{
    atomic_fetch_and_explicit(&void_fc_state, ~VOID_FC_FLAGS, memory_order_acq_rel);
    while (atomic_load_explicit(&void_fc_state, memory_order_acquire) != 0) {
        /* At most one tiny atomic OR section per admitted first hit remains.
         * This is the monitor thread, never the game/render/audio thread. */
    }
}

static int write_u32(FILE *file, uint32_t value)
{
    unsigned char b[4];
    for (unsigned i = 0; i != 4; ++i) b[i] = (unsigned char)(value >> (i * 8));
    return fwrite(b, 1, sizeof b, file) == sizeof b;
}

static int write_u64(FILE *file, uint64_t value)
{
    unsigned char b[8];
    for (unsigned i = 0; i != 8; ++i) b[i] = (unsigned char)(value >> (i * 8));
    return fwrite(b, 1, sizeof b, file) == sizeof b;
}

int void_function_coverage_export(const char *path, uint64_t accepted_ns,
    uint64_t start_before_ns, uint64_t start_after_ns, uint64_t stop_before_ns,
    uint64_t stop_after_ns, uint64_t warmup_ms, uint64_t duration_ms)
{
    if (atomic_load_explicit(&void_fc_state, memory_order_acquire) != 0) return 0;
    char temporary[2048];
    if (snprintf(temporary, sizeof temporary, "%s.tmp", path) >= (int)sizeof temporary) return 0;
    FILE *file = fopen(temporary, "wb");
    if (!file) return 0;
    int okay = fwrite("UT3COV1\0", 1, 8, file) == 8;
    okay &= write_u32(file, 1) && write_u32(file, VOID_FC_COUNT) && write_u32(file, VOID_FC_WORDS) && write_u32(file, 0);
    okay &= write_u64(file, accepted_ns) && write_u64(file, start_before_ns) && write_u64(file, start_after_ns);
    okay &= write_u64(file, stop_before_ns) && write_u64(file, stop_after_ns);
    okay &= write_u64(file, warmup_ms) && write_u64(file, duration_ms);
    okay &= fwrite(VOID_FC_IDENTITY, 1, 64, file) == 64;
    for (uint32_t i = 0; i != VOID_FC_WORDS; ++i)
        okay &= write_u64(file, atomic_load_explicit(&void_fc_cumulative[i], memory_order_relaxed));
    for (uint32_t i = 0; i != VOID_FC_WORDS; ++i)
        okay &= write_u64(file, atomic_load_explicit(&void_fc_window[i], memory_order_relaxed));
    if (fclose(file)) okay = 0;
    if (!okay) { remove(temporary); return 0; }
    if (rename(temporary, path)) { remove(temporary); return 0; }
    return 1;
}

/* Request grammar: two unsigned decimal fields separated by whitespace. Parsing
 * against the field limits also rejects integer overflow before multiplication. */
int void_function_coverage_parse_request(const char *text, size_t length,
    uint64_t *warmup_ms, uint64_t *duration_ms)
{
    const uint64_t limits[2] = {3600000, 600000};
    uint64_t values[2] = {0, 0};
    size_t position = 0;
    for (unsigned field = 0; field != 2; ++field) {
        while (position < length && isspace((unsigned char)text[position])) ++position;
        const size_t first = position;
        while (position < length && text[position] >= '0' && text[position] <= '9') {
            const unsigned digit = (unsigned)(text[position++] - '0');
            if (values[field] > limits[field] / 10 ||
                (values[field] == limits[field] / 10 && digit > limits[field] % 10)) return 0;
            values[field] = values[field] * 10 + digit;
        }
        if (position == first) return 0;
        if (field == 0 && (position == length || !isspace((unsigned char)text[position]))) return 0;
    }
    while (position < length && isspace((unsigned char)text[position])) ++position;
    if (position != length || values[1] == 0) return 0;
    *warmup_ms = values[0];
    *duration_ms = values[1];
    return 1;
}

#ifndef VOID_FC_NO_MONITOR
#include <pthread.h>
#include <time.h>
#include <unistd.h>

static uint64_t monotonic_ns(void)
{
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (uint64_t)time.tv_sec * UINT64_C(1000000000) + (uint64_t)time.tv_nsec;
}

static void sleep_until(uint64_t deadline)
{
    for (;;) {
        const uint64_t now = monotonic_ns();
        if (now >= deadline) return;
        const uint64_t remaining = deadline - now;
        struct timespec delay = {(time_t)(remaining / UINT64_C(1000000000)),
                                (long)(remaining % UINT64_C(1000000000))};
        nanosleep(&delay, NULL);
    }
}

static void status_file(const char *root, const char *state, uint64_t at,
                        uint64_t warmup, uint64_t duration)
{
    char path[2048], temporary[2048];
    if (snprintf(path, sizeof path, "%s/function-coverage.status.json", root) >= (int)sizeof path ||
        snprintf(temporary, sizeof temporary, "%s.tmp", path) >= (int)sizeof temporary) return;
    FILE *file = fopen(temporary, "wb");
    if (!file) return;
    const int written = fprintf(file,
        "{\"state\":\"%s\",\"monotonic_ns\":%llu,\"warmup_ms\":%llu,\"duration_ms\":%llu,"
        "\"identity_sha256\":\"%s\"}\n", state, (unsigned long long)at,
        (unsigned long long)warmup, (unsigned long long)duration, VOID_FC_IDENTITY);
    if (fclose(file) || written < 0 || rename(temporary, path)) remove(temporary);
}

static void *monitor(void *argument)
{
    char *root = argument;
    char request[2048], accepted_path[2048], output[2048];
    snprintf(request, sizeof request, "%s/function-coverage.request", root);
    snprintf(accepted_path, sizeof accepted_path, "%s/function-coverage.accepted", root);
    snprintf(output, sizeof output, "%s/function-coverage.bin", root);
    status_file(root, "WAITING", monotonic_ns(), 0, 0);
    for (;;) {
        FILE *file = fopen(request, "rb");
        if (!file) { sleep_until(monotonic_ns() + UINT64_C(100000000)); continue; }
        uint64_t warmup_ms = 0, duration_ms = 0;
        char request_text[128];
        const size_t length = fread(request_text, 1, sizeof request_text, file);
        const int valid = !ferror(file) && length < sizeof request_text &&
            void_function_coverage_parse_request(request_text, length, &warmup_ms, &duration_ms);
        fclose(file);
        if (!valid) {
            status_file(root, "INVALID_REQUEST", monotonic_ns(), 0, 0);
            remove(request);
            continue;
        }
        if (rename(request, accepted_path)) { sleep_until(monotonic_ns() + UINT64_C(100000000)); continue; }
        const uint64_t accepted = monotonic_ns();
        status_file(root, "WARMING", accepted, warmup_ms, duration_ms);
        sleep_until(accepted + warmup_ms * UINT64_C(1000000));
        const uint64_t start = monotonic_ns();
        if (!void_function_coverage_arm()) { status_file(root, "ARM_FAILED", start, warmup_ms, duration_ms); break; }
        const uint64_t start_after = monotonic_ns();
        status_file(root, "RECORDING", start, warmup_ms, duration_ms);
        sleep_until(start + duration_ms * UINT64_C(1000000));
        const uint64_t stop_before = monotonic_ns();
        void_function_coverage_stop();
        const uint64_t stop = monotonic_ns();
        const int okay = void_function_coverage_export(output, accepted, start, start_after,
            stop_before, stop, warmup_ms, duration_ms);
        status_file(root, okay ? "COMPLETE" : "EXPORT_FAILED", stop, warmup_ms, duration_ms);
        break;
    }
    free(root);
    return NULL;
}

int void_function_coverage_start(const char *userdata)
{
    static _Atomic int started;
    int expected = 0;
    if (!atomic_compare_exchange_strong(&started, &expected, 1)) return 0;
    if (!userdata || strlen(userdata) > 1900) return 0;
    char *copy = strdup(userdata);
    if (!copy) return 0;
    pthread_t thread;
    if (pthread_create(&thread, NULL, monitor, copy)) { free(copy); return 0; }
    pthread_detach(thread);
    return 1;
}
#endif
