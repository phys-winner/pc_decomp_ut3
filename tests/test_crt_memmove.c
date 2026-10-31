/* Execute the production lifted CRT owner; oracle vectors cover overlapping copies. */
#include "host.h"
#include "functions.h"
#include "../android/host/pe.c"
#include <assert.h>

void void_process_exit(uint32_t code, int detach) { (void)detach; _exit((int)code); }

int main(int argc, char **argv)
{
    assert(argc == 3 && void_guest_reserve());
    void_threads_init();
    void_machine_enter();
    void_dispatch_init();
    void_module *module = &modules[module_count++];
    strcpy(module->name, "msvcr80.dll"); strcpy(module->win_path, "C:\\Windows\\msvcr80.dll");
    module->kind = MOD_LIFTED_DLL;
    assert(map_image(module, argv[1], 0x10000000));
    module->state = ST_READY;
    const uint32_t arena = 0x22000000, arena_size = 8192;
    assert(void_vm_claim(arena, arena_size, 4));
    uint32_t stack = void_vm_alloc(0, 0x10000, 0x3000, 4) + 0x8000;
    assert(stack > 0x8000);
    uint8_t original[8192], expected[8192];
    for (unsigned i = 0; i < arena_size; ++i) original[i] = (uint8_t)(i * 73 + (i >> 4) * 17 + 0x35);
    FILE *input = fopen(argv[2], "rb");
    assert(input);
    uint32_t cases;
    assert(fread(&cases, 4, 1, input) == 1);
    for (uint32_t index = 0; index < cases; ++index) {
        uint32_t row[4];
        assert(fread(row, 4, 4, input) == 4);
        uint32_t size = row[0], source_offset = 4096 + row[1];
        uint32_t destination_offset = (uint32_t)((int32_t)source_offset + (int32_t)row[2]);
        assert(source_offset + size <= arena_size && destination_offset + size <= arena_size);
        memcpy(expected, original, arena_size);
        memmove(expected + destination_offset, expected + source_offset, size);
        memcpy(GPTR(arena), original, arena_size);
        MEM32(0x10095948) = row[3]; /* The CRT's detected SSE2 capability. */
        g_esp = stack; g_ebp = 0x71234567; g_ebx = 0xB1234567;
        g_esi = 0x51234567; g_edi = 0x61234567;
        MEM32(stack) = 0xDEAD0000;
        MEM32(stack + 4) = arena + destination_offset;
        MEM32(stack + 8) = arena + source_offset;
        MEM32(stack + 12) = size;
        sub_10015320();
        if (memcmp(GPTR(arena), expected, arena_size) || g_eax != arena + destination_offset ||
            g_esp != stack + 4 || g_ebp != 0x71234567 || g_ebx != 0xB1234567 ||
            g_esi != 0x51234567 || g_edi != 0x61234567) {
            fprintf(stderr, "memmove failure case%u size%u align%u delta%d SSE%u\n",
                    index, size, row[1], (int32_t)row[2], row[3]);
            return 1;
        }
    }
    assert(fgetc(input) == EOF);
    fclose(input);
    printf("CRT memmove production PASS: %u original-oracle cases, overlap/alignment/tails/SSE paths and callee-saved ABI\n", cases);
    return 0;
}
