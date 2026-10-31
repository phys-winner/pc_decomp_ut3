/* Real FArchiveAsync::Precache pending results may outlive a guest time slice.
 * Wait after the original call, with no native guest pointer retained, so
 * async workers can run without the linker burning a whole CPU on polling. */
#define _GNU_SOURCE
#include "host.h"
#include <errno.h>
#include <time.h>

#define ASYNC_PRECACHE_VA 0x00BAD370u
/* Complete original function, including RET 8. Original UT3.exe SHA256:
 * 097518615da0fe4ed73f104186121a58cd69ff30be1007e770fa2df0473cd70f.
 * Module/input integrity is checked by the existing image build receipts;
 * this additional check prevents the optimization binding another body. */
static const uint8_t precache_preimage[] = {
    0x55,0x56,0x8b,0xf1,0x8b,0x86,0xa4,0x00,0x00,0x00,0x8b,0x96,0xa8,0x00,0x00,0x00,
    0x33,0xc9,0x8d,0xae,0xa4,0x00,0x00,0x00,0x85,0xc0,0x0f,0x94,0xc1,0x57,0x8b,0x7c,
    0x24,0x10,0x8b,0xc1,0x33,0xc9,0x85,0xd2,0x0f,0x94,0xc1,0x3b,0xbe,0x8c,0x00,0x00,
    0x00,0x7c,0x12,0x8b,0x54,0x24,0x14,0x03,0xd7,0x3b,0x96,0x94,0x00,0x00,0x00,0x0f,
    0x8e,0x58,0x01,0x00,0x00,0x85,0xc0,0x0f,0x84,0x4e,0x01,0x00,0x00,0x85,0xc9,0x0f,
    0x84,0x46,0x01,0x00,0x00,0x83,0xbe,0xac,0x00,0x00,0x00,0x00,0x53,0x74,0x63,0x8b,
    0xce,0xe8,0x0a,0xfd,0xff,0xff,0x3b,0xbe,0x8c,0x00,0x00,0x00,0x7c,0x16,0x8b,0x44,
    0x24,0x18,0x8d,0x0c,0x07,0x3b,0x8e,0x94,0x00,0x00,0x00,0x7f,0x07,0xbb,0x01,0x00,
    0x00,0x00,0xeb,0x02,0x33,0xdb,0x57,0x8b,0xce,0xe8,0xa2,0xfd,0xff,0xff,0x85,0xdb,
    0x8b,0xf8,0x75,0x09,0x53,0x57,0x8b,0xce,0xe8,0x13,0xfe,0xff,0xff,0x8b,0x96,0xac,
    0x00,0x00,0x00,0x8d,0x47,0x01,0x3b,0x42,0x04,0x0f,0x8d,0xeb,0x00,0x00,0x00,0x6a,
    0x01,0x50,0x8b,0xce,0xe8,0xf7,0xfd,0xff,0xff,0x5b,0x5f,0x5e,0x33,0xc0,0x5d,0xc2,
    0x08,0x00,0x8b,0x0d,0x30,0xdd,0xdb,0x01,0x8b,0x01,0x8b,0x50,0x04,0x6a,0x00,0xff,
    0xd2,0x8b,0xd8,0x85,0xdb,0x75,0x1c,0x68,0x4c,0x0a,0x98,0x01,0x68,0x55,0x06,0x00,
    0x00,0x68,0x50,0x0a,0x98,0x01,0x68,0x6c,0x0a,0x98,0x01,0xe8,0x20,0x7f,0xfa,0xff,
    0x83,0xc4,0x10,0x8b,0x44,0x24,0x18,0x3d,0x00,0x00,0x02,0x00,0x89,0xbe,0x8c,0x00,
    0x00,0x00,0x7d,0x05,0xb8,0x00,0x00,0x02,0x00,0x8b,0x8e,0x80,0x00,0x00,0x00,0x03,
    0xc7,0x3b,0xc1,0x89,0x86,0x94,0x00,0x00,0x00,0x7e,0x02,0x8b,0xc1,0x89,0x86,0x94,
    0x00,0x00,0x00,0x8b,0x86,0x9c,0x00,0x00,0x00,0x50,0xe8,0xb1,0x45,0x85,0xff,0x8b,
    0x8e,0x94,0x00,0x00,0x00,0x2b,0x8e,0x8c,0x00,0x00,0x00,0x6a,0x08,0x51,0xe8,0x3d,
    0x45,0x85,0xff,0x83,0xc4,0x0c,0x55,0x89,0x86,0x9c,0x00,0x00,0x00,0xff,0x15,0x24,
    0x91,0x72,0x01,0x8b,0x8e,0x9c,0x00,0x00,0x00,0x8b,0x86,0x8c,0x00,0x00,0x00,0x8b,
    0x13,0x8b,0x52,0x08,0x6a,0x02,0x55,0x51,0x8b,0x8e,0x94,0x00,0x00,0x00,0x2b,0xc8,
    0x51,0x50,0x83,0xc6,0x74,0x56,0x8b,0xcb,0xff,0xd2,0x0b,0xc2,0x75,0x1c,0x68,0x9c,
    0x0a,0x98,0x01,0x68,0x6b,0x06,0x00,0x00,0x68,0xa0,0x0a,0x98,0x01,0x68,0xbc,0x0a,
    0x98,0x01,0xe8,0x79,0x7e,0xfa,0xff,0x83,0xc4,0x10,0x5b,0x33,0xc0,0x5f,0x5e,0x5d,
    0xc2,0x08,0x00,
};
static void_fn precache_original;
static unsigned precache_wait_us;
static void movie_service(int loading);

