/* Surface lifecycle and ordinary Win32 input; no game addresses are patched here. */
#ifdef UT3_ANDROID_APP
#define _GNU_SOURCE
#include "host.h"
#include "input.h"
#include <aaudio/AAudio.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <errno.h>
#include <dlfcn.h>
#include <jni.h>
#include <limits.h>
#include <time.h>
#include <unistd.h>

int ut3_game_main(const char *, const char *, int);
static ANativeWindow *window;
static pthread_mutex_t window_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t window_cond = PTHREAD_COND_INITIALIZER;
static uint32_t window_serial, window_acked;
static int display_width, display_height, game_started;
static int selected_fps = 60;
static int benchmark_start;
static int game_result = INT_MIN, frame_ready;
static uint64_t pause_state;
static float pointer_remainder_x, pointer_remainder_y;
static char game_dir[1024], data_dir[1024];
static JavaVM *crypto_vm;
static jclass crypto_class;
static jmethodID crypto_transform, crypto_adapter_mac;
extern void ut3_openal_pause(int paused) __attribute__((weak));
extern void ut3_openal_log_stats(void) __attribute__((weak));
extern void ut3_adpf_cancel(int blocked);
extern void ut3_adpf_thread_close(void);
extern void ut3_adpf_set_target(int fps);

static int crypto_exception(JNIEnv *env)
{
    if (!(*env)->ExceptionCheck(env)) return 0;
    (*env)->ExceptionClear(env);
    return 1;
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved)
{
    (void)reserved;
    JNIEnv *env = NULL;
    if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK) return JNI_ERR;
    jclass local = (*env)->FindClass(env, "org/ut3/port/PortCrypto");
    int failed = crypto_exception(env);
    if (!local || failed) return JNI_ERR;
    crypto_class = (jclass)(*env)->NewGlobalRef(env, local);
    (*env)->DeleteLocalRef(env, local);
    failed = crypto_exception(env);
    if (!crypto_class || failed) return JNI_ERR;
    crypto_transform = (*env)->GetStaticMethodID(env, crypto_class, "transform", "(Z[B[B[B)[[B");
    failed = crypto_exception(env);
    if (!failed && crypto_transform)
        crypto_adapter_mac = (*env)->GetStaticMethodID(env, crypto_class, "getAdapterMacHex", "()Ljava/lang/String;");
    failed |= crypto_exception(env);
    if (!crypto_transform || !crypto_adapter_mac || failed) {
        (*env)->DeleteGlobalRef(env, crypto_class); crypto_class = NULL;
        return JNI_ERR;
    }
    crypto_vm = vm;
    return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL JNI_OnUnload(JavaVM *vm, void *reserved)
{
    (void)reserved;
    JNIEnv *env = NULL;
    if ((*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) == JNI_OK && crypto_class)
        (*env)->DeleteGlobalRef(env, crypto_class);
    crypto_class = NULL; crypto_vm = NULL;
}

static void crypto_wipe_array(JNIEnv *env, jbyteArray array)
{
    static const jbyte zeros[4096];
    if (!array) return;
    jsize size = (*env)->GetArrayLength(env, array);
    if (crypto_exception(env)) return;
    for (jsize offset = 0; offset < size;) {
        jsize count = size - offset < (jsize)sizeof zeros ? size - offset : (jsize)sizeof zeros;
        (*env)->SetByteArrayRegion(env, array, offset, count, zeros);
        if (crypto_exception(env)) return;
        offset += count;
    }
}

/* crypt32.c has already copied all guest inputs and released the machine.
 * Only native heap arrays cross this JNI boundary. Never enter the VM here. */
