/* Production A/W getters share the real environment model; x86 ABI included. */
#include "../android/host/kernel32.c"
#include "../android/host/compat.c"
#include <assert.h>

void void_process_exit(uint32_t code, int detach) { (void)detach; _exit((int)code); }

static uint32_t api(const char *name, int count, const uint32_t *args)
{
    const void_shim *shim = void_shim_find("kernel32.dll", name, 0);
    assert(shim);
    uint32_t before = g_esp;
    uint32_t result = void_call_guest(void_thunk(shim->fn, name), count, args, 1);
    assert(g_esp == before);
    return result;
}

static void check_wide(const char *name, const char *value, const uint16_t *expected, uint32_t units)
{
    size_t n = strlen(name) + strlen(value) + 2;
    environment[environment_count] = malloc(n);
    assert(environment[environment_count]);
    snprintf(environment[environment_count++], n, "%s=%s", name, value);
    uint32_t key = void_galloc(128), out = void_galloc(64);
    void_utf8_to_gw(name, key, 64);
    for (uint32_t cap = 0; cap < units + 3; ++cap) {
        memset(GPTR(out), 0xA5, 64);
        uint32_t args[] = {key, cap ? out : 0, cap};
        void_set_last_error(123);
        uint32_t result = api("GetEnvironmentVariableW", 3, args);
        assert(result == (cap < units + 1 ? units + 1 : units));
        assert(void_get_last_error() == 123);
        if (!cap) assert(MEM16(out) == 0xA5A5);
        else if (cap < units + 1) assert(MEM16(out) == 0 && MEM16(out + 2) == 0xA5A5);
        else {
            assert(!memcmp(GPTR(out), expected, units * 2));
            assert(!MEM16(out + units * 2) && MEM16(out + (units + 1) * 2) == 0xA5A5);
            assert(api("lstrlenW", 1, &out) == units);
        }
    }
    void_gfree(out); void_gfree(key);
}

int main(void)
{
    assert(void_guest_reserve());
    void_threads_init(); void_dispatch_init(); void_machine_enter(); env_init();
    static const uint16_t ascii[] = {'1', '3', '2', '1', '0'};
    static const uint16_t unicode[] = {0x0422, 0x0435, 0x0441, 0x0442};
    static const uint16_t surrogate[] = {'A', 0xD800, 0xDF48, 'B'};
    static const uint16_t empty[] = {0};
    check_wide("SteamAppId", "13210", ascii, 5);
    check_wide("UT3_UNICODE", "\xD0\xA2\xD0\xB5\xD1\x81\xD1\x82", unicode, 4);
    check_wide("UT3_SURROGATE", "A\xF0\x90\x8D\x88" "B", surrogate, 4);
    check_wide("UT3_EMPTY", "", empty, 0);
    uint32_t key = void_galloc(128), out = void_galloc(64);
    void_utf8_to_gw("UT3_MISSING", key, 64);
    uint32_t args[] = {key, out, 32};
    memset(GPTR(out), 0xA5, 64);
    assert(!api("GetEnvironmentVariableW", 3, args));
    assert(void_get_last_error() == ERROR_ENVVAR_NOT_FOUND_ && MEM16(out) == 0xA5A5);
    void_gfree(key);
    key = void_gstrdup("UT3_UNICODE");
    args[0] = key; args[2] = 4;
    assert(api("GetEnvironmentVariableA", 3, args) == 5);
    args[2] = 5;
    assert(api("GetEnvironmentVariableA", 3, args) == 4);
    const uint8_t ansi[] = {0xD2, 0xE5, 0xF1, 0xF2, 0};
    assert(!memcmp(GPTR(out), ansi, sizeof ansi));
    uint32_t null = 0;
    assert(!api("lstrlenW", 1, &null));
    void_gfree(key); void_gfree(out); void_machine_leave();
    puts("Environment A/W PASS: required size, empty/missing, UTF-16 surrogate units, CP1251 capacity, guard bytes, lstrlenW and stdcall");
    return 0;
}
