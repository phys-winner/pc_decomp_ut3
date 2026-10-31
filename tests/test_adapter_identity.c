/* Synthetic IPv4 lists exercise the production GetAdaptersInfo only. */
#include "../android/host/threads.c"
#define getifaddrs fixture_getifaddrs
#define freeifaddrs fixture_freeifaddrs
#include "../android/host/network.c"
#undef getifaddrs
#undef freeifaddrs
#include <assert.h>

void void_process_exit(uint32_t code, int detach) { (void)detach; _exit((int)code); }
static unsigned interface_mode;
static uint32_t first_ip = 0x0a010203;
int fixture_getifaddrs(struct ifaddrs **out)
{
    static struct ifaddrs entries[3];
    static struct sockaddr_in addresses[3], masks[3];
    memset(entries, 0, sizeof entries); memset(addresses, 0, sizeof addresses); memset(masks, 0, sizeof masks);
    const char *names[] = {"lo", "wlan0", "eth0"};
    uint32_t ips[] = {0x7f000001, first_ip, 0xac100401};
    uint32_t netmasks[] = {0xff000000, 0xffffff00, 0xffff0000};
    unsigned count = interface_mode == 0 ? 3 : interface_mode == 1 ? 0 : 1;
    for (unsigned i = 0; i < count; ++i) {
        addresses[i].sin_family = masks[i].sin_family = AF_INET;
        addresses[i].sin_addr.s_addr = htonl(ips[i]); masks[i].sin_addr.s_addr = htonl(netmasks[i]);
        entries[i].ifa_name = (char *)names[i]; entries[i].ifa_flags = IFF_UP | (i ? IFF_BROADCAST : IFF_LOOPBACK);
        entries[i].ifa_addr = (struct sockaddr *)&addresses[i]; entries[i].ifa_netmask = (struct sockaddr *)&masks[i];
        entries[i].ifa_next = i + 1 < count ? &entries[i + 1] : NULL;
    }
    *out = count ? entries : NULL; return 0;
}
void fixture_freeifaddrs(struct ifaddrs *list) { (void)list; }

static uint32_t invoke(uint32_t info, uint32_t size)
{
    const void_shim *shim = void_shim_find("iphlpapi.dll", "GetAdaptersInfo", 0); assert(shim);
    uint32_t args[] = {info, size}, before = g_esp;
    uint32_t result = void_call_guest(void_thunk(shim->fn, "GetAdaptersInfo"), 2, args, 1);
    assert(g_esp == before); return result;
}
static void unchanged(uint32_t start, uint32_t bytes)
{ for (uint32_t i = 0; i < bytes; ++i) assert(MEM8(start + i) == 0xa5); }
static void online(uint32_t info, uint32_t size, int stable)
{
    memset(GPTR(info), 0xa5, 1296); MEM32(size) = 1280;
    assert(!invoke(info, size)); unchanged(info + 1280, 16);
    assert(MEM32(info) == info + 640 && !MEM32(info + 640));
    assert(MEM32(info + 400) == 6 && MEM32(info + 1040) == 6);
    assert(MEM32(info + 416) == 71 && MEM32(info + 1056) == 6);
    assert(MEM32(info + 420) == 1 && MEM32(info + 1060) == 1);
    struct in_addr address = {htonl(first_ip)}; char text[INET_ADDRSTRLEN];
    assert(inet_ntop(AF_INET, &address, text, sizeof text));
    assert(!strcmp((char *)GPTR(info + 432), text));
    assert(!strcmp((char *)GPTR(info + 448), "255.255.255.0"));
    assert(!strcmp((char *)GPTR(info + 1072), "172.16.4.1"));
    assert(!strcmp((char *)GPTR(info + 1088), "255.255.0.0"));
    const uint8_t fixed[] = {2, 0xaa, 0xbb, 0xcc, 0xdd, 0xee};
    uint8_t old[] = {2, 0x48, 0x35, first_ip >> 16, first_ip >> 8, first_ip};
    assert(!memcmp(GPTR(info + 404), stable ? fixed : old, 6));
    if (stable) { assert(!memcmp(GPTR(info + 1044), fixed, 5)); assert(MEM8(info + 1049) == 0xef); }
}
int main(void)
{
    alarm(10); assert(void_guest_reserve()); void_threads_init(); void_dispatch_init(); void_machine_enter();
    uint32_t info = void_galloc(1296), size = void_galloc(4); assert(info && size);
    unsetenv("UT3_ADAPTER_MAC"); online(info, size, 0);
    setenv("UT3_ADAPTER_MAC", "02AABBCCDDEE", 1); online(info, size, 1);
    first_ip = 0xc0a8649a; online(info, size, 1);
    unsetenv("UT3_ADAPTER_MAC"); online(info, size, 0);
    const char *invalid[] = {"", "02AABBCCDD", "02AABBCCDDEEFF", "02AABBCCDDEG", "01AABBCCDDEE", "00AABBCCDDEE", "03AABBCCDDEE", "02AABBCCDD+1"};
    for (unsigned i = 0; i < sizeof invalid / sizeof *invalid; ++i) {
        setenv("UT3_ADAPTER_MAC", invalid[i], 1); online(info, size, 0);
        interface_mode = 1; MEM32(size) = 0x1234;
        assert(invoke(info, size) == 232 && MEM32(size) == 0x1234); interface_mode = 0;
    }
    setenv("UT3_ADAPTER_MAC", "02aabbccddee", 1);
    MEM32(size) = 0; assert(invoke(0, size) == 111 && MEM32(size) == 1280);
    memset(GPTR(info), 0xa5, 1296); MEM32(size) = 1279;
    assert(invoke(info, size) == 111 && MEM32(size) == 1280); unchanged(info, 1296);
    assert(invoke(info, 0) == 87 && invoke(info, 1) == 87);
    MEM32(size) = 1280; assert(invoke(1, size) == 111 && MEM32(size) == 1280);
    for (interface_mode = 1; interface_mode <= 2; ++interface_mode) {
        MEM32(size) = 0; assert(invoke(0, size) == 111 && MEM32(size) == 640);
        memset(GPTR(info), 0xa5, 1296); MEM32(size) = 639;
        assert(invoke(info, size) == 111 && MEM32(size) == 640); unchanged(info, 1296);
        assert(!invoke(info, size)); unchanged(info + 640, 656);
        assert(!MEM32(info) && MEM32(info + 400) == 6 && MEM32(info + 416) == 6 && !MEM32(info + 420));
        assert(MEM32(info + 424) == info + 428);
        const uint8_t fixed[] = {2, 0xaa, 0xbb, 0xcc, 0xdd, 0xee}; assert(!memcmp(GPTR(info + 404), fixed, 6));
        assert(!strcmp((char *)GPTR(info + 432), "0.0.0.0") && !strcmp((char *)GPTR(info + 448), "0.0.0.0"));
    }
    setenv("UT3_ADAPTER_MAC", "aE1122334455", 1); uint8_t parsed[6];
    assert(adapter_identity(parsed) && parsed[0] == 0xae && parsed[5] == 0x55);
    void_machine_leave();
    puts("PASS production adapter identity: strict12hex/local-unicast, first MAC stable across IPv4 changes, real online addresses/masks/list, unique virtual records, offline record, old fallback, guarded sizing and 2arg stdcall; synthetic interfaces only");
    return 0;
}
