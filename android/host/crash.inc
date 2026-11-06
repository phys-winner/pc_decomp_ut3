/* Pre-opened crash journal, included by core.c: no heap, stdio or mutexes on a signal. */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int crash_fd = -1;
static int crash_written;

void void_crash_open(const char *log_path)
{
    char path[1100];
    const char *name = strrchr(log_path, '/');
    if (!name || (size_t)(name - log_path) > sizeof path - 32) return;
    snprintf(path, sizeof path, "%.*s/ut3-crash.txt", (int)(name - log_path), log_path);
    if (crash_fd >= 0) close(crash_fd);
    crash_fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    __atomic_store_n(&crash_written, 0, __ATOMIC_RELAXED);
}

static void crash_write(const char *text, size_t size)
{
    while (size) {
        ssize_t count = write(crash_fd, text, size);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        text += count; size -= (size_t)count;
    }
}

void void_crash_record(const char *reason)
{
    if (crash_fd < 0 || __atomic_exchange_n(&crash_written, 1, __ATOMIC_RELAXED)) return;
    size_t size = 0;
    while (reason[size] && size < 2048) ++size;
    crash_write(reason, size);
    crash_write("\n", 1);
    fsync(crash_fd);
}

static char *crash_hex(char *out, uintptr_t value)
{
    *out++ = '0'; *out++ = 'x';
    for (int shift = (int)(sizeof value * 8) - 4; shift >= 0; shift -= 4)
        *out++ = "0123456789abcdef"[(value >> shift) & 15];
    return out;
}

void void_crash_signal(int signo, int code, uintptr_t fault, uintptr_t pc, uintptr_t lr,
                       uint32_t guest_function, uint32_t guest_stack)
{
    const char *name = signo == SIGSEGV ? "SIGSEGV" : signo == SIGBUS ? "SIGBUS" :
        signo == SIGILL ? "SIGILL" : signo == SIGFPE ? "SIGFPE" : signo == SIGABRT ? "SIGABRT" : "signal";
    char buffer[512], *out = buffer;
#define CRASH_TEXT(s) do { const char *p = (s); while (*p) *out++ = *p++; } while (0)
#define CRASH_VALUE(s,v) do { CRASH_TEXT(s); out = crash_hex(out, (uintptr_t)(v)); *out++ = '\n'; } while (0)
    CRASH_TEXT("Native crash: "); CRASH_TEXT(name); *out++ = '\n';
    CRASH_VALUE("Signal code: ", (intptr_t)code);
    CRASH_VALUE("Fault address: ", fault);
    CRASH_VALUE("Host PC: ", pc);
    CRASH_VALUE("Host LR: ", lr);
    CRASH_VALUE("Guest function: ", guest_function);
    CRASH_VALUE("Guest stack: ", guest_stack);
    *out = 0;
    void_crash_record(buffer);
#undef CRASH_VALUE
#undef CRASH_TEXT
}

void void_crash_exit(uint32_t code)
{
    char message[64];
    if (!code) void_crash_record("Clean exit");
    else {
        snprintf(message, sizeof message, "Game exited with code %u", code);
        void_crash_record(message);
    }
}