static void precache_cooperative(void)
{
    static _Thread_local unsigned pending;
    precache_original();
    movie_service(1);
    if (g_eax & 0xffu) { pending = 0; return; }
    if (pending < 8) { ++pending; return; }
    if (!precache_wait_us) return;
    struct timespec delay = { 0, (long)precache_wait_us * 1000L };
    /* Save/restore the complete post-call guest register file. The original
     * AL/EAX, flags, stack cleanup and archive/output bytes remain unchanged. */
    void_blocking_begin();
    while (nanosleep(&delay, &delay) && errno == EINTR) {}
    void_blocking_end();
}

/* Full-screen Bink movies under -onethread. FFullScreenMovieBink::GameThreadPlayMovie
 * (00C18E5A) starts UE3's rendering thread for every movie when the engine runs
 * without one, so a movie keeps playing through a blocking load; the GL context
 * is current only on the game thread (d3d9_gl.c), so rendering stays there.
 * FEngineLoop::Tick then ticks the movie (00407F06), StopMovie's
 * StopRenderingThread returns at once (00C12C63) and StopMovie restores
 * GUseThreadedRendering from the movie (00C19253). Linked with
 * -Wl,--wrap=sub_00C12290 (StartRenderingThread). */
#define GIS_THREADED_RENDERING 0x01DEB3ECu
#define GUSE_THREADED_RENDERING 0x01DEB3F0u
extern void __real_sub_00C12290(void);
static int one_thread = -1;

static int one_thread_rendering(void)
{
    if (one_thread < 0) {
        const char *line = getenv("UT3_COMMAND_LINE"), *p = line;
        one_thread = 0;
        while (p && (p = strcasestr(p, "-onethread"))) {
            if ((p == line || p[-1] == ' ') && (!p[10] || p[10] == ' ')) { one_thread = 1; break; }
            p += 10;
        }
    }
    return one_thread;
}

void __wrap_sub_00C12290(void)
{
    if (!one_thread_rendering() || MEM32(GIS_THREADED_RENDERING)) { __real_sub_00C12290(); return; }
    static int logged;
    if (!logged) { logged = 1; void_log("[loading] -onethread: no rendering thread for movies"); }
    MEM32(GUSE_THREADED_RENDERING) = 0;
    g_esp += 4;   /* RET */
}

