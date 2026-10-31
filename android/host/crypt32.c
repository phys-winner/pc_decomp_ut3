/* Win32 DATA_BLOB/LocalFree boundary; Android owns the authenticated cipher. */
#include "host.h"

#define CRYPT_MAX_BLOB (1u << 20)
#define CRYPT_MAX_DESCRIPTION 8192u
#define CRYPT_INVALID_DATA 13u

extern int void_android_crypt(int protect, const uint8_t *input, uint32_t input_bytes,
    const uint8_t *entropy, uint32_t entropy_bytes, const uint8_t *description,
    uint32_t description_bytes, uint8_t **output, uint32_t *output_bytes,
    uint8_t **output_description, uint32_t *output_description_bytes) __attribute__((weak));

typedef struct { uint32_t bytes, address; } crypt_guest_blob;
typedef struct { uint8_t *data; uint32_t bytes; } crypt_host_blob;
_Static_assert(sizeof(crypt_guest_blob) == 8, "x86 DATA_BLOB");

static void crypt_wipe_free(crypt_host_blob *blob, uint32_t limit)
{
    if (blob->data && blob->bytes <= limit) {
        volatile uint8_t *p = blob->data;
        for (uint32_t i = 0; i < blob->bytes; ++i) p[i] = 0;
    }
    free(blob->data);
    blob->data = NULL; blob->bytes = 0;
}

static uint32_t crypt_copy_blob(uint32_t address, crypt_host_blob *out)
{
    crypt_guest_blob in;
    if (!address || !void_vm_readable(address, sizeof in)) return ERROR_INVALID_PARAMETER_;
    memcpy(&in, GPTR(address), sizeof in);
    if (in.bytes > CRYPT_MAX_BLOB || (in.bytes &&
        (!in.address || !void_vm_readable(in.address, in.bytes)))) return ERROR_INVALID_PARAMETER_;
    if (!in.bytes) return 0;
    out->data = malloc(in.bytes);
    if (!out->data) return ERROR_NOT_ENOUGH_MEMORY_;
    out->bytes = in.bytes;
    memcpy(out->data, GPTR(in.address), in.bytes);
    return 0;
}

static uint32_t crypt_copy_description(uint32_t address, crypt_host_blob *out)
{
    if (!address) return 0;
    for (uint32_t bytes = 0; bytes <= CRYPT_MAX_DESCRIPTION; bytes += 2) {
        uint64_t at = (uint64_t)address + bytes;
        uint16_t unit;
        if (at > UINT32_MAX - 1u || !void_vm_readable((uint32_t)at, 2)) return ERROR_INVALID_PARAMETER_;
        memcpy(&unit, GPTR((uint32_t)at), 2);
        if (unit) continue;
        if (!bytes) return 0;
        out->data = malloc(bytes);
        if (!out->data) return ERROR_NOT_ENOUGH_MEMORY_;
        out->bytes = bytes;
        memcpy(out->data, GPTR(address), bytes);
        return 0;
    }
    return ERROR_INVALID_PARAMETER_;
}

static int crypt_targets_valid(uint32_t output, uint32_t description)
{
    if (!output || !void_vm_writable(output, 8)) return 0;
    if (!description) return 1;
    if (!void_vm_writable(description, 4)) return 0;
    return (uint64_t)description + 4 <= output || (uint64_t)output + 8 <= description;
}

static uint32_t crypt_transform(int protect)
{
    uint32_t input_va = ARG(0), description_va = ARG(1), entropy_va = ARG(2);
    uint32_t reserved = ARG(3), prompt = ARG(4), flags = ARG(5), output_va = ARG(6);
    uint32_t description_out = protect ? 0 : description_va;
    crypt_host_blob input = {0}, entropy = {0}, description = {0}, output = {0}, result_description = {0};
    uint32_t error = ERROR_INVALID_PARAMETER_, output_block = 0, description_block = 0;
    if (reserved || prompt || (flags & ~1u) || !crypt_targets_valid(output_va, description_out)) goto done;
    if ((error = crypt_copy_blob(input_va, &input))) goto done;
    if (entropy_va && (error = crypt_copy_blob(entropy_va, &entropy))) goto done;
    if (protect && (error = crypt_copy_description(description_va, &description))) goto done;

    /* Copy first: callers may reuse their input DATA_BLOB for the result. */
    crypt_guest_blob cleared = {0};
    memcpy(GPTR(output_va), &cleared, sizeof cleared);
    if (description_out) memcpy(GPTR(description_out), &cleared, 4);
    if (!void_android_crypt) { error = ERROR_NOT_SUPPORTED_; goto done; }
    void_blocking_begin();
    int backend_error = void_android_crypt(protect, input.data, input.bytes,
        entropy.data, entropy.bytes, description.data, description.bytes,
        &output.data, &output.bytes, &result_description.data, &result_description.bytes);
    void_blocking_end();
    if (backend_error) { error = backend_error > 0 ? (uint32_t)backend_error : CRYPT_INVALID_DATA; goto done; }
    if (!crypt_targets_valid(output_va, description_out)) { error = ERROR_INVALID_PARAMETER_; goto done; }
    if (output.bytes > CRYPT_MAX_BLOB || (output.bytes && !output.data) ||
        result_description.bytes > CRYPT_MAX_DESCRIPTION || (result_description.bytes & 1u) ||
        (result_description.bytes && !result_description.data)) { error = CRYPT_INVALID_DATA; goto done; }

    output_block = void_heap_alloc(void_process_heap(), output.bytes ? output.bytes : 1, 0);
    if (!output_block) { error = ERROR_NOT_ENOUGH_MEMORY_; goto done; }
    if (description_out) {
        description_block = void_heap_alloc(void_process_heap(), result_description.bytes + 2, 0);
        if (!description_block) { error = ERROR_NOT_ENOUGH_MEMORY_; goto done; }
    }
    if (output.bytes) memcpy(GPTR(output_block), output.data, output.bytes);
    if (description_block) {
        if (result_description.bytes) memcpy(GPTR(description_block), result_description.data, result_description.bytes);
        memset(GPTR(description_block + result_description.bytes), 0, 2);
    }
    crypt_guest_blob result = {output.bytes, output_block};
    memcpy(GPTR(output_va), &result, sizeof result);
    if (description_out) memcpy(GPTR(description_out), &description_block, 4);
    output_block = description_block = 0;
    error = 0;
done:
    if (output_block) void_heap_free(void_process_heap(), output_block);
    if (description_block) void_heap_free(void_process_heap(), description_block);
    crypt_wipe_free(&input, CRYPT_MAX_BLOB);
    crypt_wipe_free(&entropy, CRYPT_MAX_BLOB);
    crypt_wipe_free(&description, CRYPT_MAX_DESCRIPTION);
    crypt_wipe_free(&output, CRYPT_MAX_BLOB);
    crypt_wipe_free(&result_description, CRYPT_MAX_DESCRIPTION);
    if (error) { void_set_last_error(error); return 0; }
    return 1;
}

WINAPI_EXPORT("crypt32.dll", CryptProtectData, 7) { return crypt_transform(1); }
WINAPI_EXPORT("crypt32.dll", CryptUnprotectData, 7) { return crypt_transform(0); }
