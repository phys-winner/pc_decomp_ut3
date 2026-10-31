/* Original-oracle vectors and contention through the production guest machine lock. */
#define _GNU_SOURCE
#include "host.h"
#include "functions.h"
#include "../android/host/pe.c"
#include <assert.h>
#include <sched.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/wait.h>

uint64_t void_cmpxchg8b(uint32_t, uint64_t, uint64_t, uint32_t);
void void_process_exit(uint32_t code, int detach) { (void)detach; _exit((int)code); }

static const uint32_t data = 0x25000000;
static pthread_barrier_t barrier;
static uint32_t counter;
static unsigned failed;

static void *contend(void *unused)
{
    (void)unused;
    uint64_t expected;
    void_machine_enter(); memcpy(&expected, GPTR(counter), 8); void_machine_leave();
    pthread_barrier_wait(&barrier);
    for (unsigned iteration = 0; iteration < 2000; ++iteration) {
        for (;;) {
            assert((uint32_t)(expected >> 32) == ~(uint32_t)expected);
            uint32_t next = (uint32_t)expected + 1;
            uint64_t replacement = ((uint64_t)~next << 32) | next;
            void_machine_enter();
            uint64_t old = void_cmpxchg8b(counter, expected, replacement, 0x2100BC59);
            if (old != expected) ++failed;
            void_machine_leave();
            if (old == expected) { expected = replacement; break; }
            expected = old;
        }
        if (!(iteration & 31)) sched_yield();
    }
    return NULL;
}

static void aborted(int signal) { _exit(signal == SIGABRT ? 77 : 78); }

