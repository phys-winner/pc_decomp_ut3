/* Production guest CDECL bridge and real pinned AAudio playback, in isolation. */
#include "../android/host/openal.c"
#include "../android/host/pe.c"
#include "test_openal_apis.h"
#include <assert.h>
#include <math.h>
#include <unistd.h>

typedef FrostpunkAudioMetrics audio_metrics;
static uint32_t audio_module;
static unsigned calls;

void void_process_exit(uint32_t code, int detach) { (void)detach; _exit((int)code); }
/* This audio CLI owns no Android surface; graphics calls are fixture errors. */
void *ut3_dxvk_native_window(void) { void_fatal("graphics call in isolated audio fixture"); }
int void_android_render_ready(void) { void_fatal("graphics call in isolated audio fixture"); }

static uint32_t procedure(const char *name)
{
    uint32_t text = void_gstrdup(name);
    uint32_t result = void_module_proc(audio_module, text);
    void_gfree(text);
    return result;
}
static uint32_t api(const char *name, unsigned count, const uint32_t *args)
{
    uint32_t target = procedure(name), before = g_esp;
    assert(target && void_dispatch_find(target));
    uint32_t result = void_call_guest(target, count, args, 0);
    assert(g_esp == before);
    ++calls;
    return result;
}
#define API(name, count, ...) api(#name, count, (uint32_t[]){__VA_ARGS__})
static uint32_t f32(float value) { uint32_t bits; memcpy(&bits, &value, 4); return bits; }
static void noerror(void) { assert(API(alGetError, 0, 0) == AL_NO_ERROR); }
static void nap(unsigned ms) { void_blocking_begin(); usleep(ms * 1000); void_blocking_end(); }
static int source_integer(uint32_t source, ALenum param, uint32_t scratch)
{
    API(alGetSourcei, 3, source, param, scratch);
    return (int32_t)MEM32(scratch);
}
static double fp_result(const char *name, ALenum value)
{
    int before = g_fp_top;
#ifdef RECOMP_X87_WINDOW
    int position = g_fp_sp;
#endif
    uint32_t arg = value;
    api(name, 1, &arg);
    assert(g_fp_top == before + 1);
#ifdef RECOMP_X87_WINDOW
    assert(g_fp_sp == position - 1);
    double result = fp_window_pop();
    assert(g_fp_sp == position);
#else
    double result = fp_pop_impl(g_st, &g_fp_top);
#endif
    assert(g_fp_top == before);
    return result;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    assert(void_guest_reserve());
    void_threads_init(); void_dispatch_init(); void_machine_enter(); add_system_pseudos();
    audio_module = void_module_load("OpenAL32.dll");
    assert(audio_module);
    for (unsigned i = 0; i < sizeof openal_api_names / sizeof *openal_api_names; ++i)
        assert(procedure(openal_api_names[i]));
    assert(sizeof openal_api_names / sizeof *openal_api_names == 118);
    assert(!procedure("EAXGet") && !procedure("EAXSet"));
    assert(!procedure("EAXGetBufferMode") && !procedure("EAXSetBufferMode"));
    uint32_t default_name = API(alcGetString, 2, 0, ALC_DEFAULT_DEVICE_SPECIFIER);
    assert(default_name && void_vm_readable(default_name, 1) && native_library && native_ok);
    void (*enable_metrics)(int) = dlsym(native_library, "FrostpunkAudio_ProbeEnable");
    int (*get_metrics)(audio_metrics *, uint32_t) = dlsym(native_library, "FrostpunkAudio_GetMetrics");
    assert(enable_metrics && get_metrics);
    enable_metrics(1);
    uint32_t configured = void_gstrdup("Generic Software");
    uint32_t dev = API(alcOpenDevice, 1, configured);
    void_gfree(configured);
    assert(dev && device(dev) && device(dev)->native);
    uint32_t attrs = void_galloc(20);
    const int32_t values[] = {ALC_FREQUENCY, 44100, ALC_MAX_AUXILIARY_SENDS, 5, 0};
    memcpy(GPTR(attrs), values, sizeof values);
    uint32_t ctx = API(alcCreateContext, 2, dev, attrs);
    assert(ctx && context(ctx) && context(ctx)->native);
    assert(API(alcMakeContextCurrent, 1, ctx) == ALC_TRUE);
    assert(API(alcGetCurrentContext, 0, 0) == ctx);
    assert(API(alcGetContextsDevice, 1, ctx) == dev);
    assert(N.alcGetCurrentContext() == context(ctx)->native);
    assert(API(alcGetError, 1, dev) == ALC_NO_ERROR);
    noerror();
    uint32_t renderer = API(alGetString, 1, AL_RENDERER);
    uint32_t version = API(alGetString, 1, AL_VERSION);
    uint32_t endpoint = API(alcGetString, 2, dev, ALC_DEVICE_SPECIFIER);
    assert(renderer && version && endpoint);
    assert(!strcmp(GPTR(renderer), N.alGetString(AL_RENDERER)));
    assert(API(alGetString, 1, AL_RENDERER) == renderer);
    uint32_t name = void_gstrdup("alSourcePlay");
    uint32_t guest_proc = API(alGetProcAddress, 1, name);
    assert(guest_proc && guest_proc <= UINT32_MAX && void_dispatch_find(guest_proc));
    void_gfree(name);
    name = void_gstrdup("UT3_NoSuchAudioAPI");
    assert(!API(alGetProcAddress, 1, name)); void_gfree(name);
    name = void_gstrdup("ALC_EXT_EFX");
    assert(API(alcIsExtensionPresent, 2, dev, name)); void_gfree(name);
    noerror();

    uint32_t scratch = void_galloc(96);
    memset(GPTR(scratch), 0xA5, 96);
    API(alcGetIntegerv, 4, dev, ALC_MAX_AUXILIARY_SENDS, 1, scratch + 4);
    int sends = (int32_t)MEM32(scratch + 4); assert(sends == 5);
    API(alDopplerFactor, 1, f32(0.75f));
    assert(fabs(fp_result("alGetFloat", AL_DOPPLER_FACTOR) - 0.75) < 1e-8);
    assert(fabs(fp_result("alGetDouble", AL_DOPPLER_FACTOR) - 0.75) < 1e-8);
    float orientation[] = {0, 0, -1, 0, 1, 0};
    memcpy(GPTR(scratch + 16), orientation, sizeof orientation);
    API(alListenerfv, 2, AL_ORIENTATION, scratch + 16);
    API(alGetListenerfv, 2, AL_ORIENTATION, scratch + 48);
    assert(!memcmp(GPTR(scratch + 48), orientation, sizeof orientation));

    uint32_t ids = void_galloc(32);
    API(alGenBuffers, 2, 4, ids);
    API(alGenSources, 2, 1, ids + 16);
    API(alGenEffects, 2, 1, ids + 20);
    API(alGenFilters, 2, 1, ids + 24);
    API(alGenAuxiliaryEffectSlots, 2, 1, ids + 28);
    uint32_t source = MEM32(ids + 16), effect = MEM32(ids + 20), filter = MEM32(ids + 24), slot = MEM32(ids + 28);
    assert(API(alIsBuffer, 1, MEM32(ids)) == AL_TRUE && API(alIsSource, 1, source) == AL_TRUE);
    assert(API(alIsEffect, 1, effect) == AL_TRUE && API(alIsFilter, 1, filter) == AL_TRUE);
    assert(API(alIsAuxiliaryEffectSlot, 1, slot) == AL_TRUE);
    API(alEffecti, 3, effect, AL_EFFECT_TYPE, AL_EFFECT_REVERB);
    API(alEffectf, 3, effect, AL_REVERB_DECAY_TIME, f32(0.25f));
    API(alGetEffectf, 3, effect, AL_REVERB_DECAY_TIME, scratch + 4);
    float effect_value; memcpy(&effect_value, GPTR(scratch + 4), 4); assert(effect_value == 0.25f);
    API(alFilteri, 3, filter, AL_FILTER_TYPE, AL_FILTER_LOWPASS);
    API(alFilterf, 3, filter, AL_LOWPASS_GAIN, f32(0.7f));
    API(alAuxiliaryEffectSloti, 3, slot, AL_EFFECTSLOT_EFFECT, effect);
    API(alSource3i, 5, source, AL_AUXILIARY_SEND_FILTER, slot, 0, filter);
    API(alSource3f, 5, source, AL_POSITION, f32(1), f32(0), f32(-1));
    API(alGetSourcefv, 3, source, AL_POSITION, scratch + 4);
    assert(MEM32(scratch + 4) == f32(1) && MEM32(scratch + 8) == f32(0) && MEM32(scratch + 12) == f32(-1));
    API(alSourcef, 3, source, AL_GAIN, f32(0.02f));
    noerror();

    enum { RATE = 44100, FRAMES = 5512 };
    uint32_t pcm = void_galloc(FRAMES * 2);
    for (unsigned i = 0; i < FRAMES; ++i)
        MEM16(pcm + 2 * i) = (uint16_t)(int16_t)(sin((double)i * 2.0 * 3.14159265358979323846 * 440 / RATE) * 4000);
    for (unsigned i = 0; i < 4; ++i) API(alBufferData, 5, MEM32(ids + 4 * i), AL_FORMAT_MONO16, pcm, FRAMES * 2, RATE);
    API(alSourceQueueBuffers, 3, source, 4, ids);
    API(alSourcePlay, 1, source);
    noerror(); nap(170);
    int processed = source_integer(source, AL_BUFFERS_PROCESSED, scratch + 4);
    int playing = source_integer(source, AL_SOURCE_STATE, scratch + 4);
    assert(playing == AL_PLAYING && processed > 0 && processed < 4);
    API(alSourceUnqueueBuffers, 3, source, processed, scratch + 16);
    API(alSourceQueueBuffers, 3, source, processed, scratch + 16);
    API(alSourcePause, 1, source);
    assert(source_integer(source, AL_SOURCE_STATE, scratch + 4) == AL_PAUSED);
    API(alSourcePlay, 1, source);
    noerror(); nap(200);
    ut3_openal_pause(1); nap(30);
    audio_metrics paused = {0}, still_paused = {0}, m = {0};
    assert(get_metrics(&paused, sizeof paused)); nap(60);
    assert(get_metrics(&still_paused, sizeof still_paused));
    assert(still_paused.callbacks <= paused.callbacks + 2);
    ut3_openal_pause(0); nap(120);
    assert(get_metrics(&m, sizeof m));
    assert(m.audio_api == 2 && m.stream_state == 4 && m.callbacks > still_paused.callbacks);
    assert(m.frames > 0 && m.nonzero_samples > 0 && !m.nonfinite_samples && m.peak_abs > 0.0001);
    printf("OPENAL_METRICS_JSON {\"audio_api\":%d,\"stream_state\":%d,\"callbacks\":%llu,\"frames\":%llu,\"nonzero_samples\":%llu,\"nonfinite_samples\":%llu,\"peak_abs\":%.9g}\n",
           m.audio_api, m.stream_state, (unsigned long long)m.callbacks, (unsigned long long)m.frames,
           (unsigned long long)m.nonzero_samples, (unsigned long long)m.nonfinite_samples, m.peak_abs);

    API(alGetListenerf, 2, AL_GAIN, 0x1000);
    API(alEnable, 1, 0xDEAD);
    uint32_t shadow_al_first = API(alGetError, 0, 0), shadow_al_second = API(alGetError, 0, 0);
    API(alcGetIntegerv, 4, dev, ALC_MAJOR_VERSION, 1, 0x1000);
    API(alcGetString, 2, dev, 0xDEAD);
    uint32_t shadow_alc_first = API(alcGetError, 1, dev), shadow_alc_second = API(alcGetError, 1, dev);
    printf("OPENAL_ERROR_EDGES_JSON {\"al_first\":%u,\"al_second\":%u,\"alc_first\":%u,\"alc_second\":%u}\n",
           shadow_al_first, shadow_al_second, shadow_alc_first, shadow_alc_second);
    assert(shadow_al_first == AL_INVALID_VALUE && shadow_al_second == AL_NO_ERROR);
    assert(shadow_alc_first == ALC_INVALID_VALUE && shadow_alc_second == ALC_NO_ERROR);
    N.alcCaptureSamples(device(dev)->native, GPTR(scratch + 16), 1);
    ALCenum native_capture_kind_error = N.alcGetError(device(dev)->native);
    assert(native_capture_kind_error == ALC_INVALID_DEVICE);
    API(alcCaptureSamples, 3, dev, scratch + 16, 1);
    assert(API(alcGetError, 1, dev) == (uint32_t)native_capture_kind_error);

    API(alSourcef, 3, source, 0xDEAD, f32(1));
    API(alSourcefv, 3, source, AL_POSITION, 0x1000);
    assert(API(alGetError, 0, 0) == AL_INVALID_ENUM); noerror();
    API(alGenSources, 2, 1, 0x1000);
    assert(API(alGetError, 0, 0) == AL_INVALID_VALUE); noerror();
    API(alListenerfv, 2, AL_ORIENTATION, 0xFFFFFFF8u);
    assert(API(alGetError, 0, 0) == AL_INVALID_VALUE); noerror();
    assert(!API(alcCaptureOpenDevice, 4, 0, RATE, AL_FORMAT_MONO16, 256));
    uint32_t capture_error = API(alcGetError, 1, 0); assert(capture_error != ALC_NO_ERROR);
    API(alSourceStop, 1, source);
    assert(source_integer(source, AL_BUFFERS_PROCESSED, scratch + 4) == 4);
    API(alSourceUnqueueBuffers, 3, source, 4, scratch + 16);
    API(alSource3i, 5, source, AL_AUXILIARY_SEND_FILTER, 0, 0, 0);
    API(alDeleteSources, 2, 1, ids + 16);
    API(alDeleteAuxiliaryEffectSlots, 2, 1, ids + 28);
    API(alDeleteEffects, 2, 1, ids + 20);
    API(alDeleteFilters, 2, 1, ids + 24);
    API(alDeleteBuffers, 2, 4, ids); noerror();
    ut3_openal_log_stats();
    assert(API(alcMakeContextCurrent, 1, 0));
    API(alcDestroyContext, 1, ctx);
    assert(!API(alcMakeContextCurrent, 1, ctx));
    assert(API(alcGetError, 1, 0) == ALC_INVALID_CONTEXT);
    assert(API(alcCloseDevice, 1, dev));
    assert(!API(alcCloseDevice, 1, dev));
    assert(API(alcGetError, 1, 0) == ALC_INVALID_DEVICE);
    enable_metrics(0);
    printf("OPENAL_TEST_JSON {\"success\":true,\"lookup_core\":85,\"lookup_efx\":33,\"guest_calls\":%u,\"renderer\":\"%s\",\"version\":\"%s\",\"device\":\"%s\",\"aux_sends\":%d,\"processed\":%d,\"capture_error\":%u,\"audio_api\":%d,\"stream_state\":%d,\"callbacks\":%llu,\"frames\":%llu,\"nonzero_samples\":%llu,\"nonfinite_samples\":%llu,\"peak_abs\":%.9g,\"pause_callback_delta\":%llu}\n",
           calls, (char *)GPTR(renderer), (char *)GPTR(version), (char *)GPTR(endpoint), sends, processed, capture_error,
           m.audio_api, m.stream_state, (unsigned long long)m.callbacks, (unsigned long long)m.frames,
           (unsigned long long)m.nonzero_samples, (unsigned long long)m.nonfinite_samples, m.peak_abs,
           (unsigned long long)(still_paused.callbacks - paused.callbacks));
    void_gfree(attrs); void_gfree(scratch); void_gfree(ids); void_gfree(pcm);
    return 0;
}
