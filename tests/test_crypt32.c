/* Synthetic callback oracle for the production Win32 boundary. No license
 * data or cipher implementation is used by this fixture. */
#include "../android/host/threads.c"
#include <assert.h>

typedef struct { void *pointer; size_t bytes; int wipe; } allocation;
static allocation allocations[32];
static unsigned host_calls, fail_host_at, guest_calls, fail_guest_at;
static uint32_t guest_blocks[4];
static void *fixture_malloc(size_t bytes)
{
    if (++host_calls == fail_host_at) return NULL;
    void *p = malloc(bytes); assert(p);
    unsigned i; for (i = 0; i < 32 && allocations[i].pointer; ++i) {}
    assert(i < 32); allocations[i] = (allocation){p, bytes, 1};
    return p;
}
static void fixture_free(void *pointer)
{
    if (!pointer) return;
    unsigned i; for (i = 0; i < 32 && allocations[i].pointer != pointer; ++i) {}
    assert(i < 32);
    if (allocations[i].wipe)
        for (size_t n = 0; n < allocations[i].bytes; ++n) assert(!((uint8_t *)pointer)[n]);
    allocations[i].pointer = NULL; free(pointer);
}
static uint32_t fixture_heap_alloc(uint32_t heap, uint32_t bytes, int zero)
{
    if (++guest_calls == fail_guest_at) return 0;
    uint32_t p = void_heap_alloc(heap, bytes, zero);
    assert(guest_calls <= 4); guest_blocks[guest_calls - 1] = p; return p;
}
static unsigned begun, ended, released;
static void_regs saved_context;
static void fixture_begin(void)
{
    assert(!released); released = 1; ++begun;
    memset(&saved_context, 0, sizeof saved_context); void_regs_save(&saved_context);
    void_blocking_begin();
}
static void fixture_end(void)
{
    void_blocking_end(); assert(released); released = 0; ++ended;
    void_regs actual; memset(&actual, 0, sizeof actual); void_regs_save(&actual);
    assert(!memcmp(&actual, &saved_context, sizeof actual));
}
#define malloc fixture_malloc
#define free fixture_free
#define void_heap_alloc fixture_heap_alloc
#define void_blocking_begin fixture_begin
#define void_blocking_end fixture_end
#include "../android/host/crypt32.c"
#undef malloc
#undef free
#undef void_heap_alloc
#undef void_blocking_begin
#undef void_blocking_end

