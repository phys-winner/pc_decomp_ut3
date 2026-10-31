/* Production read-only registry: genuine denied writes and owned typed keys. */
#include "../android/host/system.c"
#include <assert.h>

void void_process_exit(uint32_t code, int detach) { (void)detach; _exit((int)code); }
static uint32_t api(const char *name, unsigned count, const uint32_t *args)
{
    const void_shim *shim = void_shim_find("advapi32.dll", name, 0); assert(shim);
    uint32_t before = g_esp; void_set_last_error(0x1234);
    uint32_t result = void_call_guest(void_thunk(shim->fn, name), count, args, 1);
    assert(before == g_esp && void_get_last_error() == 0x1234); return result;
}
static uint32_t string(const char *text, int wide)
{
    uint32_t out = void_galloc((strlen(text) + 1) * (wide ? 2 : 1)); assert(out);
    if (wide) void_utf8_to_gw(text, out, strlen(text) + 1);
    else void_utf8_to_ga(text, out, strlen(text) + 1);
    return out;
}

int main(void)
{
    assert(void_guest_reserve()); void_threads_init(); void_dispatch_init(); void_machine_enter();
    uint32_t storage = void_galloc(128), out = storage + 1, disposition = storage + 9;
    memset(GPTR(storage), 0xA5, 128);
    uint32_t steam = string("Software\\Valve\\Steam", 0);
    uint32_t create[9] = {HKEY_LOCAL_MACHINE_, steam, 0, 0, 0, 0xF003F, 0, out, disposition};
    assert(api("RegCreateKeyExA", 9, create) == 5);
    assert(MEM32(out) == 0xA5A5A5A5 && MEM32(disposition) == 0xA5A5A5A5);
    uint32_t missing[5] = {HKEY_LOCAL_MACHINE_, steam, 0, REG_READ_, out};
    assert(api("RegOpenKeyExA", 5, missing) == 2); /* Failed create added no key. */
    create[5] = REG_READ_; assert(api("RegCreateKeyExA", 9, create) == 5);
    const char *path = "Software\\Epic Games\\Unreal Tournament 3";
    for (unsigned wide = 0; wide < 2; ++wide) {
        uint32_t sub = string(path, wide), name = string("InstalledDirectory", wide);
        uint32_t open[5] = {HKEY_LOCAL_MACHINE_, sub, 0, REG_READ_ | 0x200, out};
        assert(api(wide ? "RegOpenKeyExW" : "RegOpenKeyExA", 5, open) == 0);
        uint32_t key = MEM32(out); assert(void_handle_get(key, REGISTRY_KEY_));
        uint32_t query[6] = {key, name, 0, storage + 16, 0, storage + 20};
        assert(api(wide ? "RegQueryValueExW" : "RegQueryValueExA", 6, query) == 0);
        uint32_t needed = (strlen("C:\\Games\\UT3") + 1) * (wide ? 2 : 1);
        assert(MEM32(storage + 16) == REG_SZ_ && MEM32(storage + 20) == needed);
        query[4] = storage + 32; MEM32(storage + 20) = needed - 1;
        assert(api(wide ? "RegQueryValueExW" : "RegQueryValueExA", 6, query) == ERROR_MORE_DATA_);
        assert(MEM8(storage + 32) == 0xA5 && MEM32(storage + 20) == needed);
        MEM32(storage + 20) = needed;
        assert(api(wide ? "RegQueryValueExW" : "RegQueryValueExA", 6, query) == 0);
        char *value = wide ? void_wstr_utf8(storage + 32) : void_astr_utf8(storage + 32);
        assert(!strcmp(value, "C:\\Games\\UT3")); free(value);
        query[2] = storage; assert(api(wide ? "RegQueryValueExW" : "RegQueryValueExA", 6, query) == 87);
        query[2] = 0; query[3] = 0x1000;
        assert(api(wide ? "RegQueryValueExW" : "RegQueryValueExA", 6, query) == 998);
        assert(api("RegCloseKey", 1, &key) == 0 && api("RegCloseKey", 1, &key) == 6);
        create[1] = sub; create[5] = REG_READ_;
        assert(api(wide ? "RegCreateKeyExW" : "RegCreateKeyExA", 9, create) == 0);
        assert(MEM32(disposition) == 2); key = MEM32(out); assert(api("RegCloseKey", 1, &key) == 0);
        create[5] = 0xF003F;
        assert(api(wide ? "RegCreateKeyExW" : "RegCreateKeyExA", 9, create) == 5);
        void_gfree(sub); void_gfree(name);
        memset(GPTR(storage + 32), 0xA5, 64);
    }
    uint32_t empty = string("", 0), root_open[5] = {HKEY_LOCAL_MACHINE_, empty, 0, REG_READ_, out};
    assert(api("RegOpenKeyExA", 5, root_open) == 0 && MEM32(out) == HKEY_LOCAL_MACHINE_);
    create[1] = empty; create[5] = REG_READ_;
    assert(api("RegCreateKeyExA", 9, create) == 0 && MEM32(out) != HKEY_LOCAL_MACHINE_ && MEM32(disposition) == 2);
    uint32_t key = MEM32(out); assert(api("RegCloseKey", 1, &key) == 0);
    create[1] = 0; assert(api("RegCreateKeyExA", 9, create) == 1010);
    create[1] = empty; create[2] = 1; assert(api("RegCreateKeyExA", 9, create) == 87); create[2] = 0;
    create[4] = 0x80000000; assert(api("RegCreateKeyExA", 9, create) == 87); create[4] = 0;
    create[7] = 0; assert(api("RegCreateKeyExA", 9, create) == 998); create[7] = out;
    create[8] = 0x1000; assert(api("RegCreateKeyExA", 9, create) == 998); create[8] = disposition;
    uint32_t foreign = void_handle_new(void_object_new(VOID_OBJ_OTHER, 0));
    root_open[0] = foreign; assert(api("RegOpenKeyExA", 5, root_open) == 6);
    uint32_t query[6] = {foreign, 0, 0, 0, 0, 0};
    assert(api("RegQueryValueExA", 6, query) == 6 && api("RegCloseKey", 1, &foreign) == 6);
    assert(void_handle_get(foreign, VOID_OBJ_OTHER)); assert(void_handle_close(foreign));
    uint32_t event = void_handle_new(void_object_new(VOID_OBJ_EVENT, 0));
    assert(api("RegCloseKey", 1, &event) == 6 && void_handle_get(event, VOID_OBJ_EVENT));
    assert(void_handle_close(event));
    uint32_t page = void_vm_alloc(0, 0x1000, 0x3000, 4); assert(page);
    MEM8(page + 0xFFF) = 'x'; root_open[0] = HKEY_LOCAL_MACHINE_; root_open[1] = page + 0xFFF;
    assert(api("RegOpenKeyExA", 5, root_open) == 998);
    root_open[1] = empty; root_open[4] = page + 0xFFD;
    assert(api("RegOpenKeyExA", 5, root_open) == 998);
    assert(MEM8(storage) == 0xA5 && MEM8(storage + 5) == 0xA5 && MEM8(storage + 13) == 0xA5);
    puts("PASS read-only registry: Steam write denial/no mutation, A/W existing-key create/open/query, disposition/required bytes, root/empty/null semantics, typed ownership, bounds, LastError/stdcall");
    return 0;
}
