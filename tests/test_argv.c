/* Windows-native oracle vectors exercise the actual export and LocalFree ABI. */
#include "../android/host/argv.c"
#include "../android/host/pe.c"
#include <assert.h>

void void_process_exit(uint32_t code, int detach) { (void)detach; _exit((int)code); }

static uint32_t read_u32(FILE *in)
{
    uint32_t value;
    assert(fread(&value, sizeof value, 1, in) == 1);
    return value;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    assert(void_guest_reserve());
    void_threads_init();
    void_machine_enter();
    module_count = 1;
    modules[0].base = exe_base = 0x00400000;
    modules[0].kind = MOD_LIFTED_EXE;
    strcpy(modules[0].win_path, "C:\\Games\\UT3\\Binaries\\UT3.exe");
    uint32_t stack = void_galloc(64), count = stack + 32;
    const void_shim *release = void_shim_find("kernel32.dll", "LocalFree", 0);
    assert(stack && release);
    FILE *in = fopen(argv[1], "rb");
    assert(in);
    uint32_t cases = read_u32(in);
    for (uint32_t index = 0; index < cases; ++index) {
        uint32_t units = read_u32(in), command = void_galloc(2 * (units + 1));
        assert(command && fread(GPTR(command), 2, units, in) == units);
        uint32_t expected_count = read_u32(in);
        g_esp = stack;
        MEM32(stack + 4) = command;
        MEM32(stack + 8) = count;
        CommandLineToArgvW_entry();
        uint32_t block = g_eax, size = void_heap_size(void_process_heap(), block);
        assert(block && g_esp == stack + 12 && MEM32(count) == expected_count);
        assert(!MEM32(block + 4 * expected_count));
        for (uint32_t argument = 0; argument < expected_count; ++argument) {
            uint32_t length = read_u32(in), text = MEM32(block + 4 * argument);
            assert(text >= block && text < block + size && 2ull * (length + 1) <= block + size - text);
            for (uint32_t i = 0; i < length; ++i) {
                uint16_t expected;
                assert(fread(&expected, 2, 1, in) == 1);
                if (MEM16(text + 2 * i) != expected) {
                    fprintf(stderr, "argv mismatch case %u arg %u unit %u\n", index, argument, i);
                    return 1;
                }
            }
            assert(!MEM16(text + 2 * length));
        }
        g_esp = stack;
        MEM32(stack + 4) = block;
        release->fn();
        assert(!g_eax && g_esp == stack + 8 && void_heap_size(void_process_heap(), block) == UINT32_MAX);
        void_gfree(command);
    }
    assert(fgetc(in) == EOF);
    fclose(in);
    g_esp = stack;
    MEM32(stack + 4) = 0;
    MEM32(stack + 8) = count;
    CommandLineToArgvW_entry();
    assert(!g_eax && void_get_last_error() == ERROR_INVALID_PARAMETER_);
    g_esp = stack;
    MEM32(stack + 4) = UINT32_MAX;
    CommandLineToArgvW_entry();
    assert(!g_eax && void_get_last_error() == ERROR_INVALID_PARAMETER_);
    printf("CommandLineToArgvW production PASS: %u Windows oracle vectors, UTF-16 pointers, ABI and LocalFree\n", cases);
    return 0;
}
