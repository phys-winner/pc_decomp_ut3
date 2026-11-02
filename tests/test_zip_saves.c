/* Real CreateDirectory/CreateFile/MoveFile and a fresh process over a stored ZIP mount. */
#include "../android/host/files.c"
#include <assert.h>

void void_process_exit(uint32_t code, int detach) { (void)detach; _exit((int)code); }
static uint32_t api(const char *name, unsigned count, const uint32_t *args)
{
    const void_shim *s = void_shim_find("kernel32.dll", name, 0); assert(s);
    uint32_t before = g_esp, result = void_call_guest(void_thunk(s->fn, name), count, args, 1);
    assert(before == g_esp); return result;
}
static uint32_t text(const char *s)
{
    uint32_t p = void_galloc((strlen(s) + 1) * 2); assert(p);
    void_utf8_to_gw(s, p, strlen(s) + 1); return p;
}
static void word(FILE *f, uint64_t value, unsigned width)
{
    for (unsigned i = 0; i < width; ++i) assert(fputc((int)(value >> (8 * i)) & 255, f) != EOF);
}
static const char original[] = "original archive";
static const char payload[] = "persistent UT3 save fixture";
static void make_fixture(const char *root)
{
    char path[1024]; snprintf(path, sizeof path, "%s/archive.bin", root);
    FILE *f = fopen(path, "wb"); assert(f);
    assert(fwrite(original, 1, sizeof original, f) == sizeof original); assert(!fclose(f));
    snprintf(path, sizeof path, "%s/index.zmi", root); f = fopen(path, "wb"); assert(f);
    assert(fwrite("ZMI1", 1, 4, f) == 4); word(f, 1, 4); word(f, sizeof original, 8);
    for (unsigned i = 0; i < 32; ++i) word(f, 0, 1);
    const char name[] = "UTGame/CookedPC/Maps/fixture.ut3";
    word(f, 0, 8); word(f, sizeof original, 8); word(f, 0, 4); word(f, 0, 4); word(f, 0, 8); word(f, strlen(name), 2);
    assert(fwrite(name, 1, strlen(name), f) == strlen(name)); assert(!fclose(f));
}
static void check_read(const char *path, uint32_t memory)
{
    uint32_t args[7] = {text(path), GENERIC_READ_, 7, 0, OPEN_EXISTING_, 0, 0};
    uint32_t h = api("CreateFileW", 7, args); assert(h != WIN_INVALID_HANDLE);
    memset(GPTR(memory), 0, 128);
    uint32_t read[5] = {h, memory, sizeof payload, memory + 128, 0};
    assert(api("ReadFile", 5, read) && MEM32(memory + 128) == sizeof payload && !memcmp(GPTR(memory), payload, sizeof payload));
    assert(api("CloseHandle", 1, &h));
}
int main(int argc, char **argv)
{
    char temporary[] = "/data/local/tmp/ut3-zip-save-XXXXXX";
    int verify = argc == 3 && !strcmp(argv[1], "--verify");
    const char *root = verify ? argv[2] : mkdtemp(temporary); assert(root);
    assert(!strncmp(root, "/data/local/tmp/ut3-zip-save-", sizeof("/data/local/tmp/ut3-zip-save-") - 1));
    if (!verify) make_fixture(root);
    char game[1024], data[1024], index[1024], archive[1024], number[32];
    snprintf(game, sizeof game, "%s/game", root); snprintf(data, sizeof data, "%s/userdata", root);
    snprintf(index, sizeof index, "%s/index.zmi", root); snprintf(archive, sizeof archive, "%s/archive.bin", root);
    int fd = open(archive, O_RDONLY); assert(fd >= 0); snprintf(number, sizeof number, "%d", fd);
    assert(!setenv("PORT_MOUNT_GAME", index, 1) && !setenv("PORT_MOUNT_FD_GAME", number, 1) && !setenv("PORT_PART_GAME", game, 1));
    assert(void_guest_reserve()); void_threads_init(); void_dispatch_init(); void_machine_enter();
    assert(void_files_init(game, data)); void_files_std_init();
    uint32_t memory = void_galloc(256); assert(memory);
    if (!verify) {
        uint32_t virtual_dir = text("C:\\Games\\UT3\\UTGame"), create[2] = {virtual_dir, 0};
        assert(api("GetFileAttributesW", 1, &virtual_dir) & 0x10u);
        assert(!api("CreateDirectoryW", 2, create) && void_get_last_error() == ERROR_ALREADY_EXISTS_);
        create[0] = text("C:\\Games\\UT3\\UTGame\\SaveData");
        assert(api("CreateDirectoryW", 2, create));
        create[0] = text("C:\\Games\\UT3\\Missing\\Child");
        assert(!api("CreateDirectoryW", 2, create) && void_get_last_error() == ERROR_PATH_NOT_FOUND_);
        for (unsigned i = 0; i < 2; ++i) {
            uint32_t temporary_name = text("C:\\Games\\UT3\\UTGame\\SaveData\\save.tmp");
            uint32_t args[7] = {temporary_name, GENERIC_WRITE_, 7, 0, CREATE_ALWAYS_, 0, 0};
            uint32_t h = api("CreateFileW", 7, args); assert(h != WIN_INVALID_HANDLE);
            memcpy(GPTR(memory), payload, sizeof payload);
            uint32_t write[5] = {h, memory, sizeof payload, memory + 128, 0};
            assert(api("WriteFile", 5, write) && MEM32(memory + 128) == sizeof payload);
            assert(api("FlushFileBuffers", 1, &h) && api("CloseHandle", 1, &h));
            uint32_t move[2] = {temporary_name, text(i ? "C:\\Games\\UT3\\UTGame\\SaveData\\campaign.uss" : "C:\\Games\\UT3\\UTGame\\SaveData\\player.ue3profile")};
            assert(api("MoveFileW", 2, move));
        }
        uint32_t copy[3] = {text("C:\\Games\\UT3\\UTGame\\SaveData\\player.ue3profile"), text("C:\\Games\\UT3\\UTGame\\CookedPC\\Maps\\profile.backup"), 1};
        assert(api("CopyFileW", 3, copy));
    }
    check_read("c:\\games\\ut3\\utgame\\savedata\\PLAYER.ue3profile", memory);
    check_read("C:\\Games\\UT3\\UTGame\\SaveData\\campaign.uss", memory);
    char raw[sizeof original]; assert(pread(fd, raw, sizeof raw, 0) == sizeof raw && !memcmp(raw, original, sizeof raw));
    printf("FIXTURE_ROOT=%s\n", root);
    puts(verify ? "PASS ZIP saves persist across a fresh process, case-insensitive profile/campaign reads, original archive unchanged" : "PASS ZIP virtual parents: directory creation, profile/campaign write/flush/rename/read and copy");
    return 0;
}
