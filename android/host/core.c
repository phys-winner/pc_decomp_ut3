/* Register file, dispatch, CPUID, x87 helpers and fatal diagnostics. */
#define _GNU_SOURCE
#include "host.h"
#include <dlfcn.h>
#include <errno.h>
#include <signal.h>
#include <ucontext.h>
#include <stdatomic.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#ifdef __ANDROID__
#include <android/log.h>
#endif

/* ------------------------------------------------------------ register file */
VOID_HIDDEN uint32_t g_eax, g_ecx, g_edx, g_esp, g_ebx, g_esi, g_edi, g_ebp;
#ifdef RECOMP_X87_WINDOW
VOID_HIDDEN double g_st_win[RECOMP_X87_SLOTS];
VOID_HIDDEN int64_t g_st_i64_win[RECOMP_X87_SLOTS];
VOID_HIDDEN int g_fp_sp = RECOMP_X87_SLOTS / 2;
#else
VOID_HIDDEN double g_st[8];
#endif
VOID_HIDDEN int g_fp_top;
VOID_HIDDEN uint16_t g_fpu_cw = 0x027F;
VOID_HIDDEN uint64_t g_fp_round_mask = ~0ull, g_fp_store_mask = ~0ull;   /* of 0x027F (fp_cw_masks) */
VOID_HIDDEN uint16_t g_seg_cs, g_seg_ds, g_seg_es, g_seg_fs, g_seg_gs, g_seg_ss;
VOID_HIDDEN uint64_t g_mm[8];
VOID_HIDDEN xmm_t g_xmm[8];
VOID_HIDDEN uint32_t g_mxcsr = 0x1F80;
VOID_HIDDEN uint32_t g_fs_base, g_gs_base, g_es_base;
VOID_HIDDEN ptrdiff_t g_mem_base = (ptrdiff_t)VOID_GUEST_BASE;
VOID_HIDDEN uint32_t g_cur_func;
VOID_HIDDEN uint32_t g_icall_trace[ICALL_TRACE_SIZE], g_icall_from[ICALL_TRACE_SIZE];
VOID_HIDDEN uint32_t g_icall_trace_idx, g_icall_count;
VOID_HIDDEN uint32_t g_flag_k, g_flag_a, g_flag_b, g_flag_cf;
VOID_HIDDEN int void_fpu_cmp = 1;
#ifndef RECOMP_X87_WINDOW
VOID_HIDDEN int64_t g_st_i64[8];
#endif
VOID_HIDDEN void (*recomp_yield_hook)(void);
VOID_HIDDEN uint32_t g_backedges;
#define VOID_ENTER_RING 1024
VOID_HIDDEN uint32_t g_enter_trace[VOID_ENTER_RING];
VOID_HIDDEN uint32_t g_enter_idx;

int void_verbose;

void void_regs_save(void_regs *r)
{
    r->eax = g_eax; r->ecx = g_ecx; r->edx = g_edx; r->ebx = g_ebx;
    r->esp = g_esp; r->ebp = g_ebp; r->esi = g_esi; r->edi = g_edi;
    r->fs = g_fs_base; r->cur = g_cur_func;
    r->flag_k = g_flag_k; r->flag_a = g_flag_a; r->flag_b = g_flag_b; r->flag_cf = g_flag_cf;
    memcpy(r->st, g_st, sizeof g_st);
    memcpy(r->st_i64, g_st_i64, sizeof g_st_i64);
    r->fp_top = g_fp_top; r->fpu_cmp = void_fpu_cmp; r->fpu_cw = g_fpu_cw;
    r->fp_round_mask = g_fp_round_mask; r->fp_store_mask = g_fp_store_mask;
    memcpy(r->mm, g_mm, sizeof g_mm);
    memcpy(r->xmm, g_xmm, sizeof g_xmm);
    r->mxcsr = g_mxcsr;
    r->eh_frames = g_eh_frames; r->eh_obj = g_eh_obj; r->eh_info = g_eh_info;
    r->backedges = g_backedges;
}

