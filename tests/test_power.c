/* Production API, guest module/proc lookup and the target's actual CPUFreq data. */
#include "host.h"
#include <errno.h>
static int deny_last_cpu;
static FILE *power_test_fopen(const char *path, const char *mode);
#define fopen power_test_fopen
#include "../android/host/power.c"
#undef fopen
#include "../android/host/pe.c"
#include <assert.h>

void void_process_exit(uint32_t code, int detach) { (void)detach; _exit((int)code); }

static FILE *power_test_fopen(const char *path, const char *mode)
{
    if (deny_last_cpu && strstr(path, "cpu7/cpufreq/")) { errno = EACCES; return NULL; }
    return fopen(path, mode);
}

static uint32_t call(uint32_t target, const uint32_t args[5])
{
    uint32_t before = g_esp;
    uint32_t result = void_call_guest(target, 5, args, 1);
    assert(g_esp == before);
    return result;
}

static uint32_t raw_mhz(uint32_t cpu, const char *attribute)
{
    char path[128];
    snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%u/cpufreq/%s", cpu, attribute);
    FILE *file = fopen(path, "r");
    if (!file) return 0;
    unsigned long khz = 0;
    assert(fscanf(file, "%lu", &khz) == 1);
    fclose(file);
    return (uint32_t)(khz / 1000);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    assert(void_guest_reserve());
    void_threads_init(); void_dispatch_init(); void_machine_enter(); add_system_pseudos();
    uint32_t module = void_module_load("POWRPROF.DLL");
    assert(module && void_module_handle("powrprof") == module);
    uint32_t name = void_gstrdup("CallNtPowerInformation");
    uint32_t target = void_module_proc(module, name);
    assert(target && void_dispatch_find(target));
    void_gfree(name);
    uint32_t info = void_galloc(36);
    const void_shim *system_info = void_shim_find("kernel32.dll", "GetSystemInfo", 0);
    assert(system_info);
    void_call_guest(void_thunk(system_info->fn, "GetSystemInfo"), 1, &info, 1);
    uint32_t cpus = MEM32(info + 20), required = cpus * 24;
    assert(cpus && cpus <= 32);
    void_gfree(info);
    uint32_t allocation = void_galloc(required + 32), out = allocation + 4;
    uint32_t args[5] = {11, 0, 0, out, required};
    const uint32_t short_sizes[] = {0, 1, 23, required - 1};
    for (unsigned i = 0; i < sizeof short_sizes / sizeof *short_sizes; ++i) {
        memset(GPTR(allocation), 0xA5, required + 32);
        args[4] = short_sizes[i];
        assert(call(target, args) == (args[4] ? STATUS_BUFFER_TOO_SMALL_ : STATUS_INVALID_PARAMETER_));
        for (uint32_t byte = 0; byte < required + 32; ++byte) assert(MEM8(allocation + byte) == 0xA5);
    }
    args[4] = required;
    args[1] = out; args[2] = 4;
    assert(call(target, args) == STATUS_INVALID_PARAMETER_);
    args[1] = args[2] = 0; args[3] = 0;
    assert(call(target, args) == STATUS_INVALID_PARAMETER_);
    args[3] = 0x1000;
    assert(call(target, args) == STATUS_ACCESS_VIOLATION_);
    args[3] = out; args[0] = 5;
    assert(call(target, args) == STATUS_NOT_SUPPORTED_);
    args[0] = 11; args[4] = required + 24;
    memset(GPTR(allocation), 0xA5, required + 32);
    uint32_t before_limit[32];
    for (uint32_t cpu = 0; cpu < cpus; ++cpu) before_limit[cpu] = raw_mhz(cpu, "scaling_max_freq");
    assert(!call(target, args));
    processor_power_information *rows = GPTR(out);
    for (uint32_t cpu = 0; cpu < cpus; ++cpu) {
        uint32_t maximum = raw_mhz(cpu, "cpuinfo_max_freq"), limit = raw_mhz(cpu, "scaling_max_freq");
        assert(rows[cpu].number == cpu && rows[cpu].max_mhz == maximum);
        assert(rows[cpu].current_mhz > 0 && rows[cpu].current_mhz <= maximum);
        assert(rows[cpu].mhz_limit > 0 && rows[cpu].mhz_limit <= maximum);
        if (limit == before_limit[cpu]) assert(rows[cpu].mhz_limit == limit);
        assert(!rows[cpu].max_idle_state && !rows[cpu].current_idle_state);
        printf("cpu%u MaxMHz=%u CurrentMHz=%u LimitMHz=%u idle=0/0\n", cpu,
               rows[cpu].max_mhz, rows[cpu].current_mhz, rows[cpu].mhz_limit);
    }
    assert(MEM32(allocation) == 0xA5A5A5A5);
    for (uint32_t byte = required; byte < required + 28; ++byte) assert(MEM8(out + byte) == 0xA5);
    args[1] = out; args[2] = 0; args[4] = required;
    assert(!call(target, args)); /* Native zero-length input is ignored. */
    args[1] = 0; args[2] = 1;
    assert(!call(target, args)); /* Native NULL input ignores its length. */
    assert(cpus == 8); /* This hardware calibration fixture targets the specified RMX3851. */
    puts("Injecting CPU7 EACCES to verify supported failure without partial output");
    deny_last_cpu = 1;
    memset(GPTR(allocation), 0xA5, required + 32);
    assert(call(target, args) == STATUS_NOT_SUPPORTED_);
    for (uint32_t byte = 0; byte < required + 32; ++byte) assert(MEM8(allocation + byte) == 0xA5);
    deny_last_cpu = 0;
    assert(!cpu_frequency_mhz(9999, "cpuinfo_max_freq"));
    void_gfree(allocation); void_machine_leave();
    puts("Power API PASS: level11 24-byte records, real CPUFreq MHz, errors/buffers, powrprof module/proc lookup, stdcall");
    return 0;
}
