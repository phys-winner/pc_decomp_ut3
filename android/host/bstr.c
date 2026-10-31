/* BSTR layout reused from the local Sims2 host, with guest bounds/heap validation. */
#include "host.h"

static uint32_t bstr_owned_block(uint32_t string)
{
    if (string < 4) return 0;
    uint32_t block = string - 4, size = void_heap_size(void_process_heap(), block);
    if (size == UINT32_MAX || size < 6 || !void_vm_readable(block, size)) return 0;
    return MEM32(block) <= size - 6 ? block : 0;
}

WINAPI_EXPORT_AS("oleaut32.dll", SysAllocString, "SysAllocString", 2, 1)
{
    uint32_t source = ARG(0), bytes = 0;
    if (!source) return 0;
    for (;;) {
        uint64_t address = (uint64_t)source + bytes;
        if (address > UINT32_MAX - 1u || !void_vm_readable((uint32_t)address, 2))
            void_lift_trap(source, "invalid SysAllocString source");
        if (!MEM16((uint32_t)address)) break;
        if (bytes > UINT32_MAX - 8u) return 0;
        bytes += 2;
    }
    uint32_t block = void_galloc(bytes + 6);
    if (!block) return 0;
    MEM32(block) = bytes;
    memcpy(GPTR(block + 4), GPTR(source), bytes);
    MEM16(block + 4 + bytes) = 0;
    return block + 4;
}

WINAPI_EXPORT_AS("oleaut32.dll", SysFreeString, "SysFreeString", 6, 1)
{
    uint32_t string = ARG(0);
    if (!string) return 0;
    uint32_t block = bstr_owned_block(string);
    if (!block || !void_heap_free(void_process_heap(), block))
        void_lift_trap(string, "invalid or released BSTR allocation");
    return 0;
}