void void_regs_load(const void_regs *r)
{
    g_eax = r->eax; g_ecx = r->ecx; g_edx = r->edx; g_ebx = r->ebx;
    g_esp = r->esp; g_ebp = r->ebp; g_esi = r->esi; g_edi = r->edi;
    g_fs_base = r->fs; g_cur_func = r->cur;
    g_flag_k = r->flag_k; g_flag_a = r->flag_a; g_flag_b = r->flag_b; g_flag_cf = r->flag_cf;
    memcpy(g_st, r->st, sizeof g_st);
    memcpy(g_st_i64, r->st_i64, sizeof g_st_i64);
    g_fp_top = r->fp_top; void_fpu_cmp = r->fpu_cmp; g_fpu_cw = r->fpu_cw;
    g_fp_round_mask = r->fp_round_mask; g_fp_store_mask = r->fp_store_mask;
    memcpy(g_mm, r->mm, sizeof g_mm);
    memcpy(g_xmm, r->xmm, sizeof g_xmm);
    g_mxcsr = r->mxcsr;
    g_eh_frames = (recomp_ehframe_t *)r->eh_frames; g_eh_obj = r->eh_obj; g_eh_info = r->eh_info;
    g_backedges = r->backedges;
}

/* --------------------------------------------------------------- logging */
static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;
static FILE *log_file;

void void_log_open(const char *path)
{
    pthread_mutex_lock(&log_lock);
    if (log_file) fclose(log_file);
    log_file = path ? fopen(path, "w") : NULL;
    if (log_file) setvbuf(log_file, NULL, _IOLBF, 0);
    pthread_mutex_unlock(&log_lock);
}

static void log_va(const char *fmt, va_list ap)
{
    char line[2048];
    vsnprintf(line, sizeof line, fmt, ap);
    pthread_mutex_lock(&log_lock);
    fprintf(stderr, "%s\n", line);
    if (log_file) fprintf(log_file, "%s\n", line);
#ifdef __ANDROID__
    __android_log_write(ANDROID_LOG_INFO, "UT3", line);
#endif
    pthread_mutex_unlock(&log_lock);
}

void void_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    log_va(fmt, ap);
    va_end(ap);
}

void void_log_flush(void)
{
    pthread_mutex_lock(&log_lock);
    fflush(stderr);
    if (log_file) fflush(log_file);
    pthread_mutex_unlock(&log_lock);
}

/* ------------------------------------------------------------- dispatch */
typedef struct { uint32_t va; void_fn fn; } slot_t;
static slot_t *table;
static uint32_t table_mask, table_used;
static uint32_t image_lo[64], image_hi[64];
static int image_count;

static inline uint32_t hash_va(uint32_t va) { return (va * 0x9E3779B1u) >> 7; }

static void table_grow(void)
{
    uint32_t capacity = table ? (table_mask + 1) * 2 : 1u << 18;
    slot_t *old = table;
    uint32_t old_capacity = table ? table_mask + 1 : 0;
    table = (slot_t *)calloc(capacity, sizeof *table);
    if (!table) void_fatal("dispatch table allocation failed");
    table_mask = capacity - 1;
    table_used = 0;
    for (uint32_t i = 0; i < old_capacity; ++i)
        if (old[i].va) void_dispatch_add(old[i].va, old[i].fn);
    free(old);
}

/* A direct-mapped cache in front of the table for void_resolve. Indirect
 * calls (C++ virtual calls above all) hit the same few thousand targets over
 * and over, and the table (megabytes) misses the data cache on nearly every
 * probe. Only the thread holding the machine resolves, so plain stores do. */
#define RESOLVE_CACHE_BITS 12
static struct { uint32_t va; void_fn fn; } resolve_cache[1u << RESOLVE_CACHE_BITS];
static inline uint32_t resolve_slot(uint32_t va) { return (va * 0x9E3779B1u) >> (32 - RESOLVE_CACHE_BITS); }

void void_dispatch_add(uint32_t va, void_fn fn)
{
    if (!va) void_fatal("dispatch: VA 0 cannot be registered");
    resolve_cache[resolve_slot(va)].va = 0;   /* a re-registered VA must not keep its old target */
    if (!table || (table_used + 1) * 2 > table_mask + 1) table_grow();
    for (uint32_t i = hash_va(va) & table_mask;; i = (i + 1) & table_mask) {
        if (table[i].va == va) { table[i].fn = fn; return; }
        if (!table[i].va) { table[i].va = va; table[i].fn = fn; ++table_used; return; }
    }
}

