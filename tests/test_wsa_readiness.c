/* Real loopback transports through production WSA exports and event objects. */
#include "../android/host/network.c"
#include "../android/host/pe.c"
#include <assert.h>

void void_process_exit(uint32_t code, int detach) { (void)detach; _exit((int)code); }
static uint32_t scratch, list, result;
static uint32_t api(const char *name, unsigned count, const uint32_t *args)
{
    const void_shim *shim = void_shim_find("ws2_32.dll", name, 0); assert(shim);
    uint32_t before = g_esp, value = void_call_guest(void_thunk(shim->fn, name), count, args, 1);
    assert(before == g_esp); return value;
}
#define CALL(name, ...) api(name, sizeof((uint32_t[]){__VA_ARGS__}) / 4, (uint32_t[]){__VA_ARGS__})
static uint32_t event(void) { uint32_t h = api("WSACreateEvent", 0, NULL); assert(h); return h; }
static uint32_t socket_guest(int type) { uint32_t s = CALL("socket", 2, type, 0); assert(s != UINT32_MAX); return s; }
static uint32_t wait_event(uint32_t h, uint32_t timeout)
{ MEM32(list) = h; return CALL("WSAWaitForMultipleEvents", 1, list, 0, timeout, 0); }
static uint32_t events(uint32_t s, uint32_t h, int error_bit, uint32_t error)
{
    memset(GPTR(result - 4), 0x5A, 60);
    assert(!CALL("WSAEnumNetworkEvents", s, h, result));
    uint32_t mask = MEM32(result);
    assert(MEM32(result - 4) == 0x5A5A5A5A && MEM32(result + 44) == 0x5A5A5A5A);
    for (unsigned i = 0; i < 10; ++i)
        assert(MEM32(result + 4 + 4 * i) == ((mask & (1u << i)) ? ((int)i == error_bit ? error : 0) : 0x5A5A5A5A));
    return mask;
}
static struct sockaddr_in address_of(uint32_t s)
{
    struct sockaddr_in address; socklen_t length = sizeof address;
    assert(!getsockname(socket_fd(s, NULL), (struct sockaddr *)&address, &length)); return address;
}
static void bind_guest(uint32_t s)
{
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    memcpy(GPTR(scratch), &address, sizeof address); assert(!CALL("bind", s, scratch, 16));
}
static int host_socket(int type, int listening, struct sockaddr_in *address)
{
    int fd = socket(AF_INET, type, 0); assert(fd >= 0);
    struct sockaddr_in local = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    assert(!bind(fd, (struct sockaddr *)&local, sizeof local));
    if (listening) assert(!listen(fd, 8));
    socklen_t length = sizeof local; assert(!getsockname(fd, (struct sockaddr *)&local, &length));
    *address = local; return fd;
}
static void select_events(uint32_t s, uint32_t h, uint32_t mask)
{ assert(!CALL("WSAEventSelect", s, h, mask)); }
static void close_guest(uint32_t s) { assert(!CALL("closesocket", s)); }
static void close_event(uint32_t h) { assert(CALL("WSACloseEvent", h)); }

struct delayed_packet { int fd; struct sockaddr_in to; };
static void *delayed_send(void *argument)
{
    struct delayed_packet *packet = argument; usleep(45000);
    assert(sendto(packet->fd, "next", 4, 0, (struct sockaddr *)&packet->to, sizeof packet->to) == 4); return NULL;
}