int void_android_crypt(int protect, const uint8_t *input, uint32_t input_bytes,
    const uint8_t *entropy, uint32_t entropy_bytes, const uint8_t *description, uint32_t description_bytes,
    uint8_t **output, uint32_t *output_bytes, uint8_t **output_description, uint32_t *output_description_bytes)
{
    if (!crypto_vm || !crypto_class || !crypto_transform) return 50;
    if (input_bytes > (1u << 20) || entropy_bytes > (1u << 20) || description_bytes > 8192u) return 87;
    *output = NULL; *output_description = NULL; *output_bytes = *output_description_bytes = 0;
    JNIEnv *env = NULL; int attached = 0, error = 13;
    jint status = (*crypto_vm)->GetEnv(crypto_vm, (void **)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if ((*crypto_vm)->AttachCurrentThread(crypto_vm, &env, NULL) != JNI_OK) return 31;
        attached = 1;
    } else if (status != JNI_OK) return 31;
    jbyteArray arrays[3] = {NULL, NULL, NULL}, result_data = NULL, result_description = NULL;
    jobjectArray result = NULL;
    uint8_t *data = NULL, *descr = NULL;
    uint32_t sizes[3] = {input_bytes, entropy_bytes, description_bytes};
    const uint8_t *sources[3] = {input, entropy, description};
    for (int i = 0; i < 3; ++i) {
        arrays[i] = (*env)->NewByteArray(env, (jsize)sizes[i]);
        if (!arrays[i] || crypto_exception(env)) { error = 8; goto cleanup; }
        if (sizes[i]) (*env)->SetByteArrayRegion(env, arrays[i], 0, (jsize)sizes[i], (const jbyte *)sources[i]);
        if (crypto_exception(env)) goto cleanup;
    }
    result = (jobjectArray)(*env)->CallStaticObjectMethod(env, crypto_class, crypto_transform,
        protect ? JNI_TRUE : JNI_FALSE, arrays[0], arrays[1], arrays[2]);
    if (crypto_exception(env) || !result || (*env)->GetArrayLength(env, result) != 2) goto cleanup;
    result_data = (jbyteArray)(*env)->GetObjectArrayElement(env, result, 0);
    if (crypto_exception(env) || !result_data) goto cleanup;
    result_description = (jbyteArray)(*env)->GetObjectArrayElement(env, result, 1);
    if (crypto_exception(env) || !result_data || !result_description) goto cleanup;
    jsize size = (*env)->GetArrayLength(env, result_data), descr_size = (*env)->GetArrayLength(env, result_description);
    if (crypto_exception(env) || size < 0 || size > (1 << 20) || descr_size < 0 || descr_size > 8192 || (descr_size & 1)) goto cleanup;
    data = malloc(size ? (size_t)size : 1); descr = malloc(descr_size ? (size_t)descr_size : 1);
    if (!data || !descr) { error = 8; goto cleanup; }
    if (size) (*env)->GetByteArrayRegion(env, result_data, 0, size, (jbyte *)data);
    if (crypto_exception(env)) goto cleanup;
    if (descr_size) (*env)->GetByteArrayRegion(env, result_description, 0, descr_size, (jbyte *)descr);
    if (crypto_exception(env)) goto cleanup;
    *output = data; *output_bytes = (uint32_t)size;
    *output_description = descr; *output_description_bytes = (uint32_t)descr_size;
    data = descr = NULL; error = 0;
cleanup:
    crypto_exception(env);
    if (data) { volatile uint8_t *p = data; for (jsize i = 0; i < size; ++i) p[i] = 0; free(data); }
    if (descr) { volatile uint8_t *p = descr; for (jsize i = 0; i < descr_size; ++i) p[i] = 0; free(descr); }
    crypto_wipe_array(env, result_data); crypto_wipe_array(env, result_description);
    if (result_data) (*env)->DeleteLocalRef(env, result_data);
    if (result_description) (*env)->DeleteLocalRef(env, result_description);
    if (result) (*env)->DeleteLocalRef(env, result);
    for (int i = 0; i < 3; ++i) if (arrays[i]) { crypto_wipe_array(env, arrays[i]); (*env)->DeleteLocalRef(env, arrays[i]); }
    crypto_exception(env);
    if (attached) (*crypto_vm)->DetachCurrentThread(crypto_vm);
    return error;
}

