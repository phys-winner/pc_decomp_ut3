/* The production resource APIs over the real UT3 image and bounded malformed fixtures. */
#include "../android/host/pe_resources.c"
#include "../android/host/pe.c"
#include <assert.h>

void void_process_exit(uint32_t code, int detach) { (void)detach; _exit((int)code); }
static uint32_t stack;

static uint32_t api(const char *name, unsigned count, const uint32_t *args)
{
    const void_shim *shim = void_shim_find("kernel32.dll", name, 0);
    assert(shim);
    g_esp = stack;
    for (unsigned i = 0; i < count; ++i) MEM32(stack + 4 + 4 * i) = args[i];
    shim->fn();
    assert(g_esp == stack + 4 + 4 * count);
    return g_eax;
}

static uint32_t find(uint32_t module, uint32_t name, uint32_t type, int wide)
{
    uint32_t args[] = {module, name, type};
    return api(wide ? "FindResourceW" : "FindResourceA", 3, args);
}

static uint32_t wide(const char *text)
{
    uint32_t units = void_utf8_to_gw(text, 0, 0), result = void_galloc(2 * units);
    assert(result);
    void_utf8_to_gw(text, result, units);
    return result;
}

static uint32_t fixture(void)
{
    uint32_t base = 0x21000000;
    assert(void_vm_claim(base, 0x2000, 4));
    void_module *m = &modules[module_count++];
    strcpy(m->win_path, "C:\\Games\\fixture.dll");
    m->base = base; m->size = 0x2000; m->kind = MOD_LIFTED_DLL; m->state = ST_READY;
    MEM16(base) = 0x5A4D; MEM32(base + 0x3C) = 0x80;
    uint32_t nt = base + 0x80;
    MEM32(nt) = 0x4550; MEM16(nt + 0x14) = 0xE0; MEM16(nt + 0x18) = 0x10B;
    MEM32(nt + 0x50) = 0x2000; MEM32(nt + 0x74) = 16;
    MEM32(nt + 0x88) = 0x200; MEM32(nt + 0x8C) = 0x400;
    uint32_t root = base + 0x200;
    MEM16(root + 12) = 1; MEM16(root + 14) = 1;
    MEM32(root + 16) = 0x80000100; MEM32(root + 20) = 0x80000030;
    MEM32(root + 24) = 10; MEM32(root + 28) = 0x80000030;
    MEM16(root + 0x30 + 12) = 1; MEM16(root + 0x30 + 14) = 1;
    MEM32(root + 0x40) = 0x80000120; MEM32(root + 0x44) = 0x80000060;
    MEM32(root + 0x48) = 1010; MEM32(root + 0x4C) = 0x80000060;
    MEM16(root + 0x60 + 14) = 2;
    MEM32(root + 0x70) = 1033; MEM32(root + 0x74) = 0x90;
    MEM32(root + 0x78) = 1049; MEM32(root + 0x7C) = 0xA0;
    MEM32(root + 0x90) = 0x800; MEM32(root + 0x94) = 7;
    MEM32(root + 0xA0) = 0x810; MEM32(root + 0xA4) = 7;
    MEM16(root + 0x100) = 3;
    MEM16(root + 0x102) = 0x0422; MEM16(root + 0x104) = 0x0418; MEM16(root + 0x106) = 0x041F;
    MEM16(root + 0x120) = 4;
    for (unsigned i = 0; i < 4; ++i) MEM16(root + 0x122 + 2 * i) = "Name"[i];
    memcpy(GPTR(base + 0x800), "ENdata", 7); memcpy(GPTR(base + 0x810), "RUdata", 7);
    return base;
}