void_fn void_dispatch_find(uint32_t va)
{
    if (!va || !table) return NULL;
    for (uint32_t i = hash_va(va) & table_mask;; i = (i + 1) & table_mask) {
        if (table[i].va == va) return table[i].fn;
        if (!table[i].va) return NULL;
    }
}

void void_dispatch_image(uint32_t lo, uint32_t hi)
{
    if (image_count < 64) { image_lo[image_count] = lo; image_hi[image_count] = hi; ++image_count; }
}

static int in_image(uint32_t va)
{
    for (int i = 0; i < image_count; ++i) if (va >= image_lo[i] && va < image_hi[i]) return 1;
    return 0;
}

void void_dispatch_init(void)
{
    for (uint32_t i = 0; i < recomp_dispatch_count; ++i) {
        if (i && recomp_dispatch_table[i].address <= recomp_dispatch_table[i - 1].address)
            void_fatal("dispatch table not strictly sorted at %u", i);
        void_dispatch_add(recomp_dispatch_table[i].address, recomp_dispatch_table[i].func);
    }
    void_log("[void] dispatch: %u lifted entries", recomp_dispatch_count);
}

/* Synthetic VAs. Each thunk owns 16 bytes of a reserved, inaccessible range,
 * so a stray data access through one faults instead of reading garbage. */
#define MAX_THUNKS ((GUEST_THUNK_LIMIT - GUEST_THUNK_BASE) / 16u)
static const char **thunk_names;
static uint32_t thunk_count;

uint32_t void_thunk(void_fn fn, const char *name)
{
    if (!thunk_names) thunk_names = (const char **)calloc(MAX_THUNKS, sizeof *thunk_names);
    if (thunk_count >= MAX_THUNKS) void_fatal("thunk space exhausted");
    uint32_t va = GUEST_THUNK_BASE + 16u * thunk_count;
    thunk_names[thunk_count++] = name;
    void_dispatch_add(va, fn);
    return va;
}

const char *void_thunk_name(uint32_t va)
{
    if (va < GUEST_THUNK_BASE || va >= GUEST_THUNK_BASE + 16u * thunk_count || (va & 15u)) return NULL;
    return thunk_names[(va - GUEST_THUNK_BASE) / 16u];
}

/* x86 recomp_lookup*: the host code ported from Windows asks by kind. */
recomp_func_t recomp_lookup(uint32_t va)
{
    return in_image(va) ? (recomp_func_t)void_dispatch_find(va) : NULL;
}

recomp_func_t recomp_lookup_import(uint32_t va)
{
    return void_thunk_name(va) ? (recomp_func_t)void_dispatch_find(va) : NULL;
}

recomp_func_t recomp_lookup_manual(uint32_t va)
{
    (void)va;
    return NULL;
}

/* VOID_COUNT_THUNKS=1: count guest calls into each host thunk (Win32 and COM
 * methods all go through void_resolve) and log the busiest per frame. */
static int count_thunks = -1;
static uint32_t *thunk_calls;

void void_thunk_stats(uint32_t frames)
{
    if (count_thunks <= 0 || !thunk_calls || !frames) return;
    for (int rank = 0; rank < 12; ++rank) {
        uint32_t best = 0, best_i = 0;
        for (uint32_t i = 0; i < thunk_count; ++i) if (thunk_calls[i] > best) { best = thunk_calls[i]; best_i = i; }
        if (!best) break;
        void_log("[thunks] %8.1f/frame %s", (double)best / frames, thunk_names[best_i]);
        thunk_calls[best_i] = 0;
    }
    memset(thunk_calls, 0, thunk_count * sizeof *thunk_calls);
}

/* VOID_TRACE_CALLS=va[,va...] (hex, up to 8): indirect calls to these guest
 * functions log the object (ECX), the calling function and the EAX returned,
 * whenever the result for that object changes. Only the machine holder runs
 * guest code, so the bookkeeping needs no lock. */
