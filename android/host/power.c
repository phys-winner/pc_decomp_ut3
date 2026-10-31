/* Required processor-power query, populated from the kernel's CPUFreq interface. */
#include "host.h"
#include <errno.h>
#include <ctype.h>
#include <unistd.h>

typedef struct {
    uint8_t ac, battery, percent, reserved;
    uint32_t lifetime, full_lifetime;
} system_power_status32;
_Static_assert(sizeof(system_power_status32) == 12, "SYSTEM_POWER_STATUS layout");
static pthread_mutex_t power_lock = PTHREAD_MUTEX_INITIALIZER;
static system_power_status32 android_power;
static int android_power_valid;

static system_power_status32 power_status_from_facts(int plugged, int present, int level, int scale, int status)
{
    /* Guest Windows6.2 uses Reserved1=0. Neither Android battery level nor
     * charging time predicts remaining/full discharge lifetime: keep -1. */
    system_power_status32 result = {255, 255, 255, 0, UINT32_MAX, UINT32_MAX};
    if (plugged >= 0) result.ac = plugged ? 1 : 0;
    if (!present) { result.battery = 128; return result; }
    if (level >= 0 && scale > 0 && level <= scale) result.percent = (uint8_t)((uint64_t)level * 100 / scale);
    if (result.percent <= 100 && status >= 2 && status <= 5) {
        result.battery = result.percent > 66 ? 1 : result.percent < 33 ? 2 : 0;
        if (result.percent < 5) result.battery |= 4;
        if (status == 2) result.battery |= 8; /* Android BATTERY_STATUS_CHARGING. */
    }
    return result;
}

/* Private JNI-to-host interface: Android platform facts, never shell data. */
void void_power_android_snapshot(int plugged, int present, int level, int scale, int status)
{
    system_power_status32 value = power_status_from_facts(plugged, present, level, scale, status);
    pthread_mutex_lock(&power_lock);
    android_power = value;
    android_power_valid = plugged >= 0 || present >= 0 || (level >= 0 && scale > 0);
    pthread_mutex_unlock(&power_lock);
}

static int power_supply_text(const char *supply, const char *name, char *text, size_t capacity)
{
    char path[160]; snprintf(path, sizeof path, "/sys/class/power_supply/%s/%s", supply, name);
    FILE *file = fopen(path, "r");
    if (!file) return errno == ENOENT || errno == ENOTDIR ? 0 : -1;
    int read = fgets(text, (int)capacity, file) != NULL; fclose(file);
    if (!read) return -1;
    text[strcspn(text, "\r\n")] = 0;
    return 1;
}

static int power_supply_integer(const char *supply, const char *name, int *value)
{
    char text[64], *end;
    int read = power_supply_text(supply, name, text, sizeof text);
    if (read != 1) return read;
    errno = 0; long parsed = strtol(text, &end, 10);
    if (end == text || errno || parsed < INT32_MIN || parsed > INT32_MAX) return -1;
    while (isspace((unsigned char)*end)) ++end;
    if (*end) return -1;
    *value = (int)parsed; return 1;
}

static system_power_status32 power_status_from_sysfs(void)
{
    static const char *supplies[] = {"usb", "ac", "mains", "wireless"};
    int plugged = -1, known = 0, unavailable = 0;
    for (unsigned i = 0; i < sizeof supplies / sizeof *supplies; ++i) {
        int online = 0, read = power_supply_integer(supplies[i], "online", &online);
        if (read < 0 || (read == 1 && online != 0 && online != 1)) unavailable = 1;
        if (read == 1 && (online == 0 || online == 1)) {
            known = 1; if (online) { plugged = 1; break; }
        }
    }
    if (plugged < 0 && known && !unavailable) plugged = 0;
    int present = -1, level = -1, status = 1, value;
    if (power_supply_integer("battery", "present", &value) == 1 && (value == 0 || value == 1)) present = value;
    if (power_supply_integer("battery", "capacity", &value) == 1 && value >= 0 && value <= 100) level = value;
    char text[64];
    if (power_supply_text("battery", "status", text, sizeof text) == 1) {
        if (!strcmp(text, "Charging")) status = 2;
        else if (!strcmp(text, "Discharging")) status = 3;
        else if (!strcmp(text, "Not charging")) status = 4;
        else if (!strcmp(text, "Full")) status = 5;
    }
    return power_status_from_facts(plugged, present, level, 100, status);
}

