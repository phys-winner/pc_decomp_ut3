/* Execute actual UE3 compressed blocks in the production lifted LZO owner. */
#include "host.h"
#include "functions.h"
#include "../android/host/pe.c"
#include <assert.h>

void void_process_exit(uint32_t code, int detach) { (void)detach; _exit((int)code); }

int main(int argc, char **argv)
{
    assert(argc == 3 && void_guest_reserve());
    void_threads_init(); void_machine_enter(); void_dispatch_init();
    void_module *module = &modules[module_count++];
    strcpy(module->name, "ut3.exe"); strcpy(module->win_path, "C:\\UT3\\Binaries\\UT3.exe");
    module->kind = MOD_LIFTED_EXE;
    assert(map_image(module, argv[1], 0x00400000)); module->state = ST_READY;
    const uint32_t input = 0x30000000, output = 0x31000000, length = 0x32000000, region = 0x40000;
    assert(void_vm_claim(input, region, 4) && void_vm_claim(output, region, 4) && void_vm_claim(length, 4096, 4));
    uint32_t stack_base = void_vm_alloc(0, 0x10000, 0x3000, 4);
    assert(stack_base);
    uint32_t stack = stack_base + 0x8000;
    uint8_t source[0x40000], expected[0x40000];
    FILE *file = fopen(argv[2], "rb"); assert(file);
    uint32_t cases; assert(fread(&cases, 4, 1, file) == 1);
    for (uint32_t index = 0; index < cases; ++index) {
        uint32_t row[6]; assert(fread(row, 4, 6, file) == 6);
        uint32_t size = row[0], capacity = row[1], source_offset = 64 + row[2], destination_offset = 64 + row[3];
        assert(source_offset + size < region && destination_offset + capacity < region);
        memset(source, 0xCC, region);
        assert(fread(source + source_offset, 1, size, file) == size);
        assert(fread(expected, 1, region, file) == region);
        memcpy(GPTR(input), source, region); memset(GPTR(output), 0xA5, region); memset(GPTR(length), 0xA5, 16);
        MEM32(length + 4) = capacity;
        g_esp = stack; g_ebp = 0x71234567; g_ebx = 0xB1234567; g_esi = 0x51234567; g_edi = 0x61234567;
        MEM32(stack) = 0xDEAD0000; MEM32(stack + 4) = input + source_offset;
        MEM32(stack + 8) = size; MEM32(stack + 12) = output + destination_offset;
        MEM32(stack + 16) = length + 4; MEM32(stack + 20) = 0;
        sub_01579230();
        if (g_eax != row[4] || MEM32(length + 4) != row[5] || memcmp(GPTR(output), expected, region) ||
            memcmp(GPTR(input), source, region) || MEM32(length) != 0xA5A5A5A5 ||
            MEM32(length + 8) != 0xA5A5A5A5 || MEM32(length + 12) != 0xA5A5A5A5 ||
            g_esp != stack + 4 || g_ebp != 0x71234567 || g_ebx != 0xB1234567 ||
            g_esi != 0x51234567 || g_edi != 0x61234567) {
            fprintf(stderr, "LZO failure case%u input%u capacity%u align%u/%u result%d/%d output%u/%u\n",
                    index, size, capacity, row[2], row[3], (int32_t)g_eax, (int32_t)row[4], MEM32(length + 4), row[5]);
            return 1;
        }
    }
    assert(fgetc(file) == EOF); fclose(file);
    printf("LZO CFG production PASS: %u original-oracle cases, compressed game blocks/error codes/output length/guards/ABI\n", cases);
    return 0;
}