/* FFullScreenMovieBink::GameThreadWaitForMovie (00C12E20) waits on the movie's
 * finish event and pumps messages while the rendering thread ticks the movie.
 * Without that thread nothing ticks it: FEngineLoop::Init's StopMovie(0, TRUE,
 * FALSE) after the startup movies (004079A4) would wait for ever. Tick the
 * rendering tickables here as FEngineLoop::Tick does (00407F06-00407F10). */
#define MOVIE_WAIT_VA 0x00C12E20u
#define RENDER_TICKABLES_VA 0x00C120F0u
#define PUMP_MESSAGES_VA 0x00B6A8D0u
#define GUSE_WXWINDOWS 0x01D46AA0u
#define RENDER_TICKED 0x01D0B4E8u
static const uint8_t movie_wait_preimage[] = {
    0x56,0x8b,0xf1,0x8b,0x4e,0x08,0x8b,0x01,0x8b,0x50,0x14,0x6a,0x01,0xff,0xd2,0x85,
    0xc0,0x75,0x1e,0x83,0x3d,0xa0,0x6a,0xd4,0x01,0x00,0x75,0x05,0xe8,0x8f,0x7a,0xf5,
    0xff,0x8b,0x4e,0x08,0x8b,0x01,0x8b,0x50,0x14,0x6a,0x01,0xff,0xd2,0x85,0xc0,0x74,
    0xe2,0x5e,0xc3,
};
static const uint8_t render_tickables_preimage[] = {
    0x83,0xec,0x10,0xf6,0x05,0x48,0xb4,0xde,0x01,0x01,0x56,0x8b,0x35,0x20,0x91,0x72,
    0x01,0x57,0x75,0x32,0x83,0x0d,0x48,0xb4,0xde,0x01,0x01,
};
static const uint8_t pump_messages_preimage[] = {
    0x83,0xec,0x1c,0x56,0x8b,0x35,0xd4,0x99,0x72,0x01,0x6a,0x01,0x6a,0x00,0x6a,0x00,
    0x6a,0x00,0x8d,0x44,0x24,0x14,0x50,0xff,0xd6,
};
static void_fn movie_wait_original;

/* A tap or a pad button skips the movie being waited for: the player's own
 * FViewportClient::InputKey (00C18AC0, slot 4 of the client at this + 4) gets
 * Escape, one of its skip keys. It reads only the key and the event, and leaves
 * a movie outside SkippableMovies playing; unlike a posted Escape, nothing
 * reaches the game's viewport client. */
#define MOVIE_INPUT_KEY_VA 0x00C18AC0u
#define KEY_ESCAPE 0x01DEC714u           /* FName KEY_Escape */
static const uint8_t movie_input_key_preimage[] = {
    0x6a,0xff,0x68,0x08,0x8b,0x66,0x01,0x64,0xa1,0x00,0x00,0x00,0x00,0x50,0x83,0xec,
    0x0c,0x56,0x57,0xa1,0xe0,0x24,0xd3,0x01,0x33,0xc4,0x50,0x8d,0x44,0x24,0x18,0x64,
    0xa3,0x00,0x00,0x00,0x00,0x8b,0xf1,0x33,0xff,0x39,0x7e,0x48,0x74,0x14,0x8b,0x4e,
};
static int movie_skip_ready;
extern uint32_t void_input_movie_skips(void);

static int original_code(uint32_t va, const uint8_t *bytes, size_t size)
{
    return void_vm_readable(va, (uint32_t)size) && !memcmp(GPTR(va), bytes, size);
}

