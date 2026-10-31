/* Shared PE resource lookup, adapted from the existing host's type/name/language walk. */
#include "host.h"
#include <wctype.h>

enum { RESOURCE_DATA_NOT_FOUND = 1812, RESOURCE_TYPE_NOT_FOUND = 1813,
       RESOURCE_NAME_NOT_FOUND = 1814, RESOURCE_LANG_NOT_FOUND = 1815 };

typedef struct { uint32_t base, image_size, root, size; } resource_image;
typedef struct { uint32_t value, length; int named, wide; } resource_key;

static int resource_range(const resource_image *image, uint32_t offset, uint32_t size)
{
    return offset <= image->size && size <= image->size - offset &&
           void_vm_readable(image->root + offset, size);
}

static int resource_image_open(uint32_t module, resource_image *image)
{
    uint32_t base = module ? module : void_module_exe_base();
    if (!base || !void_module_path(base)) { void_set_last_error(ERROR_INVALID_HANDLE_); return 0; }
    if (!void_vm_readable(base, 64) || MEM16(base) != 0x5A4D) goto invalid;
    uint32_t nt_offset = MEM32(base + 0x3C);
    if ((uint64_t)base + nt_offset + 0x90 > 0x100000000ull ||
        !void_vm_readable(base + nt_offset, 0x90)) goto invalid;
    uint32_t nt = base + nt_offset, size = MEM32(nt + 0x50);
    if (MEM32(nt) != 0x4550 || MEM16(nt + 0x18) != 0x10B || MEM16(nt + 0x14) < 0x78 ||
        MEM32(nt + 0x74) < 3 || size < 0x90 || nt_offset > size - 0x90 ||
        (uint64_t)base + size > 0x100000000ull) goto invalid;
    uint32_t rva = MEM32(nt + 0x88), span = MEM32(nt + 0x8C);
    if (!rva || span < 16 || rva > size || span > size - rva ||
        !void_vm_readable(base + rva, span)) goto invalid;
    *image = (resource_image){base, size, base + rva, span};
    return 1;
invalid:
    void_set_last_error(RESOURCE_DATA_NOT_FOUND);
    return 0;
}

static uint16_t resource_character(uint32_t address, uint32_t index, int wide)
{
    return wide ? MEM16(address + 2u * index) : void_ansi_to_u16(MEM8(address + index));
}

static int resource_key_read(uint32_t value, int wide, resource_key *key)
{
    *key = (resource_key){value, 0, value >= 0x10000u, wide};
    if (!key->named) return 1;
    uint32_t width = wide ? 2 : 1;
    for (;;) {
        uint64_t address = (uint64_t)value + (uint64_t)width * key->length;
        if (key->length > 65535 || address + width > 0x100000000ull ||
            !void_vm_readable((uint32_t)address, width)) goto invalid;
        if (!resource_character(value, key->length, wide)) break;
        ++key->length;
    }
    if (key->length && resource_character(value, 0, wide) == '#') {
        uint32_t id = 0;
        if (key->length == 1) goto invalid;
        for (uint32_t i = 1; i < key->length; ++i) {
            uint16_t c = resource_character(value, i, wide);
            if (c < '0' || c > '9' || id > (65535u - (c - '0')) / 10u) goto invalid;
            id = id * 10u + c - '0';
        }
        key->named = 0;
        key->value = id;
    }
    return 1;
invalid:
    void_set_last_error(ERROR_INVALID_PARAMETER_);
    return 0;
}

static int resource_matches(const resource_image *image, uint32_t name, const resource_key *key)
{
    if (!(name & 0x80000000u)) return !key->named && name == key->value;
    uint32_t offset = name & 0x7FFFFFFFu;
    if (!resource_range(image, offset, 2)) return -1;
    uint32_t length = MEM16(image->root + offset);
    if (!resource_range(image, offset + 2, 2u * length)) return -1;
    if (!key->named || length != key->length) return 0;
    for (uint32_t i = 0; i < length; ++i)
        if (towupper(MEM16(image->root + offset + 2 + 2 * i)) !=
            towupper(resource_character(key->value, i, key->wide))) return 0;
    return 1;
}

