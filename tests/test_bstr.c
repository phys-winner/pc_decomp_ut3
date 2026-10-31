/* Production BSTR exports by name and ordinal, using the actual guest heap. */
#include "../android/host/bstr.c"
#include <assert.h>
#include <unistd.h>

void void_process_exit(uint32_t code, int detach) { (void)detach; _exit((int)code); }

static uint32_t api(const char *name, uint32_t ordinal, uint32_t argument)
{
    const void_shim *shim = void_shim_find("oleaut32.dll", name, ordinal);
    assert(shim);
    uint32_t before = g_esp;
    uint32_t result = void_call_guest(void_thunk(shim->fn, shim->name), 1, &argument, 1);
    assert(g_esp == before);
    return result;
}

static void check(const uint16_t *text, uint32_t source_units, uint32_t copied_units)
{
    uint32_t input = void_galloc((source_units + 1) * 2);
    memcpy(GPTR(input), text, source_units * 2);
    MEM16(input + source_units * 2) = 0;
    for (int ordinal = 0; ordinal <= 1; ++ordinal) {
        uint32_t string = api(ordinal ? NULL : "SysAllocString", ordinal ? 2 : 0, input);
        assert(string && string != input && (string & 3u) == 0);
        uint32_t block = string - 4;
        assert(bstr_owned_block(string) == block);
        assert(void_heap_size(void_process_heap(), block) == copied_units * 2 + 6);
        assert(MEM32(block) == copied_units * 2);
        assert(!memcmp(GPTR(string), text, copied_units * 2));
        assert(!MEM16(string + copied_units * 2));
        uint32_t prefix = MEM32(block);
        MEM32(block) = UINT32_MAX;
        assert(!bstr_owned_block(string));
        MEM32(block) = prefix;
        if (copied_units) {
            uint16_t saved = MEM16(input);
            MEM16(input) = 'Z';
            assert(MEM16(string) == text[0]);
            MEM16(input) = saved;
        }
        api(ordinal ? "SysFreeString" : NULL, ordinal ? 0 : 6, string);
        assert(!bstr_owned_block(string));
        assert(void_heap_size(void_process_heap(), block) == UINT32_MAX);
    }
    void_gfree(input);
}

int main(void)
{
    assert(void_guest_reserve());
    void_threads_init(); void_dispatch_init(); void_machine_enter();
    assert(void_shim_find("oleaut32.dll", "SysAllocString", 0) == void_shim_find("oleaut32.dll", NULL, 2));
    assert(void_shim_find("oleaut32.dll", "SysFreeString", 0) == void_shim_find("oleaut32.dll", NULL, 6));
    void_set_last_error(123);
    assert(!api("SysAllocString", 0, 0) && void_get_last_error() == 123);
    api(NULL, 6, 0);
    assert(void_get_last_error() == 123);
    static const uint16_t empty[] = {0};
    static const uint16_t ascii[] = {'A', 'B'};
    static const uint16_t unicode[] = {0x0422, 0x0435, 0x0441, 0x0442};
    static const uint16_t surrogate[] = {'A', 0xD800, 0xDF48, 'B'};
    static const uint16_t embedded[] = {'A', 0, 'B'};
    check(empty, 0, 0); check(ascii, 2, 2); check(unicode, 4, 4);
    check(surrogate, 4, 4); check(embedded, 3, 1);
    uint32_t page = void_vm_alloc(0, 0x1000, 0x3000u, 0x04u);
    assert(page);
    MEM16(page + 0xFFE) = 0;
    uint32_t string = api(NULL, 2, page + 0xFFE);
    assert(string && !MEM32(string - 4) && !MEM16(string));
    api(NULL, 6, string);
    MEM16(page + 0xFFC) = 'X';
    string = api(NULL, 2, page + 0xFFC);
    assert(string && MEM32(string - 4) == 2 && MEM16(string) == 'X' && !MEM16(string + 2));
    api(NULL, 6, string);
    assert(void_vm_free(page, 0, 0x8000u));
    assert(!bstr_owned_block(1) && !bstr_owned_block(0x1000));
    void_machine_leave();
    puts("BSTR PASS: name/ordinal2/6, byte prefix, UTF16/surrogates/NUL, null-vs-empty, page boundary, ownership/free, malformed size, stdcall");
    return 0;
}
