/* Run original-oracle vectors through the production CRT copy bodies and helpers. */
#include "host.h"
#include "fixtures.h"
#include "../android/host/pe.c"
#include <assert.h>

void void_process_exit(uint32_t code, int detach) { (void)detach; _exit((int)code); }

int main(int argc, char **argv)
{
    assert(argc == 3 && void_guest_reserve());
    void_threads_init();
    void_machine_enter();
    void_dispatch_init();
    for (unsigned i = 0; i < sizeof fixtures / sizeof *fixtures; ++i) {
        char path[1024];
        assert(snprintf(path, sizeof path, "%s/%s", argv[1], fixtures[i].file) < (int)sizeof path);
        void_module *module = &modules[module_count++];
        strcpy(module->name, fixtures[i].file);
        snprintf(module->win_path, sizeof module->win_path, "C:\\Windows\\%s", fixtures[i].file);
        module->kind = MOD_LIFTED_DLL;
        assert(map_image(module, path, fixtures[i].base));
        module->state = ST_READY;
    }
    const uint32_t arena = 0x30000000, arena_size = 16384;
    assert(void_vm_claim(arena, arena_size, 4));
    uint32_t stack = void_vm_alloc(0, 0x10000, 0x3000, 4) + 0x8000;
    assert(stack > 0x8000);
    uint8_t original[16384], expected[16384];
    for (unsigned i = 0; i < arena_size; ++i) original[i] = (uint8_t)(i * 73 + (i >> 4) * 17 + 0x35);
    FILE *input = fopen(argv[2], "rb");
    assert(input);
    uint32_t cases;
    assert(fread(&cases, 4, 1, input) == 1);
    for (uint32_t index = 0; index < cases; ++index) {
        uint32_t row[6];
        assert(fread(row, 4, 6, input) == 6);
        uint32_t owner = row[0], flag = row[1], size = row[2], source = 8192 + row[3];
        uint32_t destination = (uint32_t)((int32_t)source + (int32_t)row[4]);
        assert(source + size <= arena_size && destination + size <= arena_size);
        memcpy(expected, original, arena_size);
        memmove(expected + destination, expected + source, size);
        memcpy(GPTR(arena), original, arena_size);
        if (flag) MEM32(flag) = row[5];
        g_esp = stack; g_ebp = 0x71234567; g_ebx = 0xB1234567;
        g_esi = 0x51234567; g_edi = 0x61234567;
        MEM32(stack) = 0xDEAD0000;
        MEM32(stack + 4) = arena + destination;
        MEM32(stack + 8) = arena + source;
        MEM32(stack + 12) = size;
        void_fn function = void_dispatch_find(owner);
        assert(function);
        function();
        if (memcmp(GPTR(arena), expected, arena_size) || g_eax != arena + destination ||
            g_esp != stack + 4 || g_ebp != 0x71234567 || g_ebx != 0xB1234567 ||
            g_esi != 0x51234567 || g_edi != 0x61234567) {
            fprintf(stderr, "CRT copy failure case%u owner%08X size%u align%u delta%d SSE%u\n",
                    index, owner, size, row[3], (int32_t)row[4], row[5]);
            return 1;
        }
    }
    assert(fgetc(input) == EOF);
    fclose(input);
    printf("CRT copy family production PASS: %u original-oracle copies, all owners/tables/SSE paths and callee-saved ABI\n", cases);
    return 0;
}