void void_process_exit(uint32_t code, int detach) { (void)detach; _exit((int)code); }
static const uint8_t input_bytes[] = {3, 9, 27, 81};
static const uint8_t entropy_bytes[] = {2, 4, 8, 16, 32, 64, 128, 0};
static const uint8_t description_bytes[] = {0x54, 0x04, 0x35, 0x04};
static const uint8_t result_bytes[] = {0x10, 0x20, 0x30};
static unsigned callback_calls, backend_mode, expect_empty, expect_protect, expect_description, stage;
static int contend;
static uint32_t source_data, entropy_data, description_data, unmap_target;
int void_android_crypt(int protect, const uint8_t *input, uint32_t bytes,
    const uint8_t *entropy, uint32_t entropy_count, const uint8_t *description,
    uint32_t description_count, uint8_t **output, uint32_t *output_count,
    uint8_t **output_description, uint32_t *output_description_count)
{
    assert(released && protect == (int)expect_protect); ++callback_calls;
    assert(bytes == (expect_empty ? 0 : sizeof input_bytes));
    assert(entropy_count == (expect_empty ? 0 : sizeof entropy_bytes));
    assert(!bytes || !memcmp(input, input_bytes, bytes));
    assert(!entropy_count || !memcmp(entropy, entropy_bytes, entropy_count));
    assert((uintptr_t)input < VOID_GUEST_BASE || (uintptr_t)input >= VOID_GUEST_BASE + (1ull << 32));
    assert((uintptr_t)entropy < VOID_GUEST_BASE || (uintptr_t)entropy >= VOID_GUEST_BASE + (1ull << 32));
    if (protect && !expect_empty && expect_description) {
        assert(description_count == sizeof description_bytes);
        assert(!memcmp(description, description_bytes, description_count));
    } else assert(!description_count);
    if (contend) {
        while (__atomic_load_n(&stage, __ATOMIC_ACQUIRE) != 2) sched_yield();
        assert(!memcmp(input, input_bytes, bytes) && !memcmp(entropy, entropy_bytes, entropy_count));
        if (protect && expect_description) assert(!memcmp(description, description_bytes, description_count));
    }
    if (backend_mode == 1) return 13;
    if (backend_mode == 5) return 0;
    *output = fixture_malloc(sizeof result_bytes); memcpy(*output, result_bytes, sizeof result_bytes);
    *output_count = sizeof result_bytes;
    if (backend_mode == 2) {
        for (unsigned i = 0; i < 32; ++i) if (allocations[i].pointer == *output) allocations[i].wipe = 0;
        *output_count = CRYPT_MAX_BLOB + 1;
    }
    if (!protect) {
        *output_description_count = backend_mode == 3 ? 3 : sizeof description_bytes;
        *output_description = fixture_malloc(*output_description_count);
        memcpy(*output_description, description_bytes, *output_description_count);
    }
    return 0;
}
static void *contender(void *unused)
{
    (void)unused; __atomic_store_n(&stage, 1, __ATOMIC_RELEASE);
    void_machine_enter();
    memset(GPTR(source_data), 0, sizeof input_bytes); memset(GPTR(entropy_data), 0, sizeof entropy_bytes);
    memset(GPTR(description_data), 0, sizeof description_bytes);
    if (unmap_target) assert(void_vm_free(unmap_target, 0, 0x8000));
    g_eax = g_ebx = g_ecx = g_edx = g_esi = g_edi = g_ebp = 0xdeadbeef;
    g_flag_k = FK_TEST; g_flag_a = 12; g_flag_b = 24; g_flag_cf = 1;
    g_mm[2] = 0x1234; g_xmm[3].u64[0] = 0x5678;
    __atomic_store_n(&stage, 2, __ATOMIC_RELEASE);
    void_machine_leave(); return NULL;
}
static void no_allocations(void)
{
    assert(begun == ended && !released);
    for (unsigned i = 0; i < 32; ++i) assert(!allocations[i].pointer);
}
static uint32_t invoke(const char *name, unsigned count, const uint32_t *args)
{
    const void_shim *shim = void_shim_find(count == 7 ? "crypt32.dll" : "kernel32.dll", name, 0); assert(shim);
    uint32_t before = g_esp;
    uint32_t result = void_call_guest(void_thunk(shim->fn, name), count, args, 1);
    assert(g_esp == before); no_allocations(); return result;
}
static void blob(uint32_t at, uint32_t bytes, uint32_t pointer)
{ crypt_guest_blob data = {bytes, pointer}; memcpy(GPTR(at), &data, sizeof data); }
static crypt_guest_blob get_blob(uint32_t at)
{ crypt_guest_blob data; memcpy(&data, GPTR(at), sizeof data); return data; }
static void free_local(uint32_t pointer)
{
    assert(pointer && void_heap_size(void_process_heap(), pointer) != UINT32_MAX);
    assert(!invoke("LocalFree", 1, &pointer));
    assert(void_heap_size(void_process_heap(), pointer) == UINT32_MAX);
}
static void reset(uint32_t input, uint32_t entropy)
{
    host_calls = guest_calls = fail_host_at = fail_guest_at = 0;
    memset(guest_blocks, 0, sizeof guest_blocks);
    memcpy(GPTR(source_data), input_bytes, sizeof input_bytes);
    memcpy(GPTR(entropy_data), entropy_bytes, sizeof entropy_bytes);
    memcpy(GPTR(description_data), description_bytes, sizeof description_bytes); MEM16(description_data + 4) = 0;
    blob(input, sizeof input_bytes, source_data); blob(entropy, sizeof entropy_bytes, entropy_data);
    backend_mode = expect_empty = contend = unmap_target = 0;
    expect_description = 1;
}
int main(void)
{
    alarm(15); assert(void_guest_reserve()); void_threads_init(); void_dispatch_init(); void_machine_enter();
    uint32_t input = void_galloc(16), entropy = void_galloc(8), output = void_galloc(16), description_out = void_galloc(4);
    source_data = void_galloc(16); entropy_data = void_galloc(16); description_data = void_galloc(16);
    uint32_t a[] = {input, description_data, entropy, 0, 0, 1, output};
    for (expect_protect = 0; expect_protect < 2; ++expect_protect) {
        reset(input, entropy); a[1] = expect_protect ? description_data : description_out;
        void_set_last_error(123); unsigned before = callback_calls;
        assert(invoke(expect_protect ? "CryptProtectData" : "CryptUnprotectData", 7, a));
        assert(callback_calls == before + 1 && void_get_last_error() == 123);
        crypt_guest_blob result = get_blob(output); assert(result.bytes == 3 && !memcmp(GPTR(result.address), result_bytes, 3));
        free_local(result.address);
        if (!expect_protect) {
            uint32_t desc = MEM32(description_out); assert(!memcmp(GPTR(desc), description_bytes, 4) && !MEM16(desc + 4)); free_local(desc);
        }
    }
    for (expect_protect = 0; expect_protect < 2; ++expect_protect) {
        reset(input, entropy); a[1] = 0; expect_description = 0;
        assert(invoke(expect_protect ? "CryptProtectData" : "CryptUnprotectData", 7, a));
        free_local(get_blob(output).address); assert(guest_calls == 1);
    }
    expect_protect = 1; reset(input, entropy); a[1] = description_data;
    a[6] = input; assert(invoke("CryptProtectData", 7, a)); free_local(get_blob(input).address); a[6] = output;
    for (unsigned bad = 0; bad < 8; ++bad) {
        reset(input, entropy); uint32_t args[7]; memcpy(args, a, sizeof args);
        switch (bad) {
        case 0: args[0] = 1; break;
        case 1: blob(input, CRYPT_MAX_BLOB + 1, source_data); break;
        case 2: blob(input, 2, UINT32_MAX); break;
        case 3: args[2] = UINT32_MAX - 3; break;
        case 4: args[3] = 1; break;
        case 5: args[4] = 1; break;
        case 6: args[5] = 4; break;
        case 7: args[6] = UINT32_MAX - 3; break;
        }
        unsigned before = callback_calls;
        assert(!invoke("CryptProtectData", 7, args) && void_get_last_error() == 87 && callback_calls == before);
    }
    reset(input, entropy); uint32_t unterminated = void_galloc(CRYPT_MAX_DESCRIPTION + 2);
    memset(GPTR(unterminated), 0x31, CRYPT_MAX_DESCRIPTION + 2); a[1] = unterminated;
    assert(!invoke("CryptProtectData", 7, a) && void_get_last_error() == 87); a[1] = description_data;
    for (unsigned fail = 1; fail <= 3; ++fail) {
        reset(input, entropy); fail_host_at = fail;
        assert(!invoke("CryptProtectData", 7, a) && void_get_last_error() == 8);
    }
    for (unsigned mode = 1; mode <= 3; ++mode) {
        expect_protect = mode == 2; reset(input, entropy); backend_mode = mode;
        a[1] = expect_protect ? description_data : description_out;
        assert(!invoke(expect_protect ? "CryptProtectData" : "CryptUnprotectData", 7, a) && void_get_last_error() == 13);
        assert(!get_blob(output).address && !get_blob(output).bytes);
    }
    expect_protect = 0; a[1] = description_out;
    for (unsigned fail = 1; fail <= 2; ++fail) {
        reset(input, entropy); fail_guest_at = fail;
        assert(!invoke("CryptUnprotectData", 7, a) && void_get_last_error() == 8);
        assert(!get_blob(output).address && !MEM32(description_out));
        if (guest_blocks[0]) assert(void_heap_size(void_process_heap(), guest_blocks[0]) == UINT32_MAX);
    }
    reset(input, entropy); a[1] = output + 4;
    assert(!invoke("CryptUnprotectData", 7, a) && void_get_last_error() == 87); a[1] = description_out;
    reset(input, entropy); expect_empty = 1; backend_mode = 5;
    blob(input, 0, UINT32_MAX); blob(entropy, 0, UINT32_MAX);
    for (unsigned optional = 0; optional < 2; ++optional) {
        a[2] = optional ? 0 : entropy;
        assert(invoke("CryptUnprotectData", 7, a)); assert(!get_blob(output).bytes);
        free_local(get_blob(output).address); assert(!MEM16(MEM32(description_out))); free_local(MEM32(description_out));
    }
    a[2] = entropy;
    for (unsigned invalid_target = 0; invalid_target < 2; ++invalid_target) {
        expect_protect = 1; reset(input, entropy); a[1] = description_data;
        uint32_t target = output;
        if (invalid_target) { unmap_target = void_vm_alloc(0, 65536, 0x3000, 4); assert(unmap_target); target = unmap_target; }
        a[6] = target; contend = 1; stage = 0;
        pthread_t thread; assert(!pthread_create(&thread, NULL, contender, NULL));
        while (!__atomic_load_n(&stage, __ATOMIC_ACQUIRE) || !machine_contended()) sched_yield();
        uint32_t result = invoke("CryptProtectData", 7, a);
        assert(!pthread_join(thread, NULL));
        if (invalid_target) assert(!result && void_get_last_error() == 87);
        else { assert(result); free_local(get_blob(output).address); }
    }
    void_machine_leave();
    puts("PASS production crypt32 boundary: 7arg stdcall, spans/limits/flags, copied inputs, LocalFree outputs/UTF16 description, aliasing, errors, allocation rollback, native wipes and balanced real VM handover; synthetic callback only");
    return 0;
}