#define TRACE_MAX 8
#define TRACE_OBJECTS 32
static int trace_n = -1;
static uint32_t trace_va[TRACE_MAX];
static void_fn trace_real[TRACE_MAX];
static struct { uint32_t ecx, eax, calls; } trace_obj[TRACE_MAX][TRACE_OBJECTS];

static void trace_call(int k)
{
    uint32_t ecx = g_ecx, caller = g_cur_func;
    trace_real[k]();
    uint32_t eax = g_eax;
    int i = 0;
    while (i < TRACE_OBJECTS - 1 && trace_obj[k][i].ecx && trace_obj[k][i].ecx != ecx) ++i;
    if (trace_obj[k][i].ecx != ecx || trace_obj[k][i].eax != eax || !trace_obj[k][i].calls) {
        char peek[80] = "";
        if (eax >= 0x10000u && void_vm_readable(eax, 32)) {
            /* What the result points at: printable bytes, '.' for the rest (UTF-16 shows as "n.a.m.e."). */
            size_t n = (size_t)snprintf(peek, sizeof peek, " [");
            for (uint32_t b = 0; b < 32 && n < sizeof peek - 2; ++b) {
                uint8_t c = MEM8(eax + b);
                peek[n++] = (char)(c >= 32 && c < 127 ? c : '.');
            }
            peek[n++] = ']';
            peek[n] = 0;
        }
        void_log("[trace] %08X(ecx=%08X) from %08X -> %08X after %u calls%s", trace_va[k], ecx, caller, eax,
            trace_obj[k][i].calls, peek);
        trace_obj[k][i].ecx = ecx;
        trace_obj[k][i].eax = eax;
        trace_obj[k][i].calls = 0;
    }
    ++trace_obj[k][i].calls;
}

#define TRACE_TRAMPOLINE(k) static void trace_trampoline##k(void) { trace_call(k); }
TRACE_TRAMPOLINE(0) TRACE_TRAMPOLINE(1) TRACE_TRAMPOLINE(2) TRACE_TRAMPOLINE(3)
TRACE_TRAMPOLINE(4) TRACE_TRAMPOLINE(5) TRACE_TRAMPOLINE(6) TRACE_TRAMPOLINE(7)
static const void_fn trace_trampolines[TRACE_MAX] = {
    trace_trampoline0, trace_trampoline1, trace_trampoline2, trace_trampoline3,
    trace_trampoline4, trace_trampoline5, trace_trampoline6, trace_trampoline7,
};

static void_fn trace_wrap(uint32_t va, void_fn fn)
{
    if (trace_n < 0) {
        trace_n = 0;
        const char *env = getenv("VOID_TRACE_CALLS");
        for (char *end; env && *env && trace_n < TRACE_MAX; env = *end ? end + 1 : end) {
            uint32_t v = (uint32_t)strtoul(env, &end, 16);
            if (end == env) break;
            trace_va[trace_n++] = v;
        }
    }
    for (int k = 0; k < trace_n; ++k) {
        if (trace_va[k] != va) continue;
        trace_real[k] = fn;
        return trace_trampolines[k];
    }
    return fn;
}

static __attribute__((noinline)) recomp_func_t resolve_slow(uint32_t va)
{
    void_module_prepare_call(va);
    if (__builtin_expect(count_thunks != 0, 0)) {
        if (count_thunks < 0) {
            const char *env = getenv("VOID_COUNT_THUNKS");
            count_thunks = env && *env == '1';
            if (count_thunks) thunk_calls = (uint32_t *)calloc(MAX_THUNKS, sizeof *thunk_calls);
        }
        if (count_thunks && va >= GUEST_THUNK_BASE && va < GUEST_THUNK_BASE + 16u * thunk_count)
            ++thunk_calls[(va - GUEST_THUNK_BASE) >> 4];
    }
    uint32_t slot = resolve_slot(va);
    if (__builtin_expect(resolve_cache[slot].va == va && va, 1)) return (recomp_func_t)resolve_cache[slot].fn;
    void_fn fn = void_dispatch_find(va);
    if (__builtin_expect(fn != NULL, 1)) {
        if (__builtin_expect(trace_n != 0, 0)) fn = trace_wrap(va, fn);
        resolve_cache[slot].fn = fn;
        resolve_cache[slot].va = va;
        return (recomp_func_t)fn;
    }
    if (va == RECOMP_RETADDR) void_lift_trap(va, "return-address marker used as a call target");
    if (va < 0x10000u) void_lift_trap(va, "null or low guest control-flow target");
    if (in_image(va)) void_lift_trap(va, "unresolved guest control-flow target (not lifted)");
    void_lift_trap(va, "control-flow target outside every lifted image and thunk");
    return NULL;
}