static void udp_tests(void)
{
    uint32_t a = socket_guest(2), b = socket_guest(2), h = event(); bind_guest(a); bind_guest(b);
    struct sockaddr_in destination = address_of(a), other = address_of(b), local;
    int peer = host_socket(SOCK_DGRAM, 0, &local);
    select_events(a, h, NET_FD_READ | NET_FD_WRITE);
    assert(wait_event(h, 0) == 0 && events(a, h, -1, 0) == 2);
    uint64_t timeout_start = now_ms();
    assert(wait_event(h, 35) == WAIT_TIMEOUT_ && !events(a, h, -1, 0));
    assert(now_ms() - timeout_start >= 30 && now_ms() - timeout_start < 300);
    assert(sendto(peer, "abc", 3, 0, (struct sockaddr *)&destination, sizeof destination) == 3);
    assert(wait_event(h, 500) == 0 && events(a, h, -1, 0) == 1);
    assert(CALL("recv", a, scratch + 128, 16, 2) == 3); /* PEEK does not rearm native EventSelect. */
    assert(wait_event(h, 35) == WAIT_TIMEOUT_ && !events(a, h, -1, 0));
    assert(CALL("recv", a, scratch + 128, 1, 0) == UINT32_MAX && void_get_last_error() == 10040);
    assert(CALL("recv", a, scratch + 128, 16, 0) == UINT32_MAX && void_get_last_error() == 10035);
    struct delayed_packet packet = {peer, destination}; pthread_t worker;
    assert(!pthread_create(&worker, NULL, delayed_send, &packet));
    uint64_t before = now_ms(); assert(wait_event(h, UINT32_MAX) == 0); uint64_t duration = now_ms() - before;
    assert(duration >= 30 && duration < 450); pthread_join(worker, NULL);
    assert(events(a, h, -1, 0) == 1 && CALL("recv", a, scratch + 128, 16, 0) == 4);
    select_events(a, h, 1); select_events(b, h, 1);
    assert(sendto(peer, "A", 1, 0, (struct sockaddr *)&destination, sizeof destination) == 1);
    assert(sendto(peer, "B", 1, 0, (struct sockaddr *)&other, sizeof other) == 1);
    assert(wait_event(h, 500) == 0 && events(a, h, -1, 0) == 1);
    assert(wait_event(h, 0) == WAIT_TIMEOUT_); /* B pending does not re-set a shared reset event. */
    assert(events(b, h, -1, 0) == 1);
    assert(CALL("recv", a, scratch + 128, 16, 0) == 1 && CALL("recv", b, scratch + 128, 16, 0) == 1);
    assert(sendto(peer, "C", 1, 0, (struct sockaddr *)&destination, sizeof destination) == 1);
    assert(wait_event(h, 500) == 0);
    assert(CALL("WSAEnumNetworkEvents", a, h, 0x1000) == UINT32_MAX && void_get_last_error() == 10014);
    assert(wait_event(h, 0) == 0);
    assert(CALL("WSAEnumNetworkEvents", a, 0xDEAD, result) == UINT32_MAX && void_get_last_error() == 10038);
    assert(events(a, 0, -1, 0) == 1 && wait_event(h, 0) == 0);
    assert(!events(a, h, -1, 0) && wait_event(h, 0) == WAIT_TIMEOUT_);
    MEM32(scratch + 64) = 0;
    assert(CALL("ioctlsocket", a, 0x8004667E, scratch + 64) == UINT32_MAX && void_get_last_error() == 10022);
    assert(!CALL("WSAEventSelect", a, 0xDEAD, 0));
    assert(!CALL("ioctlsocket", a, 0x8004667E, scratch + 64));
    assert(!events(a, h, -1, 0));
    assert(CALL("WSAEventSelect", a, 0, 1) == UINT32_MAX && void_get_last_error() == 10022);
    assert(CALL("WSAEventSelect", a, h, 4) == UINT32_MAX && void_get_last_error() == 10045);
    assert(CALL("WSAEventSelect", 0x9999, h, 1) == UINT32_MAX && void_get_last_error() == 10038);
    close_event(h); assert(!sockets[slot_of(b)].event_mask); close_guest(a); close_guest(b); close(peer);
    puts("PASS WSA UDP: initial WRITE/latches/READ/PEEK/failed-recv rearm, delayed readiness, shared manual event/reset, cancellation, errors and 44-byte ABI guards");
}

