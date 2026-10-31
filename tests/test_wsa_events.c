/* Real WSA event exports, shared Kernel32 objects and machine-aware wait. */
#include "../android/host/network.c"
#include <assert.h>

void void_process_exit(uint32_t code, int detach) { (void)detach; _exit((int)code); }
static uint32_t api_dll(const char *dll, const char *name, unsigned count, const uint32_t *args)
{
    const void_shim *shim = void_shim_find(dll, name, 0); assert(shim);
    uint32_t before = g_esp, result = void_call_guest(void_thunk(shim->fn, name), count, args, 1);
    assert(before == g_esp); return result;
}
static uint32_t api(const char *name, unsigned count, const uint32_t *args)
{ return api_dll("ws2_32.dll", name, count, args); }
static uint32_t last(void) { return api("WSAGetLastError", 0, NULL); }
static uint32_t worker_event;
static void *worker(void *unused)
{
    (void)unused; usleep(40000); void_machine_enter();
    uint32_t error = 0x4321; api("WSASetLastError", 1, &error);
    assert(api("WSASetEvent", 1, &worker_event)); assert(last() == error);
    void_machine_leave(); return NULL;
}

int main(void)
{
    alarm(10); assert(void_guest_reserve()); void_threads_init(); void_dispatch_init(); void_machine_enter();
    uint32_t prestartup = api("WSACreateEvent", 0, NULL);
    assert(prestartup && last() == 0 && api("WSACloseEvent", 1, &prestartup));
    uint32_t startup[2] = {0x202, void_galloc(400)};
    assert(!api("WSAStartup", 2, startup));
    uint32_t error = 0x1234; api("WSASetLastError", 1, &error);
    assert(api_dll("kernel32.dll", "GetLastError", 0, NULL) == error);
    uint32_t events[2] = {api("WSACreateEvent", 0, NULL), api("WSACreateEvent", 0, NULL)};
    assert(events[0] && events[1] && events[0] != events[1] && last() == 0);
    api("WSASetLastError", 1, &error);
    uint32_t shared_error = 0x3210;
    api_dll("kernel32.dll", "SetLastError", 1, &shared_error); assert(last() == shared_error);
    api("WSASetLastError", 1, &error);
    assert(void_handle_get(events[0], VOID_OBJ_EVENT)->manual_reset);
    uint32_t list = void_galloc(12) + 1;
    MEM32(list) = events[0]; MEM32(list + 4) = events[1];
    uint32_t wait[5] = {2, list, 0, 0, 0};
    assert(api("WSAWaitForMultipleEvents", 5, wait) == WAIT_TIMEOUT_);
    assert(api("WSASetEvent", 1, &events[1]));
    assert(api("WSAWaitForMultipleEvents", 5, wait) == 1);
    assert(api("WSAWaitForMultipleEvents", 5, wait) == 1); /* Manual reset persists. */
    wait[2] = 1; assert(api("WSAWaitForMultipleEvents", 5, wait) == WAIT_TIMEOUT_);
    assert(api("WSASetEvent", 1, &events[0])); assert(api("WSAWaitForMultipleEvents", 5, wait) == 0);
    assert(api("WSAResetEvent", 1, &events[0]) && api("WSAResetEvent", 1, &events[1]));
    wait[2] = 0; wait[3] = 15; wait[4] = 1;
    struct timespec before, after; clock_gettime(CLOCK_MONOTONIC, &before);
    assert(api("WSAWaitForMultipleEvents", 5, wait) == WAIT_TIMEOUT_);
    clock_gettime(CLOCK_MONOTONIC, &after);
    int64_t ns = (after.tv_sec - before.tv_sec) * 1000000000ll + after.tv_nsec - before.tv_nsec;
    assert(ns >= 10000000 && last() == error);
    pthread_t thread; worker_event = events[0]; assert(!pthread_create(&thread, NULL, worker, NULL));
    wait[3] = 1000; wait[4] = 0;
    assert(api("WSAWaitForMultipleEvents", 5, wait) == 0 && last() == error); pthread_join(thread, NULL);
    uint32_t single[2] = {events[0], 0};
    assert(api_dll("kernel32.dll", "WaitForSingleObject", 2, single) == 0);
    assert(api_dll("kernel32.dll", "ResetEvent", 1, &events[0]));
    single[0] = events[0]; assert(api_dll("kernel32.dll", "WaitForSingleObject", 2, single) == WAIT_TIMEOUT_);
    uint32_t create[4] = {0, 0, 0, 0};
    uint32_t automatic = api_dll("kernel32.dll", "CreateEventW", 4, create);
    MEM32(list) = automatic; wait[0] = 1; wait[3] = 0;
    assert(api("WSASetEvent", 1, &automatic));
    assert(api("WSAWaitForMultipleEvents", 5, wait) == 0);
    assert(api("WSAWaitForMultipleEvents", 5, wait) == WAIT_TIMEOUT_); /* Kernel auto-reset shared. */
    assert(api("WSACloseEvent", 1, &automatic));
    wait[0] = 0; assert(api("WSAWaitForMultipleEvents", 5, wait) == WAIT_FAILED_ && last() == 87);
    wait[0] = 65; assert(api("WSAWaitForMultipleEvents", 5, wait) == WAIT_FAILED_ && last() == 87);
    wait[0] = 1; wait[1] = 0x1000;
    assert(api("WSAWaitForMultipleEvents", 5, wait) == WAIT_FAILED_ && last() == WSA_EFAULT);
    wait[1] = list; MEM32(list) = 0;
    assert(api("WSAWaitForMultipleEvents", 5, wait) == WAIT_FAILED_ && last() == 6);
    uint32_t invalid = 0;
    assert(!api("WSASetEvent", 1, &invalid) && last() == 6);
    assert(!api("WSAResetEvent", 1, &invalid) && last() == 6);
    assert(!api("WSACloseEvent", 1, &invalid) && last() == 6);
    for (unsigned i = 0; i < 2; ++i) {
        assert(api("WSACloseEvent", 1, &events[i]));
        assert(!api("WSACloseEvent", 1, &events[i]) && last() == 6);
    }
    puts("PASS WSA events: shared manual/auto reset, any/all/index/timeouts, real cross-thread wake releasing guest machine, Kernel32 interoperability, WSA TLS/errors, bounds and stdcall");
    return 0;
}
