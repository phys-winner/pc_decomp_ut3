/* Three rendering policies on the same real Vulkan device and query objects.
 * Only the isolated TU_DEBUG_FILE changes; no app setting or driver is replaced. */
#define main unused_bridge_fixture_main
#include "test_dxvk_bridge.cpp"
#undef main
#include <thread>
#include <chrono>
#include <sys/uio.h>
#include <unistd.h>

static const char *mode_file;
static const char *modes[] = {"", "sysmem", "gmem"};
static void driver_state(const char *policy)
{
    FILE *maps = fopen("/proc/self/maps", "r"); assert(maps);
    char line[1024]; unsigned long long start, end, offset; char permissions[8];
    uintptr_t base = 0;
    while (fgets(line, sizeof line, maps))
        if (strstr(line, "/libut3_turnip.so") &&
            sscanf(line, "%llx-%llx %7s %llx", &start, &end, permissions, &offset) == 4 && !offset) {
            base = uintptr_t(start); break;
        }
    fclose(maps);
    if (!base) { printf("DRIVER_STATE policy=%s not-loaded\n", policy); return; }
    /* Read-only self introspection, bound to pinned fdd37852... driver ELF:
     * llvm-nm: tu_env.0 cf7638/8 bytes; tu_env.1 cf7640/8 bytes. */
    uint64_t words[2]{};
    iovec local{words, sizeof words}, remote{reinterpret_cast<void*>(base + 0xcf7638), sizeof words};
    assert(process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == sizeof words);
    printf("DRIVER_STATE policy=%s base=%llx tu_env0=%016llx tu_env1=%016llx\n", policy,
           static_cast<unsigned long long>(base), static_cast<unsigned long long>(words[0]), static_cast<unsigned long long>(words[1]));
}
static void change_mode(unsigned index)
{
    assert(index < 3 && mode_file);
    FILE *file = fopen(mode_file, "wb"); assert(file);
    assert(fwrite(modes[index], 1, strlen(modes[index]), file) == strlen(modes[index]));
    assert(!fclose(file));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    driver_state(*modes[index] ? modes[index] : "automatic");
    printf("MODE same-device policy=%s file=%s\n", *modes[index] ? modes[index] : "automatic", mode_file);
    fflush(stdout);
}
static HRESULT mode_bridge_getdata(IDirect3DQuery9 *query, void *out, DWORD bytes, DWORD flags)
{
    assert(bytes == 4 && out);
    constexpr uint32_t guest = 0x3000;
    uint32_t args[] = {guest, bytes, flags};
    memcpy(memory<uint8_t>(guest, bytes, true), out, bytes);
    uint32_t before = *memory<uint32_t>(guest);
    uint32_t hr = ut3_dxvk_native_call(unsigned(Kind::IDirect3DQuery9), query, 7, args);
    if (hr == S_FALSE) assert(*memory<uint32_t>(guest) == before);
    memcpy(out, memory<uint8_t>(guest, bytes), bytes);
    return HRESULT(hr);
}

#define UT3_ORACLE_QUERY_GETDATA mode_bridge_getdata
#define UT3_ORACLE_CYCLES 3
#define UT3_ORACLE_GENERATIONS 6
#define UT3_ORACLE_CYCLE_BEGIN(cycle) change_mode(cycle)
#define UT3_ORACLE_BATCH_BEGIN(generation) change_mode((generation)/2)
#define main same_device_oracle_main
#include "test_dxvk_occlusion_gpu.cpp"
#undef main

int main()
{
    mode_file = getenv("TU_DEBUG_FILE");
    if (!mode_file || strncmp(mode_file, "/data/local/tmp/ut3-turnip/", 27)) {
        puts("FAIL test requires its own TU_DEBUG_FILE in isolated /data/local/tmp/ut3-turnip/");
        return 2;
    }
    unsetenv("TU_DEBUG");
    change_mode(0);
    int result = same_device_oracle_main();
    change_mode(0);
    printf("%s same-device Turnip file-mode oracle: automatic/sysmem/gmem, native bridge real samples and readback; no Surface/Present/game\n", result ? "FAIL" : "PASS");
    return result;
}