/* Keep the common cache hit free of the diagnostic/miss path's saved registers. */
recomp_func_t void_resolve(uint32_t va)
{
    if (__builtin_expect(count_thunks == 0, 1)) {
        uint32_t slot = resolve_slot(va);
        if (__builtin_expect(va && resolve_cache[slot].va == va, 1))
            return (recomp_func_t)resolve_cache[slot].fn;
    }
    return resolve_slow(va);
}

/* ---------------------------------------------------------------- traps */
static atomic_int dumping;

void void_dump_state(const char *why)
{
    void_log("[void] ===== state dump: %s =====", why ? why : "");
    void_log("[void] cur=%08X EAX=%08X ECX=%08X EDX=%08X EBX=%08X ESP=%08X EBP=%08X ESI=%08X EDI=%08X FS=%08X",
        g_cur_func, g_eax, g_ecx, g_edx, g_ebx, g_esp, g_ebp, g_esi, g_edi, g_fs_base);
    void_log("[void] x87 top=%d cw=%04X st0=%g st1=%g flags k=%u a=%08X b=%08X cf=%u",
        g_fp_top, g_fpu_cw, g_st[0], g_st[1], g_flag_k, g_flag_a, g_flag_b, g_flag_cf);
    if (void_vm_readable(g_esp, 64)) {
        void_log("[void] stack %08X: %08X %08X %08X %08X %08X %08X %08X %08X", g_esp,
            MEM32(g_esp), MEM32(g_esp + 4), MEM32(g_esp + 8), MEM32(g_esp + 12),
            MEM32(g_esp + 16), MEM32(g_esp + 20), MEM32(g_esp + 24), MEM32(g_esp + 28));
    }
    char line[1024];
    size_t used = 0;
    uint32_t end = g_enter_idx, count = end < 48 ? end : 48;
    used += (size_t)snprintf(line + used, sizeof line - used, "[void] last entries:");
    for (uint32_t i = end - count; i != end; ++i) {
        used += (size_t)snprintf(line + used, sizeof line - used, " %08X", g_enter_trace[i & (VOID_ENTER_RING - 1)]);
        if (used > sizeof line - 16) { void_log("%s", line); used = 0; line[0] = 0; }
    }
    if (used) void_log("%s", line);
    end = g_icall_trace_idx; count = end < 24 ? end : 24;
    used = 0;
    used += (size_t)snprintf(line + used, sizeof line - used, "[void] last icalls (from->to):");
    for (uint32_t i = end - count; i != end; ++i) {
        uint32_t k = i & (ICALL_TRACE_SIZE - 1);
        const char *name = void_thunk_name(g_icall_trace[k]);
        used += (size_t)snprintf(line + used, sizeof line - used, " %08X->%08X%s%s",
            g_icall_from[k], g_icall_trace[k], name ? ":" : "", name ? name : "");
        if (used > sizeof line - 80) { void_log("%s", line); used = 0; line[0] = 0; }
    }
    if (used) void_log("%s", line);
    void_log_flush();
}

static void log_code_address(const char *what, uintptr_t pc);

/* The lifted callers of a host trap: generated functions keep frame records. */
static void log_caller_backtrace(void)
{
#if defined(__aarch64__)
    uintptr_t fp = (uintptr_t)__builtin_frame_address(0), low = fp;
    for (int depth = 0; depth < 48 && fp >= low && fp - low < (96u << 20) && !(fp & 15); ++depth) {
        const uintptr_t *record = (const uintptr_t *)fp;
        uintptr_t next = record[0], ret = record[1];
        log_code_address("caller", ret);
        if (next <= fp) break;
        fp = next;
    }
#endif
}

