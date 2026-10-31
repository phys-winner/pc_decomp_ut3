#define _GNU_SOURCE
#define UT3_ANDROID_APP
#include <pthread.h>
#include <stdlib.h>
static int test_pthread_create(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
static int test_setenv(const char *, const char *, int);
#define pthread_create test_pthread_create
#define setenv test_setenv
#include "../android/host/android_main.c"
#undef setenv
#undef pthread_create
#include <assert.h>

static int create_calls, fail_profile_env, fail_command_env;
static const char *expected_command_line;
static const char *normal_command_line = "\"C:\\Games\\UT3\\Binaries\\UT3.exe\" DM-Deck -nosplash -nomovie -forcelogflush -onethread -ResX=1056 -ResY=480";
static const char *fixed_command_line = "\"C:\\Games\\UT3\\Binaries\\UT3.exe\" DM-Deck -nosplash -nomovie -forcelogflush -onethread -ResX=1056 -ResY=480 -fixedseed";
static const char *front_end_command_line = "\"C:\\Games\\UT3\\Binaries\\UT3.exe\" UTFrontEnd -nosplash -nomovie -forcelogflush -onethread -ResX=1056 -ResY=480";
static int test_setenv(const char *name, const char *value, int overwrite)
{
    if ((fail_profile_env && !strcmp(name, "VOID_BIG_CORES")) ||
        (fail_command_env && !strcmp(name, "UT3_COMMAND_LINE"))) { errno = ENOMEM; return -1; }
    return setenv(name, value, overwrite);
}
static int test_pthread_create(pthread_t *thread, const pthread_attr_t *attributes, void *(*entry)(void *), void *argument)
{
    (void)thread; (void)attributes; (void)entry; (void)argument;
    assert(!strcmp(getenv("VOID_BIG_CORES"), "1"));
    assert(!strcmp(getenv("UT3_FPS_LIMIT"), "60"));
    assert(!strcmp(getenv("UT3_RENDER_WIDTH"), "1056"));
    assert(!strcmp(getenv("UT3_RENDER_HEIGHT"), "480"));
    assert(expected_command_line && !strcmp(getenv("UT3_COMMAND_LINE"), expected_command_line));
    ++create_calls;
    return EAGAIN; /* Validate startup ordering without creating a game thread. */
}

int ut3_game_main(const char *game, const char *data, int selftest)
{
    (void)game; (void)data; (void)selftest;
    return 0;
}
/* This fixture isolates input/JNI; the ADPF fixture owns session lifecycle tests. */
void ut3_adpf_cancel(int blocked) { (void)blocked; }
void ut3_adpf_thread_close(void) {}
void ut3_adpf_set_target(int fps) { assert(fps >= 1 && fps <= 1000); }
void void_process_exit(uint32_t code, int detach)
{
    (void)detach;
    _exit((int)code);
}

static const char *test_string(JNIEnv *env, jstring string, jboolean *copy)
{
    (void)env; (void)copy;
    return (const char *)string;
}
static void test_release_string(JNIEnv *env, jstring string, const char *chars)
{
    (void)env; (void)string; (void)chars;
}
static jobject test_adapter_identity(JNIEnv *env,jclass type,jmethodID method,...)
{
    (void)env;assert(type==crypto_class&&method==crypto_adapter_mac);
    return (jobject)"020000000001"; /* Synthetic own-process identity, no AndroidKeyStore claim. */
}
static jboolean test_no_exception(JNIEnv *env) { (void)env;return JNI_FALSE; }
static void test_delete_local(JNIEnv *env,jobject object) { (void)env;(void)object; }

static void test_renderer_arguments(void)
{
    assert(Java_org_ut3_port_GameActivity_nativeConfigure(NULL, NULL, 0, 50, 50, JNI_TRUE) == EINVAL);
    assert(Java_org_ut3_port_GameActivity_nativeConfigure(NULL, NULL, 1001, 50, 50, JNI_TRUE) == EINVAL);
    assert(Java_org_ut3_port_GameActivity_nativeConfigure(NULL, NULL, 60, 1001, 50, JNI_TRUE) == EINVAL);
    assert(Java_org_ut3_port_GameActivity_nativeConfigure(NULL, NULL, 60, 50, -1, JNI_TRUE) == EINVAL);
    assert(Java_org_ut3_port_GameActivity_nativeConfigure(NULL, NULL, 60, 50, 1001, JNI_TRUE) == EINVAL);
    assert(!Java_org_ut3_port_GameActivity_nativeConfigure(NULL, NULL, 60, 50, 0, JNI_TRUE));
    assert(!strcmp(getenv("UT3_PRECACHE_WAIT_US"), "0"));
    assert(!Java_org_ut3_port_GameActivity_nativeConfigure(NULL, NULL, 45, 0, 50, JNI_TRUE) && selected_fps == 45);
    assert(!strcmp(getenv("UT3_DXVK_QUERY_WAIT_US"), "0"));
    assert(!Java_org_ut3_port_GameActivity_nativeConfigure(NULL, NULL, 60, 50, 50, JNI_TRUE) && selected_fps == 60);
    assert(!strcmp(getenv("UT3_PRECACHE_WAIT_US"), "50"));
    const struct JNINativeInterface functions = {
        .GetStringUTFChars = test_string, .ReleaseStringUTFChars = test_release_string,
        .CallStaticObjectMethod = test_adapter_identity,.ExceptionCheck = test_no_exception,
        .DeleteLocalRef = test_delete_local
    };
    JNIEnv env = &functions;
    crypto_class=(jclass)(uintptr_t)1;crypto_adapter_mac=(jmethodID)(uintptr_t)2;
    jstring data = (jstring)"/unused";
    assert(Java_org_ut3_port_GameActivity_nativeStart(&env, NULL, NULL, (jstring)"gles", JNI_FALSE, 1056, 480) == EINVAL);
    assert(Java_org_ut3_port_GameActivity_nativeStart(&env, NULL, data, NULL, JNI_FALSE, 1056, 480) == EINVAL);
    assert(Java_org_ut3_port_GameActivity_nativeStart(&env, NULL, data, (jstring)"gles", JNI_FALSE, 0, 480) == EINVAL);
    assert(Java_org_ut3_port_GameActivity_nativeStart(&env, NULL, data, (jstring)"gles", JNI_FALSE, 1056, 8193) == EINVAL);
    assert(Java_org_ut3_port_GameActivity_nativeStart(&env, NULL, data, (jstring)"gles", JNI_FALSE, 224, 480) == EINVAL);
    const char *invalid[] = {"", "vulkan", "GLES", "dxvk/../gles"};
    for (unsigned i = 0; i < sizeof invalid / sizeof *invalid; ++i)
        assert(Java_org_ut3_port_GameActivity_nativeStart(&env, NULL, data, (jstring)invalid[i], JNI_FALSE, 1056, 480) == EINVAL);
    game_started = 1;
    assert(!setenv("UT3_RENDERER", "gles", 1));
    assert(!setenv("VOID_BIG_CORES", "0", 1));
    assert(!setenv("UT3_COMMAND_LINE", "live-command-line", 1));
    assert(!Java_org_ut3_port_GameActivity_nativeStart(&env, NULL, data, (jstring)"gles", JNI_FALSE, 1056, 480));
    assert(!Java_org_ut3_port_GameActivity_nativeStart(&env, NULL, data, (jstring)"dxvk", JNI_TRUE, 1056, 480));
    assert(!strcmp(getenv("UT3_RENDERER"), "gles"));
    assert(!strcmp(getenv("VOID_BIG_CORES"), "0") && !create_calls);
    assert(!strcmp(getenv("UT3_COMMAND_LINE"), "live-command-line"));
    game_started = 0;
    unsetenv("UT3_RENDERER");
    fail_profile_env = 1;
    assert(Java_org_ut3_port_GameActivity_nativeStart(&env, NULL, data, (jstring)"gles", JNI_FALSE, 1056, 480) == ENOMEM);
    assert(!strcmp(getenv("VOID_BIG_CORES"), "0") && !game_started && !create_calls);
    fail_profile_env = 0;
    fail_command_env = 1;
    assert(Java_org_ut3_port_GameActivity_nativeStart(&env, NULL, data, (jstring)"gles", JNI_FALSE, 1056, 480) == ENOMEM);
    assert(!game_started && !create_calls && !strcmp(getenv("UT3_COMMAND_LINE"), "live-command-line"));
    fail_command_env = 0;
    expected_command_line = fixed_command_line;
    assert(Java_org_ut3_port_GameActivity_nativeStart(&env, NULL, data, (jstring)"gles", JNI_TRUE, 1056, 480) == EAGAIN);
    assert(!strcmp(getenv("VOID_BIG_CORES"), "1") && !game_started && create_calls == 1);
    expected_command_line = normal_command_line;
    assert(Java_org_ut3_port_GameActivity_nativeStart(&env, NULL, data, (jstring)"gles", JNI_FALSE, 1056, 480) == EAGAIN);
    assert(!game_started && create_calls == 2 && !strstr(getenv("UT3_COMMAND_LINE"), "fixedseed"));
    assert(Java_org_ut3_port_GameActivity_nativeStart(&env, NULL, data, (jstring)"dxvk-turnip", JNI_FALSE, 1056, 480) == EAGAIN);
    assert(!strcmp(getenv("UT3_RENDERER"), "dxvk") && !strcmp(getenv("UT3_VULKAN_DRIVER"), "turnip"));
    assert(!strcmp(getenv("UT3_DXVK_TURNIP"), "1") && create_calls == 3);
    assert(!strcmp(getenv("TU_DEBUG_FILE"), "/unused/tu-debug.txt"));
    assert(Java_org_ut3_port_GameActivity_nativeStart(&env, NULL, data, (jstring)"gles", JNI_FALSE, 1056, 480) == EAGAIN);
    assert(!strcmp(getenv("UT3_VULKAN_DRIVER"), "system") && !strcmp(getenv("UT3_DXVK_TURNIP"), "0") && create_calls == 4);
    assert(!getenv("TU_DEBUG_FILE"));
    assert(!Java_org_ut3_port_GameActivity_nativeConfigure(NULL, NULL, 60, 50, 50, JNI_FALSE));
    expected_command_line = front_end_command_line;
    assert(Java_org_ut3_port_GameActivity_nativeStart(&env, NULL, data, (jstring)"gles", JNI_FALSE, 1056, 480) == EAGAIN);
    assert(!benchmark_start && create_calls == 5);
    game_started = 1;
    assert(Java_org_ut3_port_GameActivity_nativeConfigure(NULL, NULL, 45, 0, 50, JNI_TRUE) == EBUSY);
    assert(selected_fps == 60 && !strcmp(getenv("UT3_DXVK_QUERY_WAIT_US"), "50"));
    assert(!strcmp(getenv("UT3_PRECACHE_WAIT_US"), "50"));
    assert(!benchmark_start);
    game_started = 0;
    unsetenv("UT3_RENDERER");
    unsetenv("UT3_COMMAND_LINE");
}

int main(void)
{
    test_renderer_arguments();
    extern int void_machine_is_owned(void);
    assert(!void_machine_is_owned());
    assert(void_guest_reserve());void_threads_init();void_dispatch_init();
    void_machine_enter();assert(void_machine_is_owned());
    void_machine_enter();assert(void_machine_is_owned());
    void_machine_leave();assert(void_machine_is_owned());
    void_blocking_begin();assert(!void_machine_is_owned());
    void_blocking_end();assert(void_machine_is_owned());
    void_machine_leave();assert(!void_machine_is_owned());
    void_input_set_screen_size(1280, 720);
    display_width = 2780;
    display_height = 1264;
    assert(Java_org_ut3_port_GameActivity_nativePointer(NULL, NULL, 0.5f, 0.5f, JNI_FALSE));
    uint32_t x, y;
    void_input_cursor(&x, &y);
    assert(x == 640 && y == 360);
    assert(!Java_org_ut3_port_GameActivity_nativePointer(NULL, NULL, 0.01f, 0.5f, JNI_FALSE));
    void_input_cursor(&x, &y);
    assert(x == 640 && y == 360);
    for (int i = 0; i < 4; ++i)
        assert(Java_org_ut3_port_GameActivity_nativePointer(NULL, NULL, 0.25f / 1280, 0, JNI_TRUE));
    int32_t dx, dy, dz;
    uint8_t buttons[8], keys[256];
    void_input_mouse_snapshot(&dx, &dy, &dz, buttons);
    assert(dx == 1 && dy == 0);
    const char pressed[] = {'W', 'A', 'S', 'D', 32};
    for (unsigned i = 0; i < sizeof pressed; ++i) void_input_key(pressed[i], 1);
    void_input_mouse_button(0, 1);
    void_input_mouse_move(10, 20);
    pointer_remainder_x = 0.75f;
    Java_org_ut3_port_GameActivity_nativePause(NULL, NULL, JNI_TRUE);
    for (unsigned i = 0; i < sizeof pressed; ++i) assert(void_input_key_state(pressed[i]) >= 0);
    void_input_keyboard_snapshot(keys);
    void_input_mouse_snapshot(&dx, &dy, &dz, buttons);
    for (unsigned i = 0; i < 256; ++i) assert(!keys[i]);
    for (unsigned i = 0; i < 8; ++i) assert(!buttons[i]);
    assert(!dx && !dy && !dz && !pointer_remainder_x && !pointer_remainder_y);
    assert(void_audio_pause_state() & 1u);
    Java_org_ut3_port_GameActivity_nativePause(NULL, NULL, JNI_FALSE);
    assert(!(void_audio_pause_state() & 1u));
    assert(void_audio_pause_state() >= 4);
    puts("Android input PASS: renderer/size validation, first-run ResX/ResY command line, fixed-seed retry and live-process immutability; VM ownership outside/nested/blocking/reacquired; letterbox/centre mapping, subpixel look, WASD/fire/jump release, pause/resume epoch");
    return 0;
}