static int crypto_set_adapter_identity(JNIEnv *env)
{
    if (!crypto_class || !crypto_adapter_mac) return EINVAL;
    jstring identity = (jstring)(*env)->CallStaticObjectMethod(env, crypto_class, crypto_adapter_mac);
    if (crypto_exception(env) || !identity) return EINVAL;
    const char *value = (*env)->GetStringUTFChars(env, identity, NULL);
    int error = 0;
    int failed = crypto_exception(env);
    if (!value || failed) error = ENOMEM;
    else {
        if (strlen(value) != 12 || strspn(value, "0123456789abcdefABCDEF") != 12) error = EINVAL;
        else if (setenv("UT3_ADAPTER_MAC", value, 1)) error = errno;
        (*env)->ReleaseStringUTFChars(env, identity, value);
    }
    (*env)->DeleteLocalRef(env, identity);
    return error;
}

uint64_t void_audio_pause_state(void) { return __atomic_load_n(&pause_state, __ATOMIC_ACQUIRE); }
void void_android_frame_presented(void)
{
    __atomic_store_n(&frame_ready, 1, __ATOMIC_RELEASE);
    static uint64_t last_audio_stats;
    uint64_t now = void_monotonic_ns();
    if (ut3_openal_log_stats && now - last_audio_stats >= UINT64_C(5000000000)) {
        last_audio_stats = now;
        ut3_openal_log_stats();
    }
}
void void_android_load_ready(void) {}

int void_android_display_size(int *w, int *h)
{
    pthread_mutex_lock(&window_lock);
    *w = display_width;
    *h = display_height;
    pthread_mutex_unlock(&window_lock);
    return *w > 0 && *h > 0;
}

int void_android_render_ready(void)
{
    pthread_mutex_lock(&window_lock);
    int ready = window != NULL && !(void_audio_pause_state() & 1u);
    pthread_mutex_unlock(&window_lock);
    return ready;
}

void *void_android_window_current(uint32_t *serial, int wait)
{
    pthread_mutex_lock(&window_lock);
    while (wait && !window) pthread_cond_wait(&window_cond, &window_lock);
    *serial = window_serial;
    ANativeWindow *result = window;
    pthread_mutex_unlock(&window_lock);
    return result;
}

void *void_android_window(void)
{
    uint32_t serial;
    return void_android_window_current(&serial, 1);
}

void *ut3_dxvk_native_window(void)
{
    pthread_mutex_lock(&window_lock);
    ANativeWindow *result = window;
    if (result) ANativeWindow_acquire(result);
    pthread_mutex_unlock(&window_lock);
    return result;
}

void void_android_window_ack(uint32_t serial)
{
    pthread_mutex_lock(&window_lock);
    window_acked = serial;
    pthread_cond_broadcast(&window_cond);
    pthread_mutex_unlock(&window_lock);
}

JNIEXPORT void JNICALL Java_org_ut3_port_GameActivity_nativeSurface(JNIEnv *env, jclass type, jobject surface)
{
    (void)type;
    ANativeWindow *next = surface ? ANativeWindow_fromSurface(env, surface) : NULL;
    if (next) {
        typedef int32_t (*set_frame_rate_fn)(ANativeWindow *, float, int8_t);
        set_frame_rate_fn set_frame_rate = (set_frame_rate_fn)dlsym(RTLD_DEFAULT, "ANativeWindow_setFrameRate");
        if (set_frame_rate) set_frame_rate(next, (float)selected_fps, 0);
    }
    pthread_mutex_lock(&window_lock);
    ANativeWindow *old = window;
    window = next;
    if (window) { display_width = ANativeWindow_getWidth(window); display_height = ANativeWindow_getHeight(window); }
    uint32_t serial = ++window_serial;
    /* Atomic epoch invalidation only: no API/TLS/session work under this lock. */
    ut3_adpf_cancel(!window || (void_audio_pause_state() & 1u));
    pthread_cond_broadcast(&window_cond);
    if (!surface && old) {
        struct timespec until;
        clock_gettime(CLOCK_REALTIME, &until);
        until.tv_sec += 1;
        while (window_acked != serial && pthread_cond_timedwait(&window_cond, &window_lock, &until) == 0) {}
    }
    pthread_mutex_unlock(&window_lock);
    if (old) ANativeWindow_release(old);
}