static void tcp_tests(void)
{
    uint32_t listener = socket_guest(1), h = event(); bind_guest(listener);
    select_events(listener, h, 0x3B); assert(!CALL("listen", listener, 8));
    struct sockaddr_in destination = address_of(listener);
    int peer[2];
    for (unsigned i = 0; i < 2; ++i) {
        peer[i] = socket(AF_INET, SOCK_STREAM, 0); assert(peer[i] >= 0);
        assert(!connect(peer[i], (struct sockaddr *)&destination, sizeof destination));
    }
    assert(wait_event(h, 500) == 0 && events(listener, h, -1, 0) == NET_FD_ACCEPT);
    uint32_t accepted[2];
    for (unsigned i = 0; i < 2; ++i) {
        accepted[i] = CALL("accept", listener, 0, 0); assert(accepted[i] != UINT32_MAX);
        assert(sockets[slot_of(accepted[i])].nonblocking && sockets[slot_of(accepted[i])].event == h);
        assert(events(accepted[i], h, -1, 0) == NET_FD_WRITE);
        assert(CALL("recv", accepted[i], scratch + 128, 16, 0) == UINT32_MAX && void_get_last_error() == 10035);
        MEM32(scratch + 64) = 0;
        assert(CALL("ioctlsocket", accepted[i], 0x8004667E, scratch + 64) == UINT32_MAX && void_get_last_error() == 10022);
        if (!i) assert(events(listener, h, -1, 0) == NET_FD_ACCEPT);
    }
    assert(CALL("accept", listener, 0, 0) == UINT32_MAX && void_get_last_error() == 10035);
    assert(!events(listener, h, -1, 0));
    select_events(accepted[0], h, NET_FD_READ | NET_FD_CLOSE);
    assert(send(peer[0], "abcdef", 6, 0) == 6 && !shutdown(peer[0], SHUT_WR)); close(peer[0]);
    usleep(10000);
    assert(wait_event(h, 500) == 0 && events(accepted[0], h, -1, 0) == (NET_FD_READ | NET_FD_CLOSE));
    assert(CALL("recv", accepted[0], scratch + 128, 2, 0) == 2);
    assert(wait_event(h, 500) == 0 && events(accepted[0], h, -1, 0) == NET_FD_READ);
    assert(CALL("recv", accepted[0], scratch + 128, 16, 0) == 4 && !events(accepted[0], h, -1, 0));
    assert(!CALL("recv", accepted[0], scratch + 128, 16, 0) && !events(accepted[0], h, -1, 0));
    select_events(accepted[1], h, NET_FD_CLOSE);
    struct linger abortive = {1, 0}; assert(!setsockopt(peer[1], SOL_SOCKET, SO_LINGER, &abortive, sizeof abortive)); close(peer[1]);
    assert(wait_event(h, 500) == 0 && events(accepted[1], h, 5, 10054) == NET_FD_CLOSE);
    for (unsigned i = 0; i < 2; ++i) close_guest(accepted[i]); close_guest(listener); close_event(h);

    struct sockaddr_in address;
    int server = host_socket(SOCK_STREAM, 1, &address);
    uint32_t client = socket_guest(1); h = event(); select_events(client, h, 0x33);
    memcpy(GPTR(scratch), &address, 16);
    uint32_t connected = CALL("connect", client, scratch, 16);
    assert(!connected || (connected == UINT32_MAX && void_get_last_error() == 10035));
    assert(wait_event(h, 500) == 0 && events(client, h, -1, 0) == (NET_FD_CONNECT | NET_FD_WRITE));
    int remote = accept(server, NULL, NULL); assert(remote >= 0);
    select_events(client, h, NET_FD_CONNECT);
    assert(wait_event(h, 25) == WAIT_TIMEOUT_ && !events(client, h, -1, 0));
    /* Exhaust the real nonblocking send buffer, then drain the peer to rearm WRITE. */
    select_events(client, h, NET_FD_WRITE); assert(events(client, h, -1, 0) == NET_FD_WRITE);
    int buffer_size = 4096; assert(!setsockopt(socket_fd(client, NULL), SOL_SOCKET, SO_SNDBUF, &buffer_size, 4));
    memset(GPTR(scratch + 1024), 0x43, 16384);
    unsigned fills = 0;
    while (CALL("send", client, scratch + 1024, 16384, 0) != UINT32_MAX) assert(++fills < 4096);
    assert(void_get_last_error() == 10035 && (sockets[slot_of(client)].event_armed & NET_FD_WRITE));
    fcntl(remote, F_SETFL, fcntl(remote, F_GETFL) | O_NONBLOCK);
    char drain[32768];
    for (unsigned i = 0; i < 100; ++i) {
        while (recv(remote, drain, sizeof drain, 0) > 0) {}
        if (wait_event(h, 10) == 0) break;
    }
    assert(events(client, h, -1, 0) == NET_FD_WRITE && wait_event(h, 25) == WAIT_TIMEOUT_);
    close_guest(client); close_event(h); close(remote); close(server);

    /* Connect completion can precede the first-ever CONNECT registration. */
    server = host_socket(SOCK_STREAM, 1, &address); client = socket_guest(1); h = event();
    select_events(client, h, NET_FD_WRITE); memcpy(GPTR(scratch), &address, 16);
    connected = CALL("connect", client, scratch, 16); assert(!connected || void_get_last_error() == 10035);
    assert(wait_event(h, 500) == 0 && events(client, h, -1, 0) == NET_FD_WRITE);
    remote = accept(server, NULL, NULL); assert(remote >= 0);
    select_events(client, h, NET_FD_CONNECT);
    assert(wait_event(h, 0) == 0 && events(client, h, -1, 0) == NET_FD_CONNECT);
    close_guest(client); close_event(h); close(remote); close(server);

    int reserved = host_socket(SOCK_STREAM, 0, &address); /* Bound but not listening: deterministic refusal. */
    client = socket_guest(1); h = event(); select_events(client, h, NET_FD_CONNECT);
    memcpy(GPTR(scratch), &address, 16);
    assert(CALL("connect", client, scratch, 16) == UINT32_MAX && void_get_last_error() == 10035);
    assert(wait_event(h, 1000) == 0 && events(client, h, 4, 10061) == NET_FD_CONNECT);
    assert(!events(client, h, -1, 0));
    close_guest(client); close_event(h); close(reserved);
    puts("PASS WSA TCP: ACCEPT rearm/inherited selection and nonblocking, CONNECT+WRITE and one-shot history, graceful/abortive CLOSE/error indices, partial reads, EWOULDBLOCK WRITE rearm, refused connection");
}

int main(void)
{
    alarm(20); assert(void_guest_reserve()); void_threads_init(); void_dispatch_init(); void_machine_enter();
    scratch = void_galloc(65536); list = void_galloc(16); result = void_galloc(64) + 4; assert(scratch && list && result);
    assert(CALL("WSAEventSelect", 0, 0, 0) == UINT32_MAX && void_get_last_error() == WSA_NOTINITIALISED);
    assert(!CALL("WSAStartup", 0x202, scratch));
    udp_tests(); tcp_tests();
    for (unsigned i = 0; i < NET_SOCKETS; ++i) assert(!sockets[i].used);
    puts("PASS production WSAEventSelect/EnumNetworkEvents readiness: real loopback TCP/UDP, latched records, shared events, finite wait deadlines, guest stdcall and bounds");
    return 0;
}