static void fault_checks(void)
{
    uint32_t base = void_vm_alloc(0, 0x2000, 0x3000, 4), old_protection;
    assert(base);
    /* Shared pages make any accidental partial write observable by the parent. */
    assert(mmap(GPTR(base), 0x2000, PROT_READ | PROT_WRITE,
                MAP_SHARED | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == GPTR(base));
    memset(GPTR(base), 0x5A, 0x2000);
    assert(void_vm_protect(base + 4096, 4096, 2, &old_protection));
    for (unsigned test = 0; test < 4; ++test) {
        pid_t child = fork(); assert(child >= 0);
        if (!child) {
            signal(SIGABRT, aborted); signal(SIGSEGV, aborted); signal(SIGBUS, aborted);
            freopen("/dev/null", "w", stderr);
            uint32_t address = test < 2 ? base + 4093 : test == 2 ? 0xFFFFFFFC : base + 0x3000;
            uint64_t expected = test == 1 ? 0 : 0x5A5A5A5A5A5A5A5Aull;
            void_cmpxchg8b(address, expected, 0x1122334455667788ull, 0x2100BC59);
            _exit(79);
        }
        int status; assert(waitpid(child, &status, 0) == child);
        assert(WIFEXITED(status) && WEXITSTATUS(status) == 70);
        for (unsigned i = 0; i < 8192; ++i) assert(MEM8(base + i) == 0x5A);
    }
    assert(void_vm_free(base, 0, 0x8000));
}

typedef struct {
    uint32_t form, address;
    uint64_t memory;
    uint32_t registers[8], flags, expected[8], expected_flags;
    uint64_t expected_memory;
} vector;
_Static_assert(sizeof(vector) == 96, "Python vector layout");

static void probe(unsigned form, uint32_t flags)
{
    uint32_t eax = g_eax, ebx = g_ebx, ecx = g_ecx, edx = g_edx;
    uint32_t esi = g_esi, edi = g_edi, ebp = g_ebp, esp = g_esp;
    uint32_t _cf = flags & 1, _flag_k = FK_EFLAGS, _flag_a = flags, _flag_b = 0;
    int _df = flags & 0x400 ? -1 : 1;
    switch (form) {
#include "probe.inc"
    default: assert(0);
    }
    g_eax = eax; g_ebx = ebx; g_ecx = ecx; g_edx = edx;
    g_esi = esi; g_edi = edi; g_ebp = ebp; g_esp = esp;
    g_flag_k = FK_EFLAGS; g_flag_a = recomp_eflags(_flag_k, _flag_a, _flag_b, _cf, _df);
    g_flag_b = 0; g_flag_cf = _cf;
}

int main(int argc, char **argv)
{
    assert(argc == 3 && void_guest_reserve());
    void_threads_init(); void_machine_enter(); void_dispatch_init();
    void_module *module = &modules[module_count++];
    strcpy(module->name, "tier0_s.dll"); strcpy(module->win_path, "C:\\UT3\\Binaries\\tier0_s.dll");
    module->kind = MOD_LIFTED_DLL;
    assert(map_image(module, argv[1], 0x21000000)); module->state = ST_READY;
    assert(void_vm_claim(data, 0x4000, 4));
    uint32_t stack = void_vm_alloc(0, 0x10000, 0x3000, 4) + 0x8000; assert(stack > 0x8000);
    FILE *file = fopen(argv[2], "rb"); assert(file);
    uint32_t count; assert(fread(&count, 4, 1, file) == 1);
    for (uint32_t index = 0; index < count; ++index) {
        vector row; assert(fread(&row, sizeof row, 1, file) == 1);
        memset(GPTR(data), 0xA5, 0x4000); memcpy(GPTR(row.address), &row.memory, 8);
        g_eax = row.registers[0]; g_ebx = row.registers[1]; g_ecx = row.registers[2]; g_edx = row.registers[3];
        g_esi = row.registers[4]; g_edi = row.registers[5]; g_ebp = row.registers[6]; g_esp = row.registers[7];
        g_flag_k = FK_EFLAGS; g_flag_a = row.flags; g_flag_b = 0; g_flag_cf = row.flags & 1;
        if (row.form == 100) {
            g_esp = stack; MEM32(stack) = 0xDEAD0000; MEM32(stack + 4) = row.address;
            MEM32(stack + 8) = g_ebx; MEM32(stack + 12) = g_ecx;
            MEM32(stack + 16) = g_eax; MEM32(stack + 20) = g_edx;
            sub_2100BC40(); row.expected[7] = stack + 4;
        } else probe(row.form, row.flags);
        uint32_t registers[] = {g_eax, g_ebx, g_ecx, g_edx, g_esi, g_edi, g_ebp, g_esp};
        uint32_t flags = recomp_eflags(g_flag_k, g_flag_a, g_flag_b, g_flag_cf, row.form == 100 ? 1 : row.flags & 0x400 ? -1 : 1);
        uint64_t memory; memcpy(&memory, GPTR(row.address), 8);
        if (memcmp(registers, row.expected, sizeof registers) || flags != row.expected_flags || memory != row.expected_memory) {
            fprintf(stderr, "CMPXCHG8B case%u form%u at%08X flags%08X/%08X memory%016llX/%016llX\n", index, row.form,
                    row.address, flags, row.expected_flags, (unsigned long long)memory, (unsigned long long)row.expected_memory);
            return 1;
        }
        for (unsigned i = 0; i < 0x4000; ++i)
            if (data + i < row.address || data + i >= row.address + 8) assert(MEM8(data + i) == 0xA5);
    }
    assert(fgetc(file) == EOF); fclose(file);
    fault_checks(); void_machine_leave();
    for (unsigned phase = 0; phase < 3; ++phase) {
        counter = data + (phase == 0 ? 0x1800 : phase == 1 ? 0x1801 : 4093); failed = 0;
        uint64_t initial = ((uint64_t)0xFu << 32) | 0xFFFFFFF0u;
        memcpy(GPTR(counter), &initial, 8);
        assert(!pthread_barrier_init(&barrier, NULL, 4));
        pthread_t workers[4];
        for (unsigned i = 0; i < 4; ++i) assert(!pthread_create(&workers[i], NULL, contend, NULL));
        for (unsigned i = 0; i < 4; ++i) assert(!pthread_join(workers[i], NULL));
        pthread_barrier_destroy(&barrier);
        uint32_t expected = 0xFFFFFFF0u + 8000;
        uint64_t value; memcpy(&value, GPTR(counter), 8);
        assert((uint32_t)value == expected && (uint32_t)(value >> 32) == ~expected && failed >= 3);
        printf("CMPXCHG8B contention PASS: phase%u 8000 increments, %u failed CAS retries\n", phase, failed);
    }
    printf("CMPXCHG8B production PASS: %u original-oracle vectors, flags/registers/unaligned/guards/RW faults and 24000 concurrent exchanges\n", count);
    return 0;
}