static void release_input(void)
{
    pointer_remainder_x = pointer_remainder_y = 0;
    for (unsigned vk = 1; vk < 256; ++vk) if (void_input_key_state(vk) < 0) void_input_key(vk, 0);
    for (int i = 0; i < 8; ++i) void_input_mouse_button(i, 0);
    int32_t dx, dy, dz;
    uint8_t buttons[8], keys[256];
    void_input_keyboard_snapshot(keys);
    void_input_mouse_snapshot(&dx, &dy, &dz, buttons);
}

JNIEXPORT void JNICALL Java_org_ut3_port_GameActivity_nativePause(JNIEnv *env, jclass type, jboolean pause)
{
    (void)env; (void)type;
    uint64_t state = void_audio_pause_state(), next;
    do {
        if ((state & 1u) == (pause ? 1u : 0u)) break;
        next = ((state & ~UINT64_C(1)) + 2u) | (pause ? 1u : 0u);
    } while (!__atomic_compare_exchange_n(&pause_state, &state, next, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE));
    ut3_adpf_cancel(!void_android_render_ready());
    if (pause) release_input();
    if (ut3_openal_pause) ut3_openal_pause(pause != JNI_FALSE);
}

/* A part's folder as the launcher chose it (PORT_PART_<ID>, launcher/native/port_env.h), else
 * the port's own folder. */
static int part_folder(char *out, size_t size, const char *name, const char *fallback)
{
    const char *value = getenv(name);
    if (!value || value[0] != '/') value = fallback;
    int count = snprintf(out, size, "%s", value);
    if (count < 0 || count >= (int)size) return ENAMETOOLONG;
    while (count > 1 && out[count - 1] == '/') out[--count] = 0;
    return 0;
}

/* The launcher's "skip intro movies" (PORT_SKIP_INTRO "1"/"0"); without it the intro is skipped,
 * as before the launcher. The original -nostartupmovies keeps the loading and story movies. */
static int skip_intro(void)
{
    const char *value = getenv("PORT_SKIP_INTRO");
    return !value || !*value || (strcmp(value, "0") && strcmp(value, "false"));
}

static void *run_game(void *unused)
{
    (void)unused;
    int result = ut3_game_main(game_dir, data_dir, 0);
    ut3_adpf_thread_close();
    __atomic_store_n(&game_result, result, __ATOMIC_RELEASE);
    void_log("[ut3] Android game thread returned %d", result);
    return NULL;
}

JNIEXPORT jint JNICALL Java_org_ut3_port_GameActivity_nativeConfigure(JNIEnv *env, jclass type, jint fps, jint query_wait_us, jint precache_wait_us, jboolean benchmark)
{
    (void)env; (void)type;
    if (game_started) return EBUSY;
    if (fps < 1 || fps > 1000 || query_wait_us < 0 || query_wait_us > 1000 || precache_wait_us < 0 || precache_wait_us > 1000) return EINVAL;
    char wait_value[16];
    snprintf(wait_value, sizeof wait_value, "%d", query_wait_us);
    if (setenv("UT3_DXVK_QUERY_WAIT_US", wait_value, 1)) return errno;
    snprintf(wait_value, sizeof wait_value, "%d", precache_wait_us);
    if (setenv("UT3_PRECACHE_WAIT_US", wait_value, 1)) return errno;
    selected_fps = fps;
    benchmark_start = benchmark != JNI_FALSE;
    ut3_adpf_set_target(fps);
    pthread_mutex_lock(&window_lock);
    if (window) {
        typedef int32_t (*set_frame_rate_fn)(ANativeWindow *, float, int8_t);
        set_frame_rate_fn set_frame_rate = (set_frame_rate_fn)dlsym(RTLD_DEFAULT, "ANativeWindow_setFrameRate");
        if (set_frame_rate) set_frame_rate(window, (float)fps, 0);
    }
    pthread_mutex_unlock(&window_lock);
    return 0;
}