int main(int argc, char **argv)
{
    assert(argc == 3 && void_guest_reserve());
    void_threads_init();
    void_machine_enter();
    stack = void_galloc(64);
    void_module *m = &modules[module_count++];
    strcpy(m->name, "UT3.exe"); strcpy(m->win_path, "C:\\Games\\UT3\\Binaries\\UT3.exe");
    m->kind = MOD_LIFTED_EXE;
    assert(map_image(m, argv[1], 0x00400000));
    exe_base = m->base;
    uint32_t entry = find(0, 1010, 10, 1), size = 0;
    assert(entry && find(exe_base, 1010, 10, 1) == entry);
    uint32_t args[] = {0, entry};
    assert(api("SizeofResource", 2, args) == 4728);
    uint32_t data = api("LoadResource", 2, args);
    assert(data == 0x01E3D600 && api("LockResource", 1, &data) == data);
    assert(void_resource_find(0, 10, 1010, &size) == data && size == 4728);
    FILE *reference = fopen(argv[2], "rb");
    assert(reference);
    for (unsigned i = 0; i < 4728; ++i) assert(fgetc(reference) == MEM8(data + i));
    assert(fgetc(reference) == EOF); fclose(reference);
    assert(find(0, wide("#1010"), wide("#10"), 1) == entry);
    assert(find(0, void_gstrdup("#1010"), void_gstrdup("#10"), 0) == entry);
    assert(!find(0, 1010, 65535, 1) && void_get_last_error() == RESOURCE_TYPE_NOT_FOUND);
    assert(!find(0, 65535, 10, 1) && void_get_last_error() == RESOURCE_NAME_NOT_FOUND);
    assert(!find(0x12345678, 1010, 10, 1) && void_get_last_error() == ERROR_INVALID_HANDLE_);
    uint32_t base = fixture(), root = base + 0x200;
    entry = find(base, wide("nAmE"), wide("тип"), 1);
    assert(entry == root + 0xA0);
    assert(find(base, void_gstrdup("name"), 10, 0) == entry);
    args[0] = base; args[1] = entry;
    assert(api("LoadResource", 2, args) == base + 0x810 && api("SizeofResource", 2, args) == 7);
    args[0] = exe_base;
    assert(!api("LoadResource", 2, args)); /* Descriptor belongs to another image. */
    MEM16(root + 0x60 + 14) = 3;
    MEM32(root + 0x80) = 0; MEM32(root + 0x84) = 0xB0;
    MEM32(root + 0xB0) = 0x820; MEM32(root + 0xB4) = 7;
    assert(find(base, 1010, 10, 1) == root + 0xB0); /* Neutral language precedes host locale. */
    MEM16(root + 0x60 + 14) = 2;
    assert(!find(base, wide("#no"), 10, 1) && void_get_last_error() == ERROR_INVALID_PARAMETER_);
    const uint32_t bad_offsets[] = {20, 0x44, 0x7C};
    for (unsigned i = 0; i < 3; ++i) {
        uint32_t old = MEM32(root + bad_offsets[i]);
        MEM32(root + bad_offsets[i]) = 0xFFFFFFF0;
        assert(!find(base, wide("Name"), wide("тип"), 1));
        MEM32(root + bad_offsets[i]) = old;
    }
    MEM16(root + 0x60 + 14) = 0;
    assert(!find(base, 1010, 10, 1) && void_get_last_error() == RESOURCE_LANG_NOT_FOUND);
    MEM16(root + 0x60 + 14) = 2;
    MEM32(root + 0xA0) = 0x1FFF;
    assert(!find(base, 1010, 10, 1) && void_get_last_error() == RESOURCE_DATA_NOT_FOUND);
    MEM32(root + 0xA0) = 0x810;
    MEM16(root + 14) = 65535;
    assert(!find(base, 1010, 10, 1) && void_get_last_error() == RESOURCE_DATA_NOT_FOUND);
    MEM16(root + 14) = 1;
    MEM32(base + 0x3C) = UINT32_MAX;
    assert(!find(base, 1010, 10, 1));
    MEM32(base + 0x3C) = 0x80;
    MEM32(base + 0x80 + 0x88) = 0xFFFFFF00;
    assert(!find(base, 1010, 10, 1) && void_get_last_error() == RESOURCE_DATA_NOT_FOUND);
    MEM32(base + 0x80 + 0x88) = 0x200;
    args[0] = base; args[1] = 0;
    assert(!api("SizeofResource", 2, args));
    uint32_t bad_lock = UINT32_MAX;
    assert(!api("LockResource", 1, &bad_lock));
    /* Existing LoadString callers now use the same resource walker. */
    MEM32(root + 24) = 6; MEM32(root + 0x48) = 1;
    MEM32(root + 0xA4) = 36;
    memset(GPTR(base + 0x810), 0, 36);
    MEM16(base + 0x810) = 2; MEM16(base + 0x812) = 'H'; MEM16(base + 0x814) = 'i';
    uint32_t buffer = void_galloc(32);
    const void_shim *load = void_shim_find("user32.dll", "LoadStringW", 0);
    assert(load);
    g_esp = stack; MEM32(stack + 4) = base; MEM32(stack + 8) = 0;
    MEM32(stack + 12) = buffer; MEM32(stack + 16) = 16;
    load->fn();
    assert(g_eax == 2 && g_esp == stack + 20 && MEM16(buffer) == 'H' && MEM16(buffer + 2) == 'i');
    load = void_shim_find("user32.dll", "LoadStringA", 0);
    assert(load);
    g_esp = stack;
    load->fn();
    assert(g_eax == 2 && g_esp == stack + 20 && MEM8(buffer) == 'H' && MEM8(buffer + 1) == 'i' && !MEM8(buffer + 2));
    puts("PE resources production PASS: original UT3 4728 bytes, ID/#ID/A/W/Unicode names, language, descriptors, malformed bounds, module ownership, LoadString and stdcall");
    return 0;
}