_Noreturn void void_fatal(const char *fmt, ...)
{
    va_list ap;
    char message[1024];
    va_start(ap, fmt);
    vsnprintf(message, sizeof message, fmt, ap);
    va_end(ap);
    void_log("[void] FATAL: %s", message);
    if (!atomic_exchange(&dumping, 1)) {
        log_caller_backtrace();
        void_dump_state(message);
    }
    void_log_flush();
    _exit(70);
}

void void_lift_trap(uint32_t address, const char *instruction)
{
    const char *name = void_thunk_name(address);
    void_fatal("guest trap at %08X%s%s: %s (caller %08X)", address, name ? " " : "", name ? name : "",
        instruction, g_cur_func);
}

/* Validation builds (build_android.py --protocol-check) report each call site
 * whose callee changed a register its summary keeps, and each x87-rewritten
 * body that left the global stack window at the wrong depth; once each. */
static int seen_once(uint32_t key)
{
    static uint32_t seen[4096];
    static unsigned count;
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    int fresh = 1;
    pthread_mutex_lock(&lock);
    for (unsigned i = 0; i < count; ++i)
        if (seen[i] == key) { fresh = 0; break; }
    if (fresh && count < sizeof seen / sizeof seen[0]) seen[count++] = key;
    pthread_mutex_unlock(&lock);
    return fresh;
}

void recomp_protocol_violation(uint32_t site, uint32_t target, uint32_t regs)
{
    if (seen_once(site ^ (regs << 24)))
        void_log("[protocol] site %08X target %08X regs %02X", site, target, regs);
}

void recomp_x87_violation(uint32_t va, int found, int expected)
{
    if (seen_once(va ^ 0x80000000u))
        void_log("[x87] %08X net %d expected %d", va, found, expected);
}

/* A host code address as module+offset (llvm-symbolizer resolves it against
 * the unstripped libut3.so); production builds keep no guest call trace. */
static void log_code_address(const char *what, uintptr_t pc)
{
    Dl_info info;
    if (pc && dladdr((void *)pc, &info) && info.dli_fbase) {
        const char *slash = info.dli_fname ? strrchr(info.dli_fname, '/') : NULL;
        const char *module = slash ? slash + 1 : info.dli_fname ? info.dli_fname : "?";
        void_log("[void] %s %p = %s+0x%lx%s%s", what, (void *)pc, module,
                 (unsigned long)(pc - (uintptr_t)info.dli_fbase), info.dli_sname ? " " : "",
                 info.dli_sname ? info.dli_sname : "");
    } else {
        void_log("[void] %s %p", what, (void *)pc);
    }
}

static void log_host_backtrace(const void *context)
{
#if defined(__aarch64__)
    const ucontext_t *uc = (const ucontext_t *)context;
    if (!uc) return;
    uintptr_t sp = (uintptr_t)uc->uc_mcontext.sp, fp = (uintptr_t)uc->uc_mcontext.regs[29];
    log_code_address("pc", (uintptr_t)uc->uc_mcontext.pc);
    log_code_address("lr", (uintptr_t)uc->uc_mcontext.regs[30]);
    /* Frame records live on the faulting thread's stack, above its SP. */
    for (int depth = 0; depth < 48 && fp >= sp && fp - sp < (96u << 20) && !(fp & 15); ++depth) {
        const uintptr_t *record = (const uintptr_t *)fp;
        uintptr_t next = record[0], ret = record[1];
        log_code_address("frame", ret);
        if (next <= fp) break;
        fp = next;
    }
#else
    (void)context;
#endif
}

static void on_signal(int signo, siginfo_t *info, void *context)
{
    uintptr_t fault = (uintptr_t)info->si_addr;
    char where[96] = "";
    if (fault >= VOID_GUEST_BASE && fault < VOID_GUEST_BASE + VOID_GUEST_SPAN)
        snprintf(where, sizeof where, " (guest VA %08X)", (uint32_t)(fault - VOID_GUEST_BASE));
    void_log("[void] host signal %d code %d at %p%s", signo, info->si_code, info->si_addr, where);
    if (!atomic_exchange(&dumping, 1)) {
        log_host_backtrace(context);
        void_dump_state("host signal");
    }
    void_log_flush();
    signal(signo, SIG_DFL);
    raise(signo);
}