static void movie_skip(uint32_t movie)
{
    uint32_t client = movie + 4;
    if (!movie_skip_ready || !void_vm_readable(client, 4) || !void_vm_readable(MEM32(client) + 0x10, 4) ||
        MEM32(MEM32(client) + 0x10) != MOVIE_INPUT_KEY_VA) return;
    /* Viewport, ControllerId, Key, IE_Pressed, AmountDepressed 1.0, bGamepad. The
     * player's skip command (00C18A00) acts on a movie its PlayMovie marked
     * skippable at +54 (00C181FB). */
    uint32_t args[7] = { 0, 0, MEM32(KEY_ESCAPE), MEM32(KEY_ESCAPE + 4), 0, 0x3F800000u, 0 };
    uint32_t skippable = void_vm_readable(movie + 0x54, 4) ? MEM32(movie + 0x54) : 0;
    uint32_t taken = void_call_guest_this(MOVIE_INPUT_KEY_VA, client, 7, args);
    void_log("[loading] movie skip request: skippable %u, InputKey %u", skippable, taken);
}

/* The wait's message pump reaches movie_service through the input pump, which
 * answers skip requests there as everywhere else. */
static void movie_wait_ticking(void)
{
    uint32_t self = g_ecx, one = 1;
    while (!MEM32(GIS_THREADED_RENDERING) && void_vm_readable(self + 8, 4)) {
        uint32_t event = MEM32(self + 8);
        if (!event || !void_vm_readable(event, 4) || !void_vm_readable(MEM32(event) + 0x14, 4)) break;
        if (void_call_guest_this(MEM32(MEM32(event) + 0x14), event, 1, &one)) break;   /* FEvent::Wait(1) */
        if (!MEM32(GUSE_WXWINDOWS)) void_call_guest(PUMP_MESSAGES_VA, 0, NULL, 0);
        MEM32(RENDER_TICKED) = 1;
        void_call_guest(RENDER_TICKABLES_VA, 0, NULL, 0);
    }
    g_ecx = self;
    movie_wait_original();
}

/* Movies outside that wait. A loading movie (UGameEngine::LoadMap 0102D4BA)
 * plays while the game thread loads the map in one call, and the original
 * rendering thread keeps ticking it meanwhile; here the game thread ticks the
 * rendering tickables itself from FArchiveAsync::Precache (BAD370), called for
 * every export the linkers serialize and in their busy wait for reads. That is
 * linker code, never inside a render command, a guest lock or the movie's own
 * code, and only once the frame loop has not ticked the movie for a frame:
 * BinkWait (00C183BD) leaves a tick that comes before the next frame idle.
 * Never inside a D3D scene, and not while the activity has no surface, which
 * would hold the load at Present until it returns. A campaign movie (UTCin-*,
 * UTMissionGRI.PlayBinkMovie) plays in the frame loop: the input pump answers
 * a tap or a pad button there with the player's own InputKey, as the wait does,
 * and publishes whether a movie plays for the touch layer. */
#define GFULLSCREEN_MOVIE 0x01D46BE8u    /* GFullScreenMovie */
#define MOVIE_VTABLE 0x0199D168u         /* FFullScreenMovieBink: [1] Tick, [10] GameThreadIsMoviePlaying */
#define MOVIE_TICK_VA 0x00C18950u
#define MOVIE_PLAYING_VA 0x00C14DE0u
#define TEXT_EMPTY 0x019C9D8Cu           /* TEXT(""), which UGameViewportClient::InputKey asks about (00C80EB1) */
#define GGAME_THREAD_ID 0x01DBDD5Cu      /* GGameThreadId and its flag, set by PreInit (004081D2) */
#define GGAME_THREAD_ID_SET 0x01DBDD60u
#define LOAD_TICK_NS 16000000ull
static const uint8_t movie_tick_preimage[] = {
    0x0f,0x57,0xc9,0x83,0xec,0x38,0x56,0x8b,0xf1,0xf3,0x0f,0x10,0x46,0x38,0x0f,0x2e,
    0xc1,0x9f,0xf6,0xc4,0x44,0x7b,0x3a,0xf3,0x0f,0x5c,0x44,0x24,0x40,0x0f,0x2f,0xc8,
};
static const uint8_t movie_playing_preimage[] = {
    0x6a,0xff,0x68,0x69,0x84,0x66,0x01,0x64,0xa1,0x00,0x00,0x00,0x00,0x50,0x83,0xec,
    0x1c,0x53,0x55,0x56,0x57,0xa1,0xe0,0x24,0xd3,0x01,0x33,0xc4,0x50,0x8d,0x44,0x24,
};
extern int void_d3d_scene_open __attribute__((weak));
__attribute__((weak)) int void_android_render_ready(void) { return 1; }
static void_fn movie_tick_original;
static int movie_service_ready, movie_service_busy, movie_playing_now;
static uint32_t movie_skips_handled;     /* skip requests answered or dropped */
static uint64_t movie_ticked_ns;         /* the movie's last Tick, by the frame loop or a load */
static uint64_t movie_asked_ns;          /* a load's last question whether a movie plays */
/* The current blocking stretch, logged once the frame loop ticks the movie again. */
static unsigned load_ticks;
static uint64_t load_first_ns, load_gap_ns, load_busy_ns;