JNIEXPORT jint JNICALL Java_org_ut3_port_GameActivity_nativeStart(JNIEnv *env, jclass type, jstring data, jstring renderer, jboolean fixed_seed, jint render_width, jint render_height)
{
    (void)type;
    if (!renderer || !data || render_width < 8 || render_width > 8192 || render_height < 8 || render_height > 8192 || render_width < render_height) return EINVAL;
    if (!game_started) { int identity_error = crypto_set_adapter_identity(env); if (identity_error) return identity_error; }
    const char *mode = (*env)->GetStringUTFChars(env, renderer, NULL);
    if (!mode) return ENOMEM;
    int turnip = !strcmp(mode, "dxvk-turnip");
    int valid = !strcmp(mode, "gles") || !strcmp(mode, "dxvk") || turnip;
    int mode_error = 0;
    if (valid && !game_started &&
        (setenv("UT3_RENDERER", turnip ? "dxvk" : mode, 1) ||
         setenv("UT3_VULKAN_DRIVER", turnip ? "turnip" : "system", 1) ||
         setenv("UT3_DXVK_TURNIP", turnip ? "1" : "0", 1))) mode_error = errno;
    (*env)->ReleaseStringUTFChars(env, renderer, mode);
    if (!valid) return EINVAL;
    if (mode_error) return mode_error;
    if (game_started) return 0;
    /* Keep the serialized guest on the strongest-core policy to avoid
     * migration/lower-capacity placement; native workers retain Android's mask. */
    if (setenv("VOID_BIG_CORES", "1", 1)) return errno;
    char render_size[16];
    snprintf(render_size, sizeof render_size, "%d", render_width);
    if (setenv("UT3_RENDER_WIDTH", render_size, 1)) return errno;
    snprintf(render_size, sizeof render_size, "%d", render_height);
    if (setenv("UT3_RENDER_HEIGHT", render_size, 1)) return errno;
    snprintf(render_size, sizeof render_size, "%d", selected_fps);
    if (setenv("UT3_FPS_LIMIT", render_size, 1) || setenv("DXVK_FRAME_RATE", render_size, 1)) return errno;
    /* Original ResX=/ResY= parsing overrides first-run compatibility's saved
     * resolution at viewport creation. Keep the Android launch profile explicit. */
    char command_line[256];
    int command_size = snprintf(command_line, sizeof command_line,
        "\"C:\\Games\\UT3\\Binaries\\UT3.exe\" %s -nosplash%s -forcelogflush -onethread -ResX=%d -ResY=%d%s",
        benchmark_start ? "DM-Deck" : "UTFrontEnd", benchmark_start ? " -nomovie" : skip_intro() ? " -nostartupmovies" : "",
        render_width, render_height, fixed_seed ? " -fixedseed" : "");
    if (command_size < 0 || command_size >= (int)sizeof command_line) return EOVERFLOW;
    if (setenv("UT3_COMMAND_LINE", command_line, 1)) return errno;
    const char *user = (*env)->GetStringUTFChars(env, data, NULL);
    if (!user) return -1;
    int size = snprintf(data_dir, sizeof data_dir, "%s", user);
    (*env)->ReleaseStringUTFChars(env, data, user);
    if (size < 0 || size >= (int)sizeof data_dir) return -1;
    if (setenv("DXVK_LOG_PATH", data_dir, 1) || setenv("DXVK_STATE_CACHE_PATH", data_dir, 1)) return errno;
    if (turnip) {
        char debug_file[1100];
        int count = snprintf(debug_file, sizeof debug_file, "%s/tu-debug.txt", data_dir);
        if (count < 0 || count >= (int)sizeof debug_file) return EOVERFLOW;
        if (setenv("TU_DEBUG_FILE", debug_file, 1)) return errno;
    } else unsetenv("TU_DEBUG_FILE");
    int folder_error = part_folder(game_dir, sizeof game_dir, "PORT_PART_GAME", "/sdcard/UT3/game");
    if (folder_error) return folder_error;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 64u << 20);
    pthread_t thread;
    int error = pthread_create(&thread, &attr, run_game, NULL);
    pthread_attr_destroy(&attr);
    if (error) return error;
    pthread_detach(thread);
    game_started = 1;
    return 0;
}