WINAPI_EXPORT("kernel32.dll", GetSystemPowerStatus, 1)
{
    uint32_t out = ARG(0);
    if (!out || !void_vm_writable(out, 12)) { void_set_last_error(998); return 0; }
    pthread_mutex_lock(&power_lock);
    int platform = android_power_valid;
    system_power_status32 status = android_power;
    pthread_mutex_unlock(&power_lock);
    if (!platform) status = power_status_from_sysfs();
    memcpy(GPTR(out), &status, sizeof status);
    static int logged;
    if (!__atomic_exchange_n(&logged, 1, __ATOMIC_RELAXED))
        void_log("[power] SYSTEM_POWER_STATUS source=%s AC=%u battery=%u percent=%u lifetime=%u/full=%u",
            platform ? "Android battery intent" : "sysfs/unknown", status.ac, status.battery, status.percent,
            status.lifetime, status.full_lifetime);
    return 1;
}

#define STATUS_INVALID_PARAMETER_ 0xC000000Du
#define STATUS_ACCESS_VIOLATION_ 0xC0000005u
#define STATUS_BUFFER_TOO_SMALL_ 0xC0000023u
#define STATUS_NOT_SUPPORTED_ 0xC00000BBu

typedef struct {
    uint32_t number, max_mhz, current_mhz, mhz_limit, max_idle_state, current_idle_state;
} processor_power_information;
_Static_assert(sizeof(processor_power_information) == 24, "PROCESSOR_POWER_INFORMATION layout");

static uint32_t cpu_frequency_mhz(uint32_t cpu, const char *attribute)
{
    char path[128];
    snprintf(path, sizeof path, "/sys/devices/system/cpu/cpu%u/cpufreq/%s", cpu, attribute);
    FILE *file = fopen(path, "r");
    if (!file) return 0;
    unsigned long long khz = 0;
    int read = fscanf(file, "%llu", &khz);
    fclose(file);
    if (read != 1 || khz < 1000 || khz / 1000 > UINT32_MAX) return 0;
    return (uint32_t)(khz / 1000);
}

WINAPI_EXPORT("powrprof.dll", CallNtPowerInformation, 5)
{
    if (ARG(0) != 11) return STATUS_NOT_SUPPORTED_;
    uint32_t out = ARG(3), length = ARG(4);
    if ((ARG(1) && ARG(2)) || !out || !length) return STATUS_INVALID_PARAMETER_;
    long cpus = sysconf(_SC_NPROCESSORS_CONF);
    if (cpus < 1) cpus = 1;
    if (cpus > 32) cpus = 32; /* Same guest processor count as GetSystemInfo. */
    uint32_t required = (uint32_t)cpus * sizeof(processor_power_information);
    if (length < required) return STATUS_BUFFER_TOO_SMALL_;
    if (!void_vm_writable(out, required)) return STATUS_ACCESS_VIOLATION_;
    processor_power_information rows[32];
    for (uint32_t cpu = 0; cpu < (uint32_t)cpus; ++cpu) {
        uint32_t maximum = cpu_frequency_mhz(cpu, "cpuinfo_max_freq");
        uint32_t current = cpu_frequency_mhz(cpu, "cpuinfo_cur_freq");
        if (!current) current = cpu_frequency_mhz(cpu, "scaling_cur_freq");
        uint32_t limit = cpu_frequency_mhz(cpu, "scaling_max_freq");
        if (!maximum || !current || !limit) {
            void_log("[power] CPU %u CPUFreq data unavailable (max=%u current=%u limit=%u); returning STATUS_NOT_SUPPORTED",
                     cpu, maximum, current, limit);
            return STATUS_NOT_SUPPORTED_;
        }
        /* Guest CPUs expose no ACPI idle-state model. */
        rows[cpu] = (processor_power_information){cpu, maximum, current, limit, 0, 0};
    }
    memcpy(GPTR(out), rows, required);
    return 0;
}
