/* Execute the production exports, including their x86 stack cleanup. */
#include "../android/host/crt_compat.c"
#include <assert.h>
#include <sys/mman.h>

uint32_t g_esp, g_eax;
static uint32_t last_error;
void void_set_last_error(uint32_t value) { last_error = value; }

int main(void)
{
    assert(mmap((void *)(VOID_GUEST_BASE + 0x10000), 0x10000, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0) != MAP_FAILED);
    g_esp = 0x11000;
    _getdrives_entry();
    assert(g_eax == 4 && g_esp == 0x11004);
    const uint32_t supported[] = {0, 1, 3, 437, 1251, 1252, 20127, 28591, 65001};
    for (unsigned cp = 0; cp < sizeof supported / sizeof *supported; ++cp) {
        for (unsigned byte = 0; byte < 256; ++byte) {
            g_esp = 0x11000;
            MEM32(g_esp + 4) = supported[cp];
            MEM32(g_esp + 8) = byte;
            last_error = 123;
            IsDBCSLeadByteEx_entry();
            assert(!g_eax && g_esp == 0x1100C && last_error == 123);
        }
    }
    const uint32_t unsupported[] = {2, 932, 936, 949, 950, 1361, 0xFFFFFFFF};
    for (unsigned cp = 0; cp < sizeof unsupported / sizeof *unsupported; ++cp) {
        g_esp = 0x11000;
        MEM32(g_esp + 4) = unsupported[cp];
        IsDBCSLeadByteEx_entry();
        assert(!g_eax && g_esp == 0x1100C && last_error == ERROR_INVALID_PARAMETER_);
    }
    puts("CRT compatibility PASS: C: drive mask, 2304 valid code-page/byte pairs, unsupported pages and ABI cleanup");
    return 0;
}