JNIEXPORT jint JNICALL Java_org_ut3_port_GameActivity_nativeStatus(JNIEnv *env, jclass type)
{
    (void)env; (void)type;
    int result = __atomic_load_n(&game_result, __ATOMIC_ACQUIRE);
    if (result != INT_MIN) return result;
    return __atomic_load_n(&frame_ready, __ATOMIC_ACQUIRE) ? INT_MAX : INT_MIN;
}

JNIEXPORT void JNICALL Java_org_ut3_port_GameActivity_nativeKey(JNIEnv *env, jclass type, jint vk, jboolean down)
{
    (void)env; (void)type;
    if (vk > 0 && vk < 256) void_input_key((uint32_t)vk, down);
}

JNIEXPORT void JNICALL Java_org_ut3_port_GameActivity_nativeButton(JNIEnv *env, jclass type, jboolean down)
{
    (void)env; (void)type;
    void_input_mouse_button(0, down);
}

extern void void_power_android_snapshot(int plugged, int present, int level, int scale, int status);
JNIEXPORT void JNICALL Java_org_ut3_port_GameActivity_nativePower(JNIEnv *env, jclass type,
    jint plugged, jint present, jint level, jint scale, jint status)
{
    (void)env; (void)type;
    void_power_android_snapshot(plugged, present, level, scale, status);
}

JNIEXPORT jboolean JNICALL Java_org_ut3_port_GameActivity_nativePointer(JNIEnv *env, jclass type,
    jfloat x, jfloat y, jboolean relative)
{
    (void)env; (void)type;
    if (!isfinite(x) || !isfinite(y) || fabsf(x) > 4 || fabsf(y) > 4) return JNI_FALSE;
    int w, h, sw, sh;
    void_input_screen_size(&w, &h);
    if (w <= 0 || h <= 0) return JNI_FALSE;
    if (relative) {
        float dx = x * w + pointer_remainder_x, dy = y * h + pointer_remainder_y;
        int ix = (int)dx, iy = (int)dy;
        pointer_remainder_x = dx - ix; pointer_remainder_y = dy - iy;
        void_input_mouse_move(ix, iy);
        return JNI_TRUE;
    }
    if (!void_android_display_size(&sw, &sh)) return JNI_FALSE;
    float scale = fminf((float)sw / w, (float)sh / h);
    float px = (x * sw - (sw - w * scale) * 0.5f) / scale;
    float py = (y * sh - (sh - h * scale) * 0.5f) / scale;
    if (px < 0 || px >= w || py < 0 || py >= h) return JNI_FALSE;
    void_input_mouse_warp((int)px, (int)py);
    return JNI_TRUE;
}
/* ------------------------------------------------------------- audio */
static AAudioStream *stream;
static pthread_mutex_t audio_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t stream_pause_state;
static uint64_t audio_short_writes, audio_dropped_frames, audio_errors;
static unsigned audio_zero_chunks;
/* shortcut: retry low latency only after process restart; revisit for route-aware retries. */
static int audio_normal_output;

static void audio_close_stream(void)
{
    if (stream) {
        AAudioStream_requestStop(stream);
        AAudioStream_close(stream);
    }
    stream = NULL;
    audio_zero_chunks = 0;
}

static void audio_close_failed(aaudio_result_t error)
{
    if (++audio_errors <= 4) void_log("[audio] stream error %d; reopening on next chunk", (int)error);
    audio_close_stream();
}

/* A timed-out write can accept a prefix. Never resend it or silently discard
 * the suffix; retry within a bounded budget, with no guest VM lock held. */
