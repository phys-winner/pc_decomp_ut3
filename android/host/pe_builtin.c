/* The VC80 CRT the game's DLLs import (msvcr80.dll and msvcp80.dll, WinSxS 8.0.50727.9680) is no file of
 * the game data, so their prepared images (generated/input, found through the builder's -I<generated>)
 * are linked into the library. pe_images.c checks them like every other image: a stale object of this
 * file, whose cache key does not cover the included bytes, fails that check. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define LINKED(symbol, file) \
    ".globl " #symbol "\n.hidden " #symbol "\n.balign 16\n" #symbol ":\n.incbin \"" file "\"\n" \
    ".globl " #symbol "_end\n.hidden " #symbol "_end\n" #symbol "_end:\n"
__asm__(".pushsection .rodata.ut3_crt80,\"a\"\n"
        LINKED(ut3_msvcr80, "input/msvcr80.dll")
        LINKED(ut3_msvcp80, "input/msvcp80.dll")
        ".popsection\n");
extern __attribute__((visibility("hidden"))) const uint8_t ut3_msvcr80[], ut3_msvcr80_end[], ut3_msvcp80[], ut3_msvcp80_end[];

/* The linked image of a CRT DLL (modules.inc's file name); NULL for any other. */
const uint8_t *void_pe_builtin(const char *file, size_t *size)
{
    if (!strcmp(file, "msvcr80.dll")) { *size = (size_t)(ut3_msvcr80_end - ut3_msvcr80); return ut3_msvcr80; }
    if (!strcmp(file, "msvcp80.dll")) { *size = (size_t)(ut3_msvcp80_end - ut3_msvcp80); return ut3_msvcp80; }
    return NULL;
}