static int resource_data(const resource_image *image, uint32_t entry, uint32_t *data, uint32_t *size)
{
    if (entry < image->root || !resource_range(image, entry - image->root, 16)) goto invalid;
    uint32_t rva = MEM32(entry), length = MEM32(entry + 4);
    if (rva >= image->image_size || length > image->image_size - rva || MEM32(entry + 12) ||
        !void_vm_readable(image->base + rva, length ? length : 1)) goto invalid;
    if (data) *data = image->base + rva;
    if (size) *size = length;
    return 1;
invalid:
    void_set_last_error(RESOURCE_DATA_NOT_FOUND);
    return 0;
}

static uint32_t resource_entry(uint32_t module, uint32_t type, uint32_t name, int wide)
{
    resource_image image;
    resource_key keys[2];
    if (!resource_image_open(module, &image) || !resource_key_read(type, wide, &keys[0]) ||
        !resource_key_read(name, wide, &keys[1])) return 0;
    uint32_t directory = 0;
    for (unsigned level = 0; level < 3; ++level) {
        if (!resource_range(&image, directory, 16)) goto invalid;
        uint32_t count = MEM16(image.root + directory + 12) + MEM16(image.root + directory + 14);
        if (!resource_range(&image, directory + 16, count * 8u)) goto invalid;
        uint32_t selected = 0;
        int found = 0, preference = -1;
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t entry = image.root + directory + 16 + 8 * i;
            uint32_t id = MEM32(entry), target = MEM32(entry + 4);
            int matches = level < 2 ? resource_matches(&image, id, &keys[level]) : 1;
            if (matches < 0) goto invalid;
            if (!matches) continue;
            if (level < 2) { selected = target; found = 1; break; }
            if (id & 0xFFFF0000u) goto invalid;
            /* Host locale is ru-RU; neutral, host language, English, then available data. */
            int rank = id == 0 ? 3 : id == 0x0419 ? 2 : id == 0x0409 ? 1 : 0;
            if (rank > preference) { selected = target; found = 1; preference = rank; }
        }
        if (!found) { void_set_last_error(level == 0 ? RESOURCE_TYPE_NOT_FOUND :
            level == 1 ? RESOURCE_NAME_NOT_FOUND : RESOURCE_LANG_NOT_FOUND); return 0; }
        if (level < 2) {
            if (!(selected & 0x80000000u)) goto invalid;
            directory = selected & 0x7FFFFFFFu;
        } else {
            if (selected & 0x80000000u || !resource_range(&image, selected, 16)) goto invalid;
            uint32_t entry = image.root + selected;
            return resource_data(&image, entry, NULL, NULL) ? entry : 0;
        }
    }
invalid:
    void_set_last_error(RESOURCE_DATA_NOT_FOUND);
    return 0;
}

WINAPI_EXPORT("kernel32.dll", FindResourceW, 3) { return resource_entry(ARG(0), ARG(2), ARG(1), 1); }
WINAPI_EXPORT("kernel32.dll", FindResourceA, 3) { return resource_entry(ARG(0), ARG(2), ARG(1), 0); }

WINAPI_EXPORT("kernel32.dll", LoadResource, 2)
{
    resource_image image;
    uint32_t data;
    return resource_image_open(ARG(0), &image) && resource_data(&image, ARG(1), &data, NULL) ? data : 0;
}

WINAPI_EXPORT("kernel32.dll", SizeofResource, 2)
{
    resource_image image;
    uint32_t size;
    return resource_image_open(ARG(0), &image) && resource_data(&image, ARG(1), NULL, &size) ? size : 0;
}

WINAPI_EXPORT("kernel32.dll", LockResource, 1)
{
    if (ARG(0) && void_vm_readable(ARG(0), 1)) return ARG(0);
    void_set_last_error(ERROR_INVALID_HANDLE_);
    return 0;
}

uint32_t void_resource_find(uint32_t module, uint32_t type, uint32_t name, uint32_t *size)
{
    uint32_t entry = resource_entry(module, type, name, 1), data;
    resource_image image;
    if (!entry || !resource_image_open(module, &image) || !resource_data(&image, entry, &data, size)) return 0;
    return data;
}