/* For GameActivity.nativeMoviePlaying: a tap then skips instead of clicking the menu below. */
int void_loading_movie_playing(void) { return __atomic_load_n(&movie_playing_now, __ATOMIC_ACQUIRE); }

static void movie_tick_hook(void)
{
    uint64_t now = void_monotonic_ns();
    if (!movie_service_busy && load_ticks) {
        void_log("[loading] movie ticked through a blocking load: %u ticks in %.1f s, longest gap %.0f ms, %.0f ms ticking",
                 load_ticks, (double)(now - load_first_ns) / 1e9, (double)load_gap_ns / 1e6, (double)load_busy_ns / 1e6);
        load_ticks = 0;
    }
    movie_ticked_ns = now;
    movie_tick_original();
}

/* Game thread, machine owned, between guest calls: everything a caller may
 * keep in registers or LastError survives. */
static void movie_service(int loading)
{
    if (!movie_service_ready || movie_service_busy || MEM32(GIS_THREADED_RENDERING) ||
        !MEM32(GGAME_THREAD_ID_SET) || MEM32(GGAME_THREAD_ID) != void_thread_id()) return;
    uint64_t now = void_monotonic_ns();
    uint32_t requested = void_input_movie_skips();
    int due = loading && now - movie_ticked_ns >= LOAD_TICK_NS && now - movie_asked_ns >= LOAD_TICK_NS &&
              !(&void_d3d_scene_open && void_d3d_scene_open) && void_android_render_ready();
    if (loading && !due && requested == movie_skips_handled) return;
    if (loading) movie_asked_ns = now;
    uint32_t movie = MEM32(GFULLSCREEN_MOVIE);
    if (!movie || !void_vm_readable(movie, 4) || MEM32(movie) != MOVIE_VTABLE) {
        __atomic_store_n(&movie_playing_now, 0, __ATOMIC_RELEASE);
        movie_skips_handled = requested;
        return;
    }
    movie_service_busy = 1;
    uint32_t eax = g_eax, ecx = g_ecx, edx = g_edx, error = void_get_last_error(), text = TEXT_EMPTY;
    int playing = void_call_guest_this(MOVIE_PLAYING_VA, movie, 1, &text) != 0;
    if (playing != __atomic_exchange_n(&movie_playing_now, playing, __ATOMIC_ACQ_REL))
        void_log("[loading] movie %s%s", playing ? "playing" : "stopped", loading ? " (load)" : "");
    if (requested != movie_skips_handled) {
        movie_skips_handled = requested;
        if (playing) movie_skip(movie);
    }
    if (due && playing) {
        if (!load_ticks) { load_first_ns = now; load_gap_ns = load_busy_ns = 0; }
        else if (now - movie_ticked_ns > load_gap_ns) load_gap_ns = now - movie_ticked_ns;
        ++load_ticks;
        MEM32(RENDER_TICKED) = 1;
        void_call_guest(RENDER_TICKABLES_VA, 0, NULL, 0);
        movie_ticked_ns = void_monotonic_ns();
        load_busy_ns += movie_ticked_ns - now;
    }
    g_eax = eax; g_ecx = ecx; g_edx = edx;
    void_set_last_error(error);
    movie_service_busy = 0;
}