void void_install_fault_handlers(void)
{
    static char alt[256 * 1024];
    stack_t ss = { .ss_sp = alt, .ss_size = sizeof alt, .ss_flags = 0 };
    sigaltstack(&ss, NULL);
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_signal;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    int signals[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT };
    for (size_t i = 0; i < sizeof signals / sizeof signals[0]; ++i) sigaction(signals[i], &sa, NULL);
}

/* ---------------------------------------------------------------- CPUID */
/* Captured on the Windows PC the x86 build was validated on (Ryzen 7 5700X),
 * so the guest picks the same MMX/SSE/SSE2 library paths it ran there. */
static const uint32_t cpuid_table[][5] = {
    {0x00000000u, 0x00000010u, 0x68747541u, 0x444D4163u, 0x69746E65u},
    {0x00000001u, 0x00A20F12u, 0x08100800u, 0x7EF8320Bu, 0x178BFBFFu},
    {0x00000007u, 0x00000000u, 0x219C97A9u, 0x0040068Cu, 0x00000010u},
    {0x80000000u, 0x80000023u, 0x68747541u, 0x444D4163u, 0x69746E65u},
    {0x80000001u, 0x00A20F12u, 0x20000000u, 0x75C237FFu, 0x2FC3FBFFu},
    {0x80000002u, 0x20444D41u, 0x657A7952u, 0x2037206Eu, 0x30303735u},
    {0x80000003u, 0x2D382058u, 0x65726F43u, 0x6F725020u, 0x73736563u},
    {0x80000004u, 0x2020726Fu, 0x20202020u, 0x20202020u, 0x00202020u},
    {0x80000005u, 0xFF40FF40u, 0xFF40FF40u, 0x20080140u, 0x20080140u},
    {0x80000006u, 0x48002200u, 0x68004200u, 0x02006140u, 0x01009140u},
};

extern uint32_t void_guest_cpu_count(void);
extern uint32_t void_guest_cpu_id(void);
void void_cpuid(uint32_t leaf, uint32_t subleaf)
{
    (void)subleaf;
    if (leaf == 0x80000008u) {
        uint32_t count = void_guest_cpu_count();
        g_eax = 0x00002020u; /* Virtual 32-bit physical/linear address widths. */
        g_ebx = g_edx = 0;
        /* Legacy UE3 reads the entire ECX + 1 as its core count. A zero
         * APIC-width field also selects Valve's exact NC + 1 fallback. */
        g_ecx = count - 1;
        return;
    }
    for (size_t i = 0; i < sizeof cpuid_table / sizeof cpuid_table[0]; ++i) {
        if (cpuid_table[i][0] == leaf) {
            g_eax = cpuid_table[i][1]; g_ebx = cpuid_table[i][2];
            g_ecx = cpuid_table[i][3]; g_edx = cpuid_table[i][4];
            if (leaf == 1) {
                uint32_t count = void_guest_cpu_count();
                g_ebx = (g_ebx & 0x0000FFFFu) | (count << 16) | (void_guest_cpu_id() << 24);
                if (count == 1) g_edx &= ~(1u << 28);
            }
            return;
        }
    }
    g_eax = g_ebx = g_ecx = g_edx = 0;
}

uint64_t void_cmpxchg8b(uint32_t address, uint64_t expected, uint64_t replacement, uint32_t instruction)
{
    /* The caller owns the FIFO machine lock: no other guest load/store can run
     * during this instruction. Byte accesses also support unaligned and split
     * cache-line/page operands without ARM64's unaligned atomic fault. */
    if (!void_vm_readable(address, 8) || !void_vm_writable(address, 8))
        void_lift_trap(instruction, "CMPXCHG8B requires a readable/writable 8-byte operand");
    atomic_thread_fence(memory_order_seq_cst);
    volatile uint8_t *bytes = (volatile uint8_t *)GPTR(address);
    uint64_t old = 0;
    for (unsigned i = 0; i < 8; ++i) old |= (uint64_t)bytes[i] << (8 * i);
    uint64_t value = old == expected ? replacement : old;
    /* x86 performs a write cycle on failure too; validate all bytes before any write. */
    for (unsigned i = 0; i < 8; ++i) bytes[i] = (uint8_t)(value >> (8 * i));
    atomic_thread_fence(memory_order_seq_cst);
    return old;
}

