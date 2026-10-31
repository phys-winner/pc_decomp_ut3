/* Shell32 command-line parsing over guest UTF-16 and one LocalFree-compatible block. */
#include "host.h"

WINAPI_EXPORT("shell32.dll", CommandLineToArgvW, 2)
{
    uint32_t command = ARG(0), count = ARG(1), length = 0;
    if (!command || !count || !void_vm_writable(count, 4)) {
        void_set_last_error(ERROR_INVALID_PARAMETER_);
        return 0;
    }
    for (;;) {
        uint64_t address = (uint64_t)command + 2ull * length;
        if (address > UINT32_MAX - 1u || !void_vm_readable((uint32_t)address, 2)) {
            void_set_last_error(ERROR_INVALID_PARAMETER_);
            return 0;
        }
        if (!MEM16((uint32_t)address)) break;
        ++length;
    }
    if (!length) {
        const char *path = void_module_path(0);
        if (!path) { void_set_last_error(ERROR_MOD_NOT_FOUND_); return 0; }
        uint32_t units = void_utf8_to_gw(path, 0, 0);
        uint32_t block = void_galloc(8u + 2u * units);
        if (!block) { void_set_last_error(ERROR_NOT_ENOUGH_MEMORY_); return 0; }
        MEM32(block) = block + 8;
        MEM32(block + 4) = 0;
        void_utf8_to_gw(path, block + 8, units);
        MEM32(count) = 1;
        return block;
    }
    /* Each argument consumes input; length+2 slots and WCHARs bound both outputs. */
    uint64_t bytes = 6ull * (length + 2ull);
    if (bytes > UINT32_MAX) { void_set_last_error(ERROR_NOT_ENOUGH_MEMORY_); return 0; }
    uint32_t block = void_galloc((uint32_t)bytes);
    if (!block) { void_set_last_error(ERROR_NOT_ENOUGH_MEMORY_); return 0; }
    uint32_t strings = block + 4u * (length + 2u), argc = 1, i = 0, written = 0;
    const uint16_t *input = (const uint16_t *)GPTR(command);
    uint16_t *output = (uint16_t *)GPTR(strings);
    MEM32(block) = strings;
    /* argv[0] has separate Shell32 rules: backslashes are literal. */
    if (input[i] == '"') {
        ++i;
        while (input[i] && input[i] != '"') output[written++] = input[i++];
        if (input[i]) ++i;
    } else {
        while (input[i] && input[i] != ' ' && !(input[i] >= 9 && input[i] <= 13))
            output[written++] = input[i++];
        if (input[i]) ++i;
    }
    output[written++] = 0;
    for (;;) {
        while (input[i] == ' ' || input[i] == '\t') ++i;
        if (!input[i]) break;
        MEM32(block + 4u * argc++) = strings + 2u * written;
        unsigned quotes = 0;
        while (input[i] && (quotes || (input[i] != ' ' && input[i] != '\t'))) {
            if (input[i] == '\\') {
                uint32_t start = i;
                while (input[i] == '\\') ++i;
                uint32_t slashes = i - start, copies = input[i] == '"' ? slashes / 2 : slashes;
                while (copies--) output[written++] = '\\';
                if (input[i] != '"') continue;
                if (slashes & 1u) { output[written++] = '"'; ++i; continue; }
            }
            if (input[i] == '"') {
                /* Shell32 quote runs use groups of three, unlike the modern CRT. */
                do {
                    if (++quotes == 3) { output[written++] = '"'; quotes = 0; }
                    ++i;
                } while (input[i] == '"');
                if (quotes == 2) quotes = 0;
            } else {
                output[written++] = input[i++];
            }
        }
        output[written++] = 0;
    }
    MEM32(block + 4u * argc) = 0;
    MEM32(count) = argc;
    return block;
}
