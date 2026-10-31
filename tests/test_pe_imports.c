/* Include the production PE binder so malformed descriptors exercise its actual checks. */
#include "../android/host/pe.c"
#include <assert.h>

void void_process_exit(uint32_t code, int detach)
{
    (void)detach;
    _exit((int)code);
}

static void_module fixture(int delayed, int rva_mode)
{
    void_module m = {0};
    m.base = 0x00400000u;
    m.size = 0x2000;
    strcpy(m.name, "binder-test.exe");
    memset(GPTR(m.base), 0, m.size);
    MEM32(m.base + 0x3C) = 0x80;
    uint32_t dir = m.base + 0x80 + 0x78 + (delayed ? 13 : 1) * 8;
    MEM32(dir) = 0x300;
    MEM32(dir + 4) = delayed ? 64 : 40;
    strcpy((char *)GPTR(m.base + 0x400), "kernel32.dll");
    strcpy((char *)GPTR(m.base + 0x702), "GetTickCount");
    MEM32(m.base + 0x500) = (rva_mode ? 0 : m.base) + 0x700;
    uint32_t d = m.base + 0x300;
    if (delayed) {
        MEM32(d) = (uint32_t)rva_mode;
        MEM32(d + 4) = (rva_mode ? 0 : m.base) + 0x400;
        MEM32(d + 8) = (rva_mode ? 0 : m.base) + 0x650;
        MEM32(d + 12) = (rva_mode ? 0 : m.base) + 0x600;
        MEM32(d + 16) = (rva_mode ? 0 : m.base) + 0x500;
    } else {
        MEM32(d) = 0x500;
        MEM32(d + 12) = 0x400;
        MEM32(d + 16) = 0x600;
    }
    return m;
}

static void check_section_mapping(void)
{
    uint8_t file[0x600] = {0};
    #define PUT16(at, value) do { uint16_t v = (value); memcpy(file + (at), &v, 2); } while (0)
    #define PUT32(at, value) do { uint32_t v = (value); memcpy(file + (at), &v, 4); } while (0)
    PUT16(0, 0x5A4D);
    PUT32(0x3C, 0x80);
    PUT32(0x80, 0x4550);
    PUT16(0x84, 0x14C);
    PUT16(0x86, 2);
    PUT16(0x94, 0xE0);
    PUT16(0x98, 0x10B);
    PUT32(0xA8, 0x1000);
    PUT32(0xB4, 0x00500000);
    PUT32(0xD0, 0x3000);
    PUT32(0xD4, 0x200);
    PUT32(0x178 + 8, 4);
    PUT32(0x178 + 12, 0x1000);
    PUT32(0x178 + 16, 0x200);
    PUT32(0x178 + 20, 0x200);
    PUT32(0x1A0 + 8, 0x800);
    PUT32(0x1A0 + 12, 0x2000);
    PUT32(0x1A0 + 16, 0x200);
    PUT32(0x1A0 + 20, 0x400);
    file[0x3FF] = 0x5A;
    file[0x400] = 0xA5;
    char path[] = "/data/local/tmp/ut3-pe-map-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0 && write(fd, file, sizeof file) == sizeof file);
    close(fd);
    void_module m = {0};
    assert(map_image(&m, path, 0x00500000));
    unlink(path);
    assert(MEM8(0x005011FF) == 0x5A);
    assert(MEM8(0x00502000) == 0xA5);
    for (uint32_t i = 0x00502200; i < 0x00502800; ++i) assert(MEM8(i) == 0);
    #undef PUT16
    #undef PUT32
}

int main(void)
{
    assert(void_guest_reserve());
    void_threads_init();
    void_dispatch_init();
    assert(void_vm_claim(0x00400000u, 0x2000u, 0x04u));
    add_system_pseudos();
    assert(is_lifted_name("PhysXLoader.dll"));
    assert(is_lifted_name("physxloader"));
    assert(!find_by_name("PhysXLoader.dll"));
    void_module *physics = &modules[module_count++];
    strcpy(physics->name, "PhysXLoader.dll");
    physics->kind = MOD_LIFTED_DLL;
    physics->base = 0x1D000000u;
    assert(find_by_name("physxloader.dll") == physics);
    --module_count;
    uint32_t feature = void_gstrdup("UT3MissingFeature");
    assert(!void_module_proc(modules[0].base, feature));
    uint32_t missing = thunk_for("kernel32.dll", "UT3MissingFeature", 0, 1);
    assert(missing && void_dispatch_find(missing));
    assert(thunk_for("kernel32.dll", "UT3MissingFeature", 0, 1) == missing);
    assert(!void_module_proc(modules[0].base, feature));
    assert(void_get_last_error() == ERROR_PROC_NOT_FOUND_);
    void_gfree(feature);
    uint32_t tick = thunk_for("kernel32.dll", "GetTickCount", 0, 1);
    assert(tick && thunk_for("kernel32.dll", "GetTickCount", 0, 0) == tick);
    for (int delayed = 0; delayed <= 1; ++delayed) {
        for (int rva = delayed ? 0 : 1; rva <= 1; ++rva) {
            void_module m = fixture(delayed, rva);
            assert(bind_imports(&m));
            uint32_t bound = MEM32(m.base + 0x600);
            assert(bound && void_dispatch_find(bound));
            assert(!strcmp(void_thunk_name(bound), "GetTickCount"));
            if (delayed) assert(MEM32(m.base + 0x650) == modules[0].base);
            m = fixture(delayed, rva);
            MEM32(m.base + 0x500) = rva ? 0x10000000u : 0x003FFFF0u;
            assert(!bind_imports(&m));
        }
    }
    void_module m = fixture(1, 1);
    MEM32(m.base + 0x300) = 2;
    assert(!bind_imports(&m));
    m = fixture(1, 1);
    MEM32(m.base + 0x304) = 0xFFFFFFF0u;
    assert(!bind_imports(&m));
    m = fixture(1, 1);
    MEM32(m.base + 0x30C) = m.size - 2;
    assert(!bind_imports(&m));
    m = fixture(1, 1);
    MEM32(m.base + 0x80 + 0x7C + 13 * 8) = 32;
    assert(!bind_imports(&m));
    check_section_mapping();
    puts("PE imports/mapping PASS: delay RVA/VA, invalid descriptors, API detection, raw tail, zero-fill, lifted/pseudo collision");
    return 0;
}