void void_fpu_clear_exceptions(uint32_t address)
{
    /* FNCLEX clears IE..ES/SF and B; the existing condition/TOP encoding survives. */
    if (void_fpu_cmp >= 0x10000 && void_fpu_cmp <= 0x1FFFF)
        void_fpu_cmp &= ~0x80FF;
    else if (void_fpu_cmp < -1 || void_fpu_cmp > 2)
        void_lift_trap(address, "invalid x87 status encoding at FNCLEX");
    /* shortcut: arithmetic does not accumulate x87 sticky exceptions; extend producers and FNSTSW before claiming full exception status. */
}

/* ----------------------------------------------------------- FPREM/FPREM1 */
/* The x86 host executed the real instruction on the two doubles; here the
 * same double-model operation is computed exactly: the IEEE remainder of two
 * doubles is exact, and so is the partial-remainder step for exponent gaps
 * below 64, which is every case the Windows host accepted. */
void void_fpu_remainder(uint32_t address, int nearest)
{
    if (g_fp_top < 2 || g_fp_top > 8 || (nearest != 0 && nearest != 1))
        void_lift_trap(address, "unsupported FPREM stack/control word");
    double x = g_st[0], y = g_st[1];
    uint16_t status = 0;
    if (isnan(x) || isnan(y) || isinf(x) || y == 0.0) {
        /* Masked invalid operation: the x87 "real indefinite" QNaN. */
        uint64_t indefinite = 0xFFF8000000000000ull;
        memcpy(&g_st[0], &indefinite, sizeof indefinite);
        g_st_i64[0] = 0;
        void_fpu_cmp = (int)0x10000u;
        return;
    }
    if (isinf(y) || x == 0.0) {
        g_st_i64[0] = 0;          /* x unchanged, quotient 0 */
        void_fpu_cmp = (int)0x10000u;
        return;
    }
    int ex, ey;
    frexp(x, &ex); frexp(y, &ey);
    if (ex - ey < 64) {
        /* fmod and remainder are exact in IEEE arithmetic. The quotient's
         * low three bits (C0 C3 C1 = Q2 Q1 Q0) come from the exact truncated
         * remainder modulo 8|y|: r8 - rt = q8 * y with q8 in 0..7, so the
         * rounded division is within an ulp of a small integer. */
        double rt = fmod(x, y);
        double r8 = fmod(x, 8.0 * y);
        if (isinf(8.0 * y)) r8 = x;   /* |y| near DBL_MAX: |q| < 8 already */
        uint32_t q = (uint32_t)nearbyint(fabs((r8 - rt) / y)) & 7u;
        double r = rt;
        if (nearest) {
            double rn = remainder(x, y);
            if (rn != rt) q = (q + 1u) & 7u;  /* quotient rounded away from zero */
            r = rn;
        }
        if (q & 1u) status |= 0x0200u;   /* C1 = Q0 */
        if (q & 2u) status |= 0x4000u;   /* C3 = Q1 */
        if (q & 4u) status |= 0x0100u;   /* C0 = Q2 */
        g_st[0] = r;
    } else {
        /* Partial reduction (C2 = 1): reduce by y * 2^(D - N), N in 32..63,
         * exactly; callers loop until C2 clears, and the remainder they reach
         * is the same exact fmod/remainder whichever N the hardware used. */
        int n = 32 + ((ex - ey) & 31);
        g_st[0] = fmod(x, ldexp(y, ex - ey - n));
        status |= 0x0400u;
    }
    g_st_i64[0] = 0;
    void_fpu_cmp = (int)(0x10000u | status);
}
