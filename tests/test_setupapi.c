/* Production empty-PPU device sets, ABI, object ownership and Windows errors. */
#include "../android/host/setupapi.c"
#include "../android/host/pe.c"
#include <assert.h>
#include <unistd.h>

typedef struct { uint32_t size; uint8_t guid[16]; uint32_t flags, reserved; } interface32;
typedef struct { uint32_t size; uint8_t guid[16]; uint32_t devinst, reserved; } devinfo32;
typedef struct __attribute__((packed)) { uint32_t size; char path[1]; } detail32a;
_Static_assert(sizeof(interface32) == 28, "x86 SP_DEVICE_INTERFACE_DATA");
_Static_assert(sizeof(devinfo32) == 28, "x86 SP_DEVINFO_DATA");
_Static_assert(sizeof(detail32a) == 5, "x86 SP_DEVICE_INTERFACE_DETAIL_DATA_A");

void void_process_exit(uint32_t code, int detach) { (void)detach; _exit((int)code); }

static uint32_t procedure(uint32_t module, const char *name)
{
    uint32_t text = void_gstrdup(name), result = void_module_proc(module, text);
    void_gfree(text);
    assert(result);
    return result;
}

static uint32_t invoke(uint32_t target, unsigned count, const uint32_t *args)
{
    uint32_t before = g_esp;
    void_set_last_error(0x1234);
    uint32_t result = void_call_guest(target, count, args, 1);
    assert(g_esp == before);
    return result;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    assert(void_guest_reserve());
    void_threads_init(); void_dispatch_init(); void_machine_enter(); add_system_pseudos();
    uint32_t module = void_module_load("SETUPAPI.DLL");
    assert(module && void_module_handle("setupapi") == module);
    uint32_t get = procedure(module, "SetupDiGetClassDevsA");
    uint32_t enumerate = procedure(module, "SetupDiEnumDeviceInterfaces");
    uint32_t detail = procedure(module, "SetupDiGetDeviceInterfaceDetailA");
    uint32_t destroy = procedure(module, "SetupDiDestroyDeviceInfoList");
    uint32_t guid = void_galloc(16), output = void_galloc(36), device = void_galloc(28);
    uint32_t required = void_galloc(4), path = void_galloc(13);
    uint32_t get_args[4] = {guid, 0, 0, 0x12};
    /* Independent original-PE/native-oracle input vectors, as little-endian DWORDs. */
    static const uint32_t guids[2][4] = {
        {0x85CFDA8C, 0x4C6F1431, 0x5E1270AC, 0xC66BE86E},
        {0x29D2A384, 0x49B52E47, 0x6269BFAE, 0xC2D72BC2},
    };
    static const uint32_t flags[] = {0, 2, 8, 0x10, 0x11, 0x12, 0x18, 0x1A, 0x20, 0x80000012};
    for (unsigned c = 0; c < 2; ++c) {
        memcpy(GPTR(guid), guids[c], 16);
        for (unsigned f = 0; f < sizeof flags / sizeof *flags; ++f) {
            get_args[3] = flags[f];
            uint32_t set = invoke(get, 4, get_args);
            assert(set && set != 0xFFFFFFFF && !void_get_last_error());
            assert(void_handle_get(set, SETUP_DEVICE_SET_));
            uint32_t enum_args[5] = {set, 0, guid, 0, output + 4};
            static const uint32_t sizes[] = {0, 27, 28, 32};
            for (unsigned s = 0; s < sizeof sizes / sizeof *sizes; ++s) {
                memset(GPTR(output), 0xA5, 36); MEM32(output + 4) = sizes[s];
                uint8_t before[36]; memcpy(before, GPTR(output), 36);
                assert(!invoke(enumerate, 5, enum_args) && void_get_last_error() == 259);
                assert(!memcmp(before, GPTR(output), 36));
            }
            enum_args[2] = enum_args[4] = 0; enum_args[3] = 7;
            assert(!invoke(enumerate, 5, enum_args) && void_get_last_error() == 259);
            enum_args[1] = device; MEM32(device) = sizeof(devinfo32);
            assert(!invoke(enumerate, 5, enum_args) && void_get_last_error() == 87);
            uint32_t detail_args[6] = {set, output + 4, path + 4, 5, required, device};
            memset(GPTR(path), 0xA5, 13); MEM32(path + 4) = sizeof(detail32a);
            MEM32(required) = 0xA5A5A5A5;
            assert(!invoke(detail, 6, detail_args) && void_get_last_error() == 87);
            assert(MEM32(required) == 0xA5A5A5A5 && MEM32(path + 4) == 5);
            assert(MEM32(path) == 0xA5A5A5A5 && MEM32(path + 9) == 0xA5A5A5A5);
            assert(invoke(destroy, 1, &set) && !void_get_last_error());
            assert(!void_handle_get(set, VOID_OBJ_NONE));
            assert(!invoke(destroy, 1, &set) && void_get_last_error() == 6);
            enum_args[1] = 0;
            assert(!invoke(enumerate, 5, enum_args) && void_get_last_error() == 6);
            assert(!invoke(detail, 6, detail_args) && void_get_last_error() == 6);
        }
    }
    get_args[3] = 0x12;
    uint32_t first = invoke(get, 4, get_args), second = invoke(get, 4, get_args);
    assert(first && second && first != second);
    assert(invoke(destroy, 1, &first));
    assert(void_handle_get(second, SETUP_DEVICE_SET_));
    assert(invoke(destroy, 1, &second));
    uint32_t unrelated = void_handle_new(void_object_new(VOID_OBJ_OTHER, 0));
    assert(!invoke(destroy, 1, &unrelated) && void_get_last_error() == 6);
    assert(void_handle_get(unrelated, VOID_OBJ_OTHER));
    assert(void_handle_close(unrelated));
    const uint32_t invalid_handles[] = {0, 0xFFFFFFFF, 0x12345678};
    for (unsigned i = 0; i < 3; ++i)
        assert(!invoke(destroy, 1, &invalid_handles[i]) && void_get_last_error() == 6);
    get_args[3] = 1;
    assert(invoke(get, 4, get_args) == 0xFFFFFFFF && void_get_last_error() == 1004);
    get_args[3] = 0x12; get_args[0] = 0;
    assert(invoke(get, 4, get_args) == 0xFFFFFFFF && void_get_last_error() == 87);
    get_args[0] = 0x1000;
    assert(invoke(get, 4, get_args) == 0xFFFFFFFF && void_get_last_error() == 87);
    get_args[0] = guid; MEM8(guid) ^= 1;
    assert(invoke(get, 4, get_args) == 0xFFFFFFFF && void_get_last_error() == 50);
    MEM8(guid) ^= 1; get_args[3] = 0x16;
    assert(invoke(get, 4, get_args) == 0xFFFFFFFF && void_get_last_error() == 50);
    get_args[3] = 0x12; get_args[1] = void_gstrdup("");
    uint32_t set = invoke(get, 4, get_args);
    assert(set && set != 0xFFFFFFFF && !void_get_last_error());
    assert(invoke(destroy, 1, &set));
    void_gfree(get_args[1]); get_args[1] = void_gstrdup("PCI");
    assert(invoke(get, 4, get_args) == 0xFFFFFFFF && void_get_last_error() == 50);
    void_gfree(get_args[1]); get_args[1] = 0x1000;
    assert(invoke(get, 4, get_args) == 0xFFFFFFFF && void_get_last_error() == 87);
    void_gfree(guid); void_gfree(output); void_gfree(device);
    void_gfree(required); void_gfree(path);
    printf("PASS SetupAPI no-PPU empty sets: both PhysX GUIDs, native flags/errors, 28/28/5-byte guest layouts, stdcall, guards and handle ownership\n");
    return 0;
}
