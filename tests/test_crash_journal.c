#define _GNU_SOURCE
#include <assert.h>
#include <stdlib.h>
#include "../android/host/crash.inc"

static void reported(int signo)
{
    (void)signo;
    void_crash_signal(SIGSEGV, 1, 0x1234, 0x11223344, 0x55667788, 0x00401234, 0x12340000);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    char log[1100], path[1100], body[1024] = {0};
    assert(snprintf(log, sizeof log, "%s/ut3.log", argv[1]) < (int)sizeof log);
    assert(snprintf(path, sizeof path, "%s/ut3-crash.txt", argv[1]) < (int)sizeof path);
    void_crash_open(log);
    struct sigaction action = {.sa_handler = reported};
    sigemptyset(&action.sa_mask);
    assert(!sigaction(SIGUSR1, &action, NULL));
    assert(!raise(SIGUSR1));
    void_crash_record("must not overwrite the first fault");
    FILE *file = fopen(path, "rb"); assert(file);
    assert(fread(body, 1, sizeof body - 1, file) > 0); fclose(file);
    assert(strstr(body, "Native crash: SIGSEGV\n"));
    assert(strstr(body, "0000000011223344"));
    assert(strstr(body, "0000000000401234"));
    assert(!strstr(body, "must not overwrite"));
    void_crash_open(log); void_crash_exit(0);
    memset(body, 0, sizeof body); file = fopen(path, "rb"); assert(file);
    assert(fread(body, 1, sizeof body - 1, file) == 11); fclose(file);
    assert(!strcmp(body, "Clean exit\n"));
    void_crash_open(log); void_crash_exit(70);
    memset(body, 0, sizeof body); file = fopen(path, "rb"); assert(file);
    assert(fread(body, 1, sizeof body - 1, file) > 0); fclose(file);
    assert(!strcmp(body, "Game exited with code 70\n"));
    puts("PASS native signal journal: real signal, precise fault/PC/guest fields, first fault retained, clean/error exits, reset");
}