/* The input pump (input.c), from the game's message loop. */
void void_loading_movie_input(void) { movie_service(0); }

static void movie_service_init(void)
{
    if (movie_tick_original) return;
    void_fn tick = void_dispatch_find(MOVIE_TICK_VA);
    if (!tick || !original_code(MOVIE_TICK_VA, movie_tick_preimage, sizeof movie_tick_preimage) ||
        !original_code(MOVIE_PLAYING_VA, movie_playing_preimage, sizeof movie_playing_preimage) ||
        !original_code(RENDER_TICKABLES_VA, render_tickables_preimage, sizeof render_tickables_preimage) ||
        !void_vm_readable(MOVIE_VTABLE, 44) || MEM32(MOVIE_VTABLE + 4) != MOVIE_TICK_VA ||
        MEM32(MOVIE_VTABLE + 40) != MOVIE_PLAYING_VA || !void_vm_readable(TEXT_EMPTY, 2) || MEM16(TEXT_EMPTY)) {
        void_log("[loading] movie service skipped: original code/dispatch mismatch");
        return;
    }
    movie_tick_original = tick;
    void_dispatch_add(MOVIE_TICK_VA, movie_tick_hook);
    movie_service_ready = 1;
    void_log("[loading] Verified original movie Tick C18950 and IsMoviePlaying C14DE0: loading movies tick during "
             "blocking loads%s", movie_skip_ready ? ", taps and pad buttons skip movies in the frame loop" : "");
}

static void movie_wait_init(void)
{
    if (movie_wait_original) return;
    void_fn original = void_dispatch_find(MOVIE_WAIT_VA);
    if (!original || !original_code(MOVIE_WAIT_VA, movie_wait_preimage, sizeof movie_wait_preimage) ||
        !original_code(RENDER_TICKABLES_VA, render_tickables_preimage, sizeof render_tickables_preimage) ||
        !original_code(PUMP_MESSAGES_VA, pump_messages_preimage, sizeof pump_messages_preimage)) {
        void_log("[loading] movie wait ticking skipped: original code/dispatch mismatch");
        return;
    }
    movie_wait_original = original;
    void_dispatch_add(MOVIE_WAIT_VA, movie_wait_ticking);
    movie_skip_ready = original_code(MOVIE_INPUT_KEY_VA, movie_input_key_preimage, sizeof movie_input_key_preimage);
    void_log("[loading] Verified original movie wait C12E20: ticks the movie without a rendering thread%s",
             movie_skip_ready ? "; taps and pad buttons skip through InputKey C18AC0" : "");
}

void void_loading_init(void)
{
    movie_wait_init();
    movie_service_init();
    if (precache_original) return;
    void_fn original = void_dispatch_find(ASYNC_PRECACHE_VA);
    if (!original || !void_vm_readable(ASYNC_PRECACHE_VA, sizeof precache_preimage) ||
        memcmp(GPTR(ASYNC_PRECACHE_VA), precache_preimage, sizeof precache_preimage)) {
        void_log("[loading] Precache cooperative wait skipped: original code/dispatch mismatch");
        return;
    }
    precache_wait_us = 50;
    const char *value = getenv("UT3_PRECACHE_WAIT_US");
    if (value && *value) {
        char *end;
        errno = 0;
        unsigned long parsed = strtoul(value, &end, 10);
        if (!errno && !*end && parsed <= 1000) precache_wait_us = (unsigned)parsed;
    }
    precache_original = original;
    void_dispatch_add(ASYNC_PRECACHE_VA, precache_cooperative);
    void_log("[loading] Verified original Precache BAD370: pending wait %u us after 8 polls", precache_wait_us);
}
