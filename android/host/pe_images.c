/* The PE images the lifted code was generated from (generated/input) are made here at load time.
 * UT3.exe and the game's own DLLs come from the game data (Binaries, through files.c: the folder or the
 * stored ZIP the game plays from) and move to the port's private bases as tools/pe_rebase.py prepared
 * them: HIGHLOW relocations plus the ImageBase field, nothing else. The VC80 CRT is no file of the game;
 * its two prepared images are linked into the library (pe_builtin.c). Every image must then have the
 * prepared one's size and CRC-32, so another game version or a damaged copy stops the start here
 * instead of running the lifted code over the wrong data. */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

void void_log(const char *fmt, ...);
uint8_t *void_files_read_game(const char *relative, size_t *size);   /* files.c */
const uint8_t *void_pe_builtin(const char *file, size_t *size);       /* pe_builtin.c */

/* modules.inc's files: the source below the installation (NULL: linked in), the prepared size and CRC-32. */
static const struct { const char *file, *path; uint32_t size, crc; } images[] = {
    {"UT3.exe", "Binaries/UT3.exe", 28505368u, 0x93B95261u},
    {"msvcr80.dll", NULL, 626688u, 0x34F8A54Du},
    {"msvcp80.dll", NULL, 548864u, 0x6AE0AC26u},
    {"wxmsw262u.dll", "Binaries/wxmsw262u.dll", 3824720u, 0x7BF4C379u},
    {"wxdockitu.dll", "Binaries/wxdockitu.dll", 199760u, 0xB08FA142u},
    {"FxGraphLayout.dll", "Binaries/FxGraphLayout.dll", 556112u, 0x02C8E525u},
    {"libresample.dll", "Binaries/libresample.dll", 68688u, 0x079A98ABu},
    {"ogg.dll", "Binaries/ogg.dll", 18512u, 0xE8A2F952u},
    {"vorbis.dll", "Binaries/vorbis.dll", 318544u, 0x932C2599u},
    {"vorbisenc.dll", "Binaries/vorbisenc.dll", 986192u, 0x90E54983u},
    {"vorbisfile.dll", "Binaries/vorbisfile.dll", 23632u, 0x359859CDu},
    {"binkw32.dll", "Binaries/binkw32.dll", 171088u, 0xAE938D12u},
    {"NxCooking.dll", "Binaries/NxCooking.dll", 388176u, 0x5CA34FC6u},
    {"NxCharacter.dll", "Binaries/NxCharacter.dll", 121936u, 0x49660262u},
    {"PhysXLoader.dll", "Binaries/PhysXLocal/PhysXLoader.dll", 68688u, 0x3C0B256Eu},
    {"PhysXCore.dll", "Binaries/PhysXCore.dll", 2817104u, 0xD2DA4AC4u},
    {"steam_api.dll", "Binaries/steam_api.dll", 87288u, 0xA7FC8646u},
    {"steamclient.dll", "Binaries/steamclient.dll", 2968048u, 0x0EFF1CA3u},
    {"tier0_s.dll", "Binaries/tier0_s.dll", 263416u, 0x9A14F1A3u},
    {"vstdlib_s.dll", "Binaries/vstdlib_s.dll", 369912u, 0xF9BC8411u},
};

static uint16_t rd16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }
static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }

/* The file offset of [rva, rva + length) when one section's raw data holds all of it; 0 if none does. */
static uint32_t raw_offset(const uint8_t *file, size_t size, uint32_t table, uint32_t sections,
                           uint32_t rva, uint32_t length)
{
    for (uint32_t i = 0; i < sections; ++i) {
        const uint8_t *s = file + table + 40u * i;
        uint32_t va = rd32(s + 12), raw_size = rd32(s + 16), raw = rd32(s + 20);
        if (rva >= va && (uint64_t)rva + length <= (uint64_t)va + raw_size && (uint64_t)raw + raw_size <= size)
            return raw + (rva - va);
    }
    return 0;
}

/* pe_rebase.rebase_image in place; 0 if the file cannot be relocated that way. */
static int rebase(uint8_t *file, size_t size, uint32_t base)
{
    uint32_t pe = size >= 0x40 ? rd32(file + 0x3C) : 0;
    if (!pe || (uint64_t)pe + 0xF8 > size || memcmp(file + pe, "PE\0\0", 4) || rd16(file + pe + 4) != 0x14C ||
        rd16(file + pe + 0x18) != 0x10B) return 0;
    uint32_t delta = base - rd32(file + pe + 0x34);
    if (!delta) return 1;
    uint32_t sections = rd16(file + pe + 6), table = pe + 0x18 + rd16(file + pe + 0x14);
    if ((uint64_t)table + 40u * sections > size) return 0;
    uint32_t length = rd32(file + pe + 0xA4);
    uint32_t at = raw_offset(file, size, table, sections, rd32(file + pe + 0xA0), length);
    if (!length || !at) return 0;
    for (uint32_t end = at + length; end - at >= 8;) {
        uint32_t page = rd32(file + at), block = rd32(file + at + 4);
        if (!block) break;                          /* pefile stops at an empty block */
        if (block < 8 || block > end - at) return 0;
        for (uint32_t k = at + 8; k + 2 <= at + block; k += 2) {
            uint16_t entry = rd16(file + k);
            if (!(entry >> 12)) continue;           /* IMAGE_REL_BASED_ABSOLUTE: padding */
            /* Only IMAGE_REL_BASED_HIGHLOW, on four bytes of raw section data. */
            uint32_t site = entry >> 12 == 3 ? raw_offset(file, size, table, sections, page + (entry & 0xFFFu), 4) : 0;
            if (!site) return 0;
            uint32_t value = rd32(file + site) + delta;
            memcpy(file + site, &value, 4);
        }
        at += block;
    }
    memcpy(file + pe + 0x34, &base, 4);
    return 1;
}

/* The prepared image of a lifted module (modules.inc's file name) at its private base: a malloc'd copy of
 * *size bytes, or NULL with errno ENOENT (no such file) or EILSEQ (not the file the port was built from). */
uint8_t *void_pe_image(const char *file, uint32_t base, size_t *size)
{
    size_t i = 0, count = sizeof images / sizeof images[0];
    while (i < count && strcmp(images[i].file, file)) ++i;
    if (i == count) { void_log("[void] no source for image %s", file); errno = ENOENT; return NULL; }
    const char *path = images[i].path ? images[i].path : "the library";
    size_t n = 0;
    uint8_t *data = NULL;
    if (images[i].path) {
        data = void_files_read_game(images[i].path, &n);
    } else {
        const uint8_t *linked = void_pe_builtin(file, &n);
        errno = ENOENT;
        if (linked && (data = (uint8_t *)malloc(n ? n : 1))) memcpy(data, linked, n);
    }
    if (!data) {
        int error = errno;
        void_log("[void] cannot read image %s from %s: %s", file, path, strerror(error));
        errno = error;
        return NULL;
    }
    uint32_t crc = 0;
    if (n != images[i].size || (images[i].path && !rebase(data, n, base)) ||
        (crc = (uint32_t)crc32(0, data, (uInt)n)) != images[i].crc) {
        void_log("[void] %s from %s is not the file the port was built from: %zu bytes, CRC-32 %08X "
                 "(expected %u bytes, %08X)", file, path, n, crc, images[i].size, images[i].crc);
        free(data);
        errno = EILSEQ;
        return NULL;
    }
    *size = n;
    return data;
}
