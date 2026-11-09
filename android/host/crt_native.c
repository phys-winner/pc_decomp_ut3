/* msvcr80 memory primitives in native code: memcpy 0x10014FB0, memmove 0x10015320, memset 0x10014A20.
 *
 * The original memcpy is memmove in MSVC 8 (it copies downwards when the destination starts inside the
 * source, upwards otherwise, with the SSE2 fastcopy_I block loop for large co-aligned copies), so both
 * become bionic memmove over the guest window; memset becomes memset. A WAR-Torlan bot match spent
 * about 1.5% of the game thread in the lifted copies. Memory effects and EAX (the destination) are the
 * original's; the C calling convention leaves ECX, EDX, the flags and the XMM registers undefined after
 * the call, and the original's scratch values there are not reproduced. Ranges that would wrap the
 * 4 GiB guest space keep the lifted code. Callers reach these through the msvcr80 import slots
 * (dispatch); direct calls inside msvcr80 keep the lifted bodies. Exact mode keeps the originals. */
#include "host.h"
#include "native_mode.inc"

static void_fn original_memcpy, original_memmove, original_memset;
static int crt_mode = -1;

static __attribute__((cold, noinline)) int crt_mode_load(void)
{
    crt_mode = void_native_mode(1);
    void_log("[native-crt] memcpy/memmove/memset: mode %d", crt_mode);
    return crt_mode;
}

static inline int crt_native(void)
{
    return __builtin_expect(crt_mode >= 0, 1) ? crt_mode : crt_mode_load();
}

static inline int crt_range(uint32_t address, uint32_t count)
{
    return (uint64_t)address + count <= 0x100000000ull;
}

static void crt_copy(void_fn original)
{
    const uint32_t dst = ARG(0), src = ARG(1), count = ARG(2);
    if (!crt_native() || !crt_range(dst, count) || !crt_range(src, count)) { original(); return; }
    if (count) memmove(GPTR(dst), GPTR(src), count);
    g_eax = dst;
    g_esp += 4;
}

static void crt_memcpy(void) { crt_copy(original_memcpy); }
static void crt_memmove(void) { crt_copy(original_memmove); }

static void crt_memset(void)
{
    const uint32_t dst = ARG(0), value = ARG(1), count = ARG(2);
    if (!crt_native() || !crt_range(dst, count)) { original_memset(); return; }
    if (count) memset(GPTR(dst), (int)(uint8_t)value, count);
    g_eax = dst;
    g_esp += 4;
}

void void_crt_native_init(void)
{
    original_memcpy = void_dispatch_find(0x10014FB0u);
    original_memmove = void_dispatch_find(0x10015320u);
    original_memset = void_dispatch_find(0x10014A20u);
    if (!original_memcpy || !original_memmove || !original_memset) {
        void_log("[native-crt] msvcr80 memory primitives not found; keeping the lifted code");
        return;
    }
    void_dispatch_add(0x10014FB0u, crt_memcpy);
    void_dispatch_add(0x10015320u, crt_memmove);
    void_dispatch_add(0x10014A20u, crt_memset);
}