static uint32_t audio_write_frames(const int16_t *frames, uint32_t count)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    int64_t deadline = (int64_t)now.tv_sec * 1000000000LL + now.tv_nsec + 100000000LL;
    uint32_t written = 0;
    uint64_t pause = stream_pause_state;
    while (written < count && void_audio_pause_state() == pause) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        int64_t remaining = deadline - ((int64_t)now.tv_sec * 1000000000LL + now.tv_nsec);
        if (remaining <= 0) break;
        aaudio_result_t result = AAudioStream_write(stream, frames + written * 2,
            (int32_t)(count - written), remaining < 20000000LL ? remaining : 20000000LL);
        if (result < 0) { audio_close_failed(result); break; }
        if ((uint32_t)result < count - written) ++audio_short_writes;
        written += (uint32_t)result;
        if (!result) usleep(1000);
    }
    if (written < count && void_audio_pause_state() == pause) {
        if (audio_dropped_frames < 1920)
            void_log("[audio] discarded %u unwritten frames after timeout/error", count - written);
        audio_dropped_frames += count - written;
    }
    return written;
}

void void_audio_output(const int16_t *frames, uint32_t count)
{
    pthread_mutex_lock(&audio_lock);
    uint64_t pause = void_audio_pause_state();
    /* Pause retains queued PCM; a fresh foreground epoch needs a fresh stream. */
    if (stream && pause != stream_pause_state) audio_close_stream();
    if (pause & 1u) {
        /* mixer_main already paces every chunk, including in the background. */
        pthread_mutex_unlock(&audio_lock);
        return;
    }
    if (!stream) {
        AAudioStreamBuilder *b = NULL;
        aaudio_result_t created = AAudio_createStreamBuilder(&b);
        if (created == AAUDIO_OK) {
            AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_I16);
            AAudioStreamBuilder_setChannelCount(b, 2);
            AAudioStreamBuilder_setSampleRate(b, 48000);
            AAudioStreamBuilder_setPerformanceMode(b, audio_normal_output ?
                AAUDIO_PERFORMANCE_MODE_NONE : AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
            aaudio_result_t result = AAudioStreamBuilder_openStream(b, &stream);
            if (result == AAUDIO_OK) {
                stream_pause_state = pause;
                /* MMAP can erase pre-start PCM: prime silence, then send the whole chunk. */
                static const int16_t silence[2 * 480] = {0};
                result = AAudioStream_write(stream, silence, (int32_t)(count < 480 ? count : 480), 0);
                if (result >= 0) {
                    if (void_audio_pause_state() == pause) result = AAudioStream_requestStart(stream);
                    else audio_close_stream();
                }
                if (result < 0) audio_close_failed(result);
            } else { stream = NULL; audio_close_failed(result); }
            AAudioStreamBuilder_delete(b);
        } else audio_close_failed(created);
        if (!stream) {
            if (void_audio_pause_state() == pause) audio_dropped_frames += count;
            pthread_mutex_unlock(&audio_lock);
            return;
        }
    }
    uint32_t written = audio_write_frames(frames, count);
    if (written || void_audio_pause_state() != pause) audio_zero_chunks = 0;
    else if (stream && count && !audio_normal_output && ++audio_zero_chunks == 3) {
        audio_normal_output = 1;
        ++audio_errors;
        void_log("[audio] low latency stream stalled for 3 chunks; using normal output until process exit");
        audio_close_stream();
    }
    /* Level of what reached the device, every 10 s of audio: silence or
     * underruns show up in the log rather than only by ear. */
    static uint32_t level_frames;
    static double level_sum;
    static int level_peak;
    for (uint32_t i = 0; i < written * 2; ++i) {
        int v = frames[i] < 0 ? -frames[i] : frames[i];
        if (v > level_peak) level_peak = v;
        level_sum += (double)frames[i] * frames[i];
    }
    level_frames += written;
    if (level_frames >= 48000u * 10u) {
        void_log("[audio] last 10 s: peak %d, rms %.0f, underruns %d, short writes %llu, dropped frames %llu, errors %llu", level_peak,
                 sqrt(level_sum / (2.0 * level_frames)), stream ? (int)AAudioStream_getXRunCount(stream) : -1,
                 (unsigned long long)audio_short_writes, (unsigned long long)audio_dropped_frames,
                 (unsigned long long)audio_errors);
        level_frames = 0;
        level_sum = 0;
        level_peak = 0;
    }
    pthread_mutex_unlock(&audio_lock);
}
#endif
