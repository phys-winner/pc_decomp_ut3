/* Typed x86 CDECL OpenAL 1.1/EFX bridge to pinned ARM64 OpenAL Soft.
 * Guest pointers and native device/context pointers never share an ABI. */
#define AL_ALEXT_PROTOTYPES 1
#include "host.h"
#include <AL/al.h>
#include <AL/alc.h>
#include <AL/efx.h>
#include "../../reference/audio-runtime/rebuild/android/audio/metrics.h"
#include <dlfcn.h>
#include <strings.h>
_Static_assert(sizeof(ALint)==4 && sizeof(ALuint)==4 && sizeof(ALsizei)==4 && sizeof(ALenum)==4,"AL scalar ABI");
_Static_assert(sizeof(ALboolean)==1 && sizeof(ALCboolean)==1 && sizeof(ALfloat)==4 && sizeof(ALdouble)==8,"AL float/boolean ABI");

#define OPENAL_ROUTES(X) \
    X(alEnable) \
    X(alDisable) \
    X(alIsEnabled) \
    X(alGetString) \
    X(alGetBooleanv) \
    X(alGetIntegerv) \
    X(alGetFloatv) \
    X(alGetDoublev) \
    X(alGetBoolean) \
    X(alGetInteger) \
    X(alGetFloat) \
    X(alGetDouble) \
    X(alGetError) \
    X(alIsExtensionPresent) \
    X(alGetProcAddress) \
    X(alGetEnumValue) \
    X(alListenerf) \
    X(alListener3f) \
    X(alListenerfv) \
    X(alListeneri) \
    X(alListener3i) \
    X(alListeneriv) \
    X(alGetListenerf) \
    X(alGetListener3f) \
    X(alGetListenerfv) \
    X(alGetListeneri) \
    X(alGetListener3i) \
    X(alGetListeneriv) \
    X(alGenSources) \
    X(alDeleteSources) \
    X(alIsSource) \
    X(alSourcef) \
    X(alSource3f) \
    X(alSourcefv) \
    X(alSourcei) \
    X(alSource3i) \
    X(alSourceiv) \
    X(alGetSourcef) \
    X(alGetSource3f) \
    X(alGetSourcefv) \
    X(alGetSourcei) \
    X(alGetSource3i) \
    X(alGetSourceiv) \
    X(alSourcePlayv) \
    X(alSourceStopv) \
    X(alSourceRewindv) \
    X(alSourcePausev) \
    X(alSourcePlay) \
    X(alSourceStop) \
    X(alSourceRewind) \
    X(alSourcePause) \
    X(alSourceQueueBuffers) \
    X(alSourceUnqueueBuffers) \
    X(alGenBuffers) \
    X(alDeleteBuffers) \
    X(alIsBuffer) \
    X(alBufferData) \
    X(alBufferf) \
    X(alBufferi) \
    X(alGetBufferf) \
    X(alGetBufferi) \
    X(alDopplerFactor) \
    X(alDopplerVelocity) \
    X(alSpeedOfSound) \
    X(alDistanceModel) \
    X(alcCreateContext) \
    X(alcMakeContextCurrent) \
    X(alcProcessContext) \
    X(alcSuspendContext) \
    X(alcDestroyContext) \
    X(alcGetCurrentContext) \
    X(alcGetContextsDevice) \
    X(alcOpenDevice) \
    X(alcCloseDevice) \
    X(alcGetError) \
    X(alcIsExtensionPresent) \
    X(alcGetProcAddress) \
    X(alcGetEnumValue) \
    X(alcGetString) \
    X(alcGetIntegerv) \
    X(alcCaptureOpenDevice) \
    X(alcCaptureCloseDevice) \
    X(alcCaptureStart) \
    X(alcCaptureStop) \
    X(alcCaptureSamples) \
    X(alGenEffects) \
    X(alDeleteEffects) \
    X(alIsEffect) \
    X(alEffectf) \
    X(alEffecti) \
    X(alEffectfv) \
    X(alEffectiv) \
    X(alGetEffectf) \
    X(alGetEffecti) \
    X(alGetEffectfv) \
    X(alGetEffectiv) \
    X(alGenFilters) \
    X(alDeleteFilters) \
    X(alIsFilter) \
    X(alFilterf) \
    X(alFilteri) \
    X(alFilterfv) \
    X(alFilteriv) \
    X(alGetFilterf) \
    X(alGetFilteri) \
    X(alGetFilterfv) \
    X(alGetFilteriv) \
    X(alGenAuxiliaryEffectSlots) \
    X(alDeleteAuxiliaryEffectSlots) \
    X(alIsAuxiliaryEffectSlot) \
    X(alAuxiliaryEffectSlotf) \
    X(alAuxiliaryEffectSloti) \
    X(alAuxiliaryEffectSlotfv) \
    X(alAuxiliaryEffectSlotiv) \
    X(alGetAuxiliaryEffectSlotf) \
    X(alGetAuxiliaryEffectSloti) \
    X(alGetAuxiliaryEffectSlotfv) \
    X(alGetAuxiliaryEffectSlotiv)

typedef struct {
#define FIELD(name) __typeof__(&name) name;
    OPENAL_ROUTES(FIELD)
#undef FIELD
} native_functions;
static native_functions N;
static void *native_library;
static pthread_once_t native_once=PTHREAD_ONCE_INIT;
static pthread_mutex_t bridge_lock=PTHREAD_MUTEX_INITIALIZER;
static int native_ok, audio_paused;
extern uint64_t void_audio_pause_state(void) __attribute__((weak));
extern void void_native_worker_affinity_begin(void);
extern void void_native_worker_affinity_end(void);
static void (*native_pause)(ALCdevice *), (*native_resume)(ALCdevice *);
static void (*native_probe_enable)(int);
static int (*native_get_metrics)(FrostpunkAudioMetrics *,uint32_t);
static void load_native(void)
{
    native_library=dlopen("libopenal.so",RTLD_NOW|RTLD_LOCAL);
    if (!native_library) { void_log("[openal] native library unavailable: %s",dlerror()); return; }
    int missing=0;
#define LOAD(name) N.name=(__typeof__(N.name))dlsym(native_library,#name); if(!N.name) { void_log("[openal] missing native %s",#name); ++missing; }
    OPENAL_ROUTES(LOAD)
#undef LOAD
    native_pause=(__typeof__(native_pause))dlsym(native_library,"alcDevicePauseSOFT");
    native_resume=(__typeof__(native_resume))dlsym(native_library,"alcDeviceResumeSOFT");
    native_probe_enable=(__typeof__(native_probe_enable))dlsym(native_library,"FrostpunkAudio_ProbeEnable");
    native_get_metrics=(__typeof__(native_get_metrics))dlsym(native_library,"FrostpunkAudio_GetMetrics");
    if (!native_pause || !native_resume) ++missing;
    native_ok=!missing;
    if(native_ok && native_probe_enable)native_probe_enable(1);
    void_log("[openal] pinned native library %s (85 core + 33 EFX, real Oboe/AAudio)",native_ok?"loaded":"incomplete");
}

typedef struct audio_device {
    uint32_t token; ALCdevice *native; int capture, frame_bytes; ALCenum error;
    struct audio_device *next;
} audio_device;
typedef struct audio_context {
    uint32_t token; ALCcontext *native; audio_device *device; ALenum error;
    struct audio_context *next;
} audio_context;
static audio_device *devices;
static audio_context *contexts;
static uint32_t next_token=0x6A000000u;
static ALCenum null_device_error;
static __thread ALenum null_context_error;
static uint64_t device_opens, context_creates, buffers_generated, source_plays, upload_bytes, nonzero_uploads, observed_al_errors;

static audio_context *current_context(void)
{
    ALCcontext *native=native_ok?N.alcGetCurrentContext():NULL;
    for(audio_context *c=contexts;c;c=c->next) if(c->native==native) return c;
    return NULL;
}
static void al_error(ALenum value)
{
    audio_context *context=current_context(); ALenum *slot=context?&context->error:&null_context_error;
    if(!*slot) { ALenum prior=native_ok?N.alGetError():AL_NO_ERROR; *slot=prior?prior:value; }
}
static void alc_error(audio_device *device,ALCenum value)
{
    ALCenum *slot=device?&device->error:&null_device_error;
    if(!*slot) { ALCenum prior=native_ok?N.alcGetError(device?device->native:NULL):ALC_NO_ERROR; *slot=prior?prior:value; }
}
static audio_device *device(uint32_t token)
{
    if(!token) return NULL;
    for(audio_device *d=devices;d;d=d->next) if(d->token==token) return d;
    alc_error(NULL,ALC_INVALID_DEVICE); return NULL;
}
static audio_context *context(uint32_t token)
{
    if(!token) return NULL;
    for(audio_context *c=contexts;c;c=c->next) if(c->token==token) return c;
    alc_error(NULL,ALC_INVALID_CONTEXT); return NULL;
}
static uint32_t token(void) { if(next_token>=0x6B000000u) return 0; uint32_t result=next_token;next_token+=16;return result; }
static uint32_t wrap_device(ALCdevice *native,int capture,int frame_bytes)
{
    if(!native) return 0;
    audio_device *d=calloc(1,sizeof *d);uint32_t id=token();
    if(!d || !id) { free(d); if(capture) N.alcCaptureCloseDevice(native);else N.alcCloseDevice(native);alc_error(NULL,ALC_OUT_OF_MEMORY);return 0; }
    *d=(audio_device){id,native,capture,frame_bytes,0,devices};devices=d;
    if(!capture && (audio_paused || (void_audio_pause_state && (void_audio_pause_state() & 1u)))) native_pause(native);
    ++device_opens;void_log("[openal] real %s device %08X: %s",capture?"capture":"playback",id,N.alcGetString(native,ALC_DEVICE_SPECIFIER));return id;
}
static void remove_device(audio_device *d)
{
    audio_context **cp=&contexts;
    while(*cp) { if((*cp)->device==d){audio_context *old=*cp;*cp=old->next;free(old);}else cp=&(*cp)->next; }
    audio_device **dp=&devices;while(*dp && *dp!=d) dp=&(*dp)->next;
    if(*dp) *dp=d->next;free(d);
}
static void *checked(uint32_t address,uint64_t bytes,int write,int alc,audio_device *dev)
{
    if(bytes>UINT32_MAX || (!address && bytes) || (bytes && !(write?void_vm_writable(address,(uint32_t)bytes):void_vm_readable(address,(uint32_t)bytes)))) {
        if(alc) alc_error(dev,ALC_INVALID_VALUE);else al_error(AL_INVALID_VALUE);return NULL;
    }
    return address?GPTR(address):NULL;
}
static const char *text(uint32_t address,int alc,audio_device *dev)
{
    if(!address) return NULL;
    for(uint32_t i=0;i<65536;++i) {
        uint64_t at=(uint64_t)address+i;
        if(at>UINT32_MAX || !void_vm_readable((uint32_t)at,1)) break;
        if(!MEM8((uint32_t)at)) return GPTR(address);
    }
    if(alc) alc_error(dev,ALC_INVALID_VALUE);else al_error(AL_INVALID_VALUE);return NULL;
}
static void *array(uint32_t address,int32_t count,unsigned width,int write)
{
    if(count<0){al_error(AL_INVALID_VALUE);return NULL;}
    return checked(address,(uint64_t)(uint32_t)count*width,write,0,NULL);
}
static unsigned vector_count(ALenum param,int listener)
{
    if(param==AL_POSITION || param==AL_VELOCITY || (!listener && param==AL_DIRECTION) || (!listener && param==0x20006)) return 3;
    if(listener && param==AL_ORIENTATION) return 6;
    return 1;
}
static unsigned effect_vector_count(ALuint object,ALenum param)
{
    if(param!=AL_EAXREVERB_REFLECTIONS_PAN && param!=AL_EAXREVERB_LATE_REVERB_PAN) return 1;
    ALint type=0; N.alGetEffecti(object,AL_EFFECT_TYPE,&type);
    return type==AL_EFFECT_EAXREVERB?3:1;
}
static float farg(unsigned index) { uint32_t bits=ARG(index);float value;memcpy(&value,&bits,4);return value; }

typedef struct audio_string { uint32_t address;size_t bytes;struct audio_string *next; } audio_string;
static audio_string *strings;
static uint32_t intern(const char *value,int list)
{
    if(!value) return 0;
    static const char empty_list[2]={0,0};
    if(list && !*value) value=empty_list;
    size_t bytes=0;
    if(list) { if(!*value) bytes=2;else { while(bytes<65535 && (value[bytes] || value[bytes+1])) ++bytes;bytes+=2; } }
    else bytes=strnlen(value,65536)+1;
    if(bytes>65536){al_error(AL_OUT_OF_MEMORY);return 0;}
    for(audio_string *s=strings;s;s=s->next) if(s->bytes==bytes && !memcmp(GPTR(s->address),value,bytes)) return s->address;
    uint32_t address=void_vm_alloc(0,(uint32_t)bytes,0x3000,4);audio_string *entry=calloc(1,sizeof *entry);
    if(!address || !entry){free(entry);al_error(AL_OUT_OF_MEMORY);return 0;}
    if(list && !*value) memset(GPTR(address),0,2);else memcpy(GPTR(address),value,bytes);
    uint32_t old;void_vm_protect(address,(uint32_t)bytes,2,&old);
    *entry=(audio_string){address,bytes,strings};strings=entry;return address;
}
static int word_present(const char *list,const char *word)
{
    if(!list || !word || !*word) return 0;size_t n=strlen(word);
    for(const char *p=list;*p;) { while(*p==' ')++p;const char *e=strchr(p,' ');if(!e)e=p+strlen(p);if((size_t)(e-p)==n&&!memcmp(p,word,n))return 1;p=e; }
    return 0;
}
static int supported_extension(const char *name,int alc)
{
    if(!name) return 0;
    if(alc) return !strcmp(name,"ALC_ENUMERATION_EXT") || !strcmp(name,"ALC_ENUMERATE_ALL_EXT") || !strcmp(name,"ALC_EXT_EFX");
    return !strcmp(name,"AL_EXT_FLOAT32") || !strcmp(name,"AL_EXT_MCFORMATS");
}
static uint32_t extension_string(const char *native,int alc)
{
    if(!native)return 0;
    const char *names=alc?"ALC_ENUMERATION_EXT ALC_ENUMERATE_ALL_EXT ALC_EXT_EFX":"AL_EXT_FLOAT32 AL_EXT_MCFORMATS";
    char result[128]={0},word[64];
    for(const char *p=names;*p;) {const char *e=strchr(p,' ');if(!e)e=p+strlen(p);size_t n=e-p;memcpy(word,p,n);word[n]=0;
        if(word_present(native,word)){if(*result)strcat(result," ");strcat(result,word);}p=*e?e+1:e;}
    return intern(result,0);
}
static uint32_t guest_proc(const char *name);

/* Private Android lifecycle hooks: no guest calls or context ownership switch. */
void ut3_openal_pause(int paused)
{
    pthread_mutex_lock(&bridge_lock);audio_paused=paused!=0;
    if(native_ok) for(audio_device *d=devices;d;d=d->next) if(!d->capture) {if(audio_paused)native_pause(d->native);else native_resume(d->native);}
    pthread_mutex_unlock(&bridge_lock);
}
void ut3_openal_log_stats(void)
{
    pthread_mutex_lock(&bridge_lock);
    void_log("[openal] stats device_opens=%llu context_creates=%llu generated_buffers=%llu submitted_bytes=%llu nonzero_submissions=%llu play_calls=%llu game_errors=%llu paused=%d",
        (unsigned long long)device_opens,(unsigned long long)context_creates,(unsigned long long)buffers_generated,(unsigned long long)upload_bytes,
        (unsigned long long)nonzero_uploads,(unsigned long long)source_plays,(unsigned long long)observed_al_errors,audio_paused);
    FrostpunkAudioMetrics metrics={0};
    if(native_get_metrics && native_get_metrics(&metrics,sizeof metrics) && metrics.struct_bytes==sizeof metrics)
        void_log("[openal] AAudio api=%d state=%d callbacks=%llu frames=%llu nonzero=%llu nonfinite=%llu peak=%.6g xruns=%d device=%d rate=%d channels=%d",
            metrics.audio_api,metrics.stream_state,(unsigned long long)metrics.callbacks,(unsigned long long)metrics.frames,
            (unsigned long long)metrics.nonzero_samples,(unsigned long long)metrics.nonfinite_samples,metrics.peak_abs,
            metrics.xruns,metrics.device_id,metrics.sample_rate,metrics.channels);
    pthread_mutex_unlock(&bridge_lock);
}
static void played(ALuint source)
{
    ++source_plays;
    if(source_plays<=8 && N.alIsSource(source)) {ALint state=0,queued=0;N.alGetSourcei(source,AL_SOURCE_STATE,&state);N.alGetSourcei(source,AL_BUFFERS_QUEUED,&queued);
        void_log("[openal] play source=%u actual_state=%#x queued=%d",source,state,queued);}
}
#define AUDIO_EXPORT(name,fp,allow) \
    static uint32_t name##_impl(void); \
    static void name##_entry(void) { pthread_mutex_lock(&bridge_lock);pthread_once(&native_once,load_native);uint32_t result=0; \
        if(native_ok || (allow)) result=name##_impl(); else {al_error(AL_INVALID_OPERATION);alc_error(NULL,ALC_INVALID_DEVICE);if(fp)fp_push(0);}\
        pthread_mutex_unlock(&bridge_lock);g_eax=result;g_esp+=4; } \
    static const void_shim name##_shim VOID_SHIM_SECTION={"openal32.dll",#name,0,name##_entry}; \
    static uint32_t name##_impl(void)

AUDIO_EXPORT(alEnable,0,0)
{
    N.alEnable((ALenum)ARG(0));return 0;
}

AUDIO_EXPORT(alDisable,0,0)
{
    N.alDisable((ALenum)ARG(0));return 0;
}

AUDIO_EXPORT(alIsEnabled,0,0)
{
    return (uint8_t)N.alIsEnabled((ALenum)ARG(0));
}

AUDIO_EXPORT(alGetString,0,0)
{
    ALenum param=ARG(0);const char *value=N.alGetString(param);return param==AL_EXTENSIONS?extension_string(value,0):intern(value,0);
}

AUDIO_EXPORT(alGetBooleanv,0,0)
{
    ALboolean * p1=array(ARG(1),1,1,1);if((1) && !p1)return 0;
    N.alGetBooleanv((ALenum)ARG(0),p1);return 0;
}

AUDIO_EXPORT(alGetIntegerv,0,0)
{
    ALint * p1=array(ARG(1),1,4,1);if((1) && !p1)return 0;
    N.alGetIntegerv((ALenum)ARG(0),p1);return 0;
}

AUDIO_EXPORT(alGetFloatv,0,0)
{
    ALfloat * p1=array(ARG(1),1,4,1);if((1) && !p1)return 0;
    N.alGetFloatv((ALenum)ARG(0),p1);return 0;
}

AUDIO_EXPORT(alGetDoublev,0,0)
{
    ALdouble * p1=array(ARG(1),1,8,1);if((1) && !p1)return 0;
    N.alGetDoublev((ALenum)ARG(0),p1);return 0;
}

AUDIO_EXPORT(alGetBoolean,0,0)
{
    return (uint8_t)N.alGetBoolean((ALenum)ARG(0));
}

AUDIO_EXPORT(alGetInteger,0,0)
{
    return N.alGetInteger((ALenum)ARG(0));
}

AUDIO_EXPORT(alGetFloat,1,0)
{
    fp_push(N.alGetFloat((ALenum)ARG(0)));return 0;
}

AUDIO_EXPORT(alGetDouble,1,0)
{
    fp_push(N.alGetDouble((ALenum)ARG(0)));return 0;
}

AUDIO_EXPORT(alGetError,0,1)
{
    audio_context *c=current_context();ALenum *slot=c?&c->error:&null_context_error;ALenum result=*slot;*slot=0;
    /* Native errors raised while the bridge's first error was pending must
     * be ignored, as they would be by the native sticky-error slot. */
    if(result && native_ok)N.alGetError();
    if(!result)result=native_ok?N.alGetError():AL_INVALID_OPERATION;
    if(result){++observed_al_errors;if(observed_al_errors<=16)void_log("[openal] game alGetError = %#x",result);}return result;
}

AUDIO_EXPORT(alIsExtensionPresent,0,0)
{
    const char *name=text(ARG(0),0,NULL);if(!name){if(!ARG(0))al_error(AL_INVALID_VALUE);return 0;}
    return supported_extension(name,0)?(uint8_t)N.alIsExtensionPresent(name):0;
}

AUDIO_EXPORT(alGetProcAddress,0,0)
{
    const char *name=text(ARG(0),0,NULL);if(!name){if(!ARG(0))al_error(AL_INVALID_VALUE);return 0;}
    return N.alGetProcAddress(name)?guest_proc(name):0;
}

AUDIO_EXPORT(alGetEnumValue,0,0)
{
    const char *name=text(ARG(0),0,NULL);if(!name){if(!ARG(0))al_error(AL_INVALID_VALUE);return 0;}return N.alGetEnumValue(name);
}

AUDIO_EXPORT(alListenerf,0,0)
{
    N.alListenerf((ALenum)ARG(0),farg(1));return 0;
}

AUDIO_EXPORT(alListener3f,0,0)
{
    N.alListener3f((ALenum)ARG(0),farg(1),farg(2),farg(3));return 0;
}

AUDIO_EXPORT(alListenerfv,0,0)
{
    const ALfloat * p1=array(ARG(1),vector_count(ARG(0),1),4,0);if((vector_count(ARG(0),1)) && !p1)return 0;
    N.alListenerfv((ALenum)ARG(0),p1);return 0;
}

AUDIO_EXPORT(alListeneri,0,0)
{
    N.alListeneri((ALenum)ARG(0),(ALint)ARG(1));return 0;
}

AUDIO_EXPORT(alListener3i,0,0)
{
    N.alListener3i((ALenum)ARG(0),(ALint)ARG(1),(ALint)ARG(2),(ALint)ARG(3));return 0;
}

AUDIO_EXPORT(alListeneriv,0,0)
{
    const ALint * p1=array(ARG(1),vector_count(ARG(0),1),4,0);if((vector_count(ARG(0),1)) && !p1)return 0;
    N.alListeneriv((ALenum)ARG(0),p1);return 0;
}

AUDIO_EXPORT(alGetListenerf,0,0)
{
    ALfloat * p1=array(ARG(1),1,4,1);if((1) && !p1)return 0;
    N.alGetListenerf((ALenum)ARG(0),p1);return 0;
}

AUDIO_EXPORT(alGetListener3f,0,0)
{
    ALfloat * p1=array(ARG(1),1,4,1);if((1) && !p1)return 0;
    ALfloat * p2=array(ARG(2),1,4,1);if((1) && !p2)return 0;
    ALfloat * p3=array(ARG(3),1,4,1);if((1) && !p3)return 0;
    N.alGetListener3f((ALenum)ARG(0),p1,p2,p3);return 0;
}

AUDIO_EXPORT(alGetListenerfv,0,0)
{
    ALfloat * p1=array(ARG(1),vector_count(ARG(0),1),4,1);if((vector_count(ARG(0),1)) && !p1)return 0;
    N.alGetListenerfv((ALenum)ARG(0),p1);return 0;
}

AUDIO_EXPORT(alGetListeneri,0,0)
{
    ALint * p1=array(ARG(1),1,4,1);if((1) && !p1)return 0;
    N.alGetListeneri((ALenum)ARG(0),p1);return 0;
}

AUDIO_EXPORT(alGetListener3i,0,0)
{
    ALint * p1=array(ARG(1),1,4,1);if((1) && !p1)return 0;
    ALint * p2=array(ARG(2),1,4,1);if((1) && !p2)return 0;
    ALint * p3=array(ARG(3),1,4,1);if((1) && !p3)return 0;
    N.alGetListener3i((ALenum)ARG(0),p1,p2,p3);return 0;
}

AUDIO_EXPORT(alGetListeneriv,0,0)
{
    ALint * p1=array(ARG(1),vector_count(ARG(0),1),4,1);if((vector_count(ARG(0),1)) && !p1)return 0;
    N.alGetListeneriv((ALenum)ARG(0),p1);return 0;
}

AUDIO_EXPORT(alGenSources,0,0)
{
    ALuint * p1=array(ARG(1),(int32_t)ARG(0),4,1);if(((int32_t)ARG(0)) && !p1)return 0;
    N.alGenSources((ALsizei)ARG(0),p1);return 0;
}

AUDIO_EXPORT(alDeleteSources,0,0)
{
    const ALuint * p1=array(ARG(1),(int32_t)ARG(0),4,0);if(((int32_t)ARG(0)) && !p1)return 0;
    N.alDeleteSources((ALsizei)ARG(0),p1);return 0;
}

AUDIO_EXPORT(alIsSource,0,0)
{
    return (uint8_t)N.alIsSource((ALuint)ARG(0));
}

AUDIO_EXPORT(alSourcef,0,0)
{
    N.alSourcef((ALuint)ARG(0),(ALenum)ARG(1),farg(2));return 0;
}

AUDIO_EXPORT(alSource3f,0,0)
{
    N.alSource3f((ALuint)ARG(0),(ALenum)ARG(1),farg(2),farg(3),farg(4));return 0;
}

AUDIO_EXPORT(alSourcefv,0,0)
{
    const ALfloat * p2=array(ARG(2),vector_count(ARG(1),0),4,0);if((vector_count(ARG(1),0)) && !p2)return 0;
    N.alSourcefv((ALuint)ARG(0),(ALenum)ARG(1),p2);return 0;
}

AUDIO_EXPORT(alSourcei,0,0)
{
    N.alSourcei((ALuint)ARG(0),(ALenum)ARG(1),(ALint)ARG(2));return 0;
}

AUDIO_EXPORT(alSource3i,0,0)
{
    N.alSource3i((ALuint)ARG(0),(ALenum)ARG(1),(ALint)ARG(2),(ALint)ARG(3),(ALint)ARG(4));return 0;
}

AUDIO_EXPORT(alSourceiv,0,0)
{
    const ALint * p2=array(ARG(2),vector_count(ARG(1),0),4,0);if((vector_count(ARG(1),0)) && !p2)return 0;
    N.alSourceiv((ALuint)ARG(0),(ALenum)ARG(1),p2);return 0;
}

AUDIO_EXPORT(alGetSourcef,0,0)
{
    ALfloat * p2=array(ARG(2),1,4,1);if((1) && !p2)return 0;
    N.alGetSourcef((ALuint)ARG(0),(ALenum)ARG(1),p2);return 0;
}

AUDIO_EXPORT(alGetSource3f,0,0)
{
    ALfloat * p2=array(ARG(2),1,4,1);if((1) && !p2)return 0;
    ALfloat * p3=array(ARG(3),1,4,1);if((1) && !p3)return 0;
    ALfloat * p4=array(ARG(4),1,4,1);if((1) && !p4)return 0;
    N.alGetSource3f((ALuint)ARG(0),(ALenum)ARG(1),p2,p3,p4);return 0;
}

AUDIO_EXPORT(alGetSourcefv,0,0)
{
    ALfloat * p2=array(ARG(2),vector_count(ARG(1),0),4,1);if((vector_count(ARG(1),0)) && !p2)return 0;
    N.alGetSourcefv((ALuint)ARG(0),(ALenum)ARG(1),p2);return 0;
}

AUDIO_EXPORT(alGetSourcei,0,0)
{
    ALint * p2=array(ARG(2),1,4,1);if((1) && !p2)return 0;
    N.alGetSourcei((ALuint)ARG(0),(ALenum)ARG(1),p2);return 0;
}

AUDIO_EXPORT(alGetSource3i,0,0)
{
    ALint * p2=array(ARG(2),1,4,1);if((1) && !p2)return 0;
    ALint * p3=array(ARG(3),1,4,1);if((1) && !p3)return 0;
    ALint * p4=array(ARG(4),1,4,1);if((1) && !p4)return 0;
    N.alGetSource3i((ALuint)ARG(0),(ALenum)ARG(1),p2,p3,p4);return 0;
}

AUDIO_EXPORT(alGetSourceiv,0,0)
{
    ALint * p2=array(ARG(2),vector_count(ARG(1),0),4,1);if((vector_count(ARG(1),0)) && !p2)return 0;
    N.alGetSourceiv((ALuint)ARG(0),(ALenum)ARG(1),p2);return 0;
}

AUDIO_EXPORT(alSourcePlayv,0,0)
{
    const ALuint * p1=array(ARG(1),(int32_t)ARG(0),4,0);if(((int32_t)ARG(0)) && !p1)return 0;
    N.alSourcePlayv((ALsizei)ARG(0),p1);if((int32_t)ARG(0)>0)for(int32_t i=0;i<(int32_t)ARG(0);++i)played(p1[i]);return 0;
}

AUDIO_EXPORT(alSourceStopv,0,0)
{
    const ALuint * p1=array(ARG(1),(int32_t)ARG(0),4,0);if(((int32_t)ARG(0)) && !p1)return 0;
    N.alSourceStopv((ALsizei)ARG(0),p1);return 0;
}

AUDIO_EXPORT(alSourceRewindv,0,0)
{
    const ALuint * p1=array(ARG(1),(int32_t)ARG(0),4,0);if(((int32_t)ARG(0)) && !p1)return 0;
    N.alSourceRewindv((ALsizei)ARG(0),p1);return 0;
}

AUDIO_EXPORT(alSourcePausev,0,0)
{
    const ALuint * p1=array(ARG(1),(int32_t)ARG(0),4,0);if(((int32_t)ARG(0)) && !p1)return 0;
    N.alSourcePausev((ALsizei)ARG(0),p1);return 0;
}

AUDIO_EXPORT(alSourcePlay,0,0)
{
    N.alSourcePlay((ALuint)ARG(0));played(ARG(0));return 0;
}

AUDIO_EXPORT(alSourceStop,0,0)
{
    N.alSourceStop((ALuint)ARG(0));return 0;
}

AUDIO_EXPORT(alSourceRewind,0,0)
{
    N.alSourceRewind((ALuint)ARG(0));return 0;
}

AUDIO_EXPORT(alSourcePause,0,0)
{
    N.alSourcePause((ALuint)ARG(0));return 0;
}

AUDIO_EXPORT(alSourceQueueBuffers,0,0)
{
    const ALuint * p2=array(ARG(2),(int32_t)ARG(1),4,0);if(((int32_t)ARG(1)) && !p2)return 0;
    N.alSourceQueueBuffers((ALuint)ARG(0),(ALsizei)ARG(1),p2);return 0;
}

AUDIO_EXPORT(alSourceUnqueueBuffers,0,0)
{
    ALuint * p2=array(ARG(2),(int32_t)ARG(1),4,1);if(((int32_t)ARG(1)) && !p2)return 0;
    N.alSourceUnqueueBuffers((ALuint)ARG(0),(ALsizei)ARG(1),p2);return 0;
}

AUDIO_EXPORT(alGenBuffers,0,0)
{
    ALuint * p1=array(ARG(1),(int32_t)ARG(0),4,1);if(((int32_t)ARG(0)) && !p1)return 0;
    N.alGenBuffers((ALsizei)ARG(0),p1);if((int32_t)ARG(0)>0)for(int32_t i=0;i<(int32_t)ARG(0);++i)if(N.alIsBuffer(p1[i]))++buffers_generated;return 0;
}

AUDIO_EXPORT(alDeleteBuffers,0,0)
{
    const ALuint * p1=array(ARG(1),(int32_t)ARG(0),4,0);if(((int32_t)ARG(0)) && !p1)return 0;
    N.alDeleteBuffers((ALsizei)ARG(0),p1);return 0;
}

AUDIO_EXPORT(alIsBuffer,0,0)
{
    return (uint8_t)N.alIsBuffer((ALuint)ARG(0));
}

AUDIO_EXPORT(alBufferData,0,0)
{
    ALsizei size=(ALsizei)ARG(3);if(size<0){al_error(AL_INVALID_VALUE);return 0;}
    const void *data=ARG(2)?checked(ARG(2),(uint32_t)size,0,0,NULL):NULL;if(ARG(2)&&!data)return 0;
    N.alBufferData(ARG(0),ARG(1),data,size,(ALsizei)ARG(4));
    if(size>0 && data){upload_bytes+=(uint32_t)size;const uint8_t *bytes=data;int nonzero=0;for(ALsizei i=0;i<size;++i)if(bytes[i]){nonzero=1;break;}nonzero_uploads+=nonzero;
        if(nonzero_uploads<=4)void_log("[openal] submitted buffer=%u format=%#x bytes=%d rate=%u nonzero=%d",ARG(0),ARG(1),size,ARG(4),nonzero);}return 0;
}

AUDIO_EXPORT(alBufferf,0,0)
{
    N.alBufferf((ALuint)ARG(0),(ALenum)ARG(1),farg(2));return 0;
}

AUDIO_EXPORT(alBufferi,0,0)
{
    N.alBufferi((ALuint)ARG(0),(ALenum)ARG(1),(ALint)ARG(2));return 0;
}

AUDIO_EXPORT(alGetBufferf,0,0)
{
    ALfloat * p2=array(ARG(2),1,4,1);if((1) && !p2)return 0;
    N.alGetBufferf((ALuint)ARG(0),(ALenum)ARG(1),p2);return 0;
}

AUDIO_EXPORT(alGetBufferi,0,0)
{
    ALint * p2=array(ARG(2),1,4,1);if((1) && !p2)return 0;
    N.alGetBufferi((ALuint)ARG(0),(ALenum)ARG(1),p2);return 0;
}

AUDIO_EXPORT(alDopplerFactor,0,0)
{
    N.alDopplerFactor(farg(0));return 0;
}

AUDIO_EXPORT(alDopplerVelocity,0,0)
{
    N.alDopplerVelocity(farg(0));return 0;
}

AUDIO_EXPORT(alSpeedOfSound,0,0)
{
    N.alSpeedOfSound(farg(0));return 0;
}

AUDIO_EXPORT(alDistanceModel,0,0)
{
    N.alDistanceModel((ALenum)ARG(0));return 0;
}

AUDIO_EXPORT(alcCreateContext,0,0)
{
    audio_device *d=device(ARG(0));if(!d){if(!ARG(0))alc_error(NULL,ALC_INVALID_DEVICE);return 0;}
    const ALCint *attrs=NULL;if(ARG(1)){uint32_t count=0;for(;;){if(count>=512){alc_error(d,ALC_INVALID_VALUE);return 0;}if(!checked(ARG(1),(uint64_t)(count+1)*4,0,1,d))return 0;
        if(!MEM32(ARG(1)+count*4)){attrs=GPTR(ARG(1));break;}count+=2;}}
    void_native_worker_affinity_begin();
    ALCcontext *native=N.alcCreateContext(d->native,attrs);
    void_native_worker_affinity_end();if(!native)return 0;
    audio_context *c=calloc(1,sizeof *c);uint32_t id=token();if(!c||!id){free(c);N.alcDestroyContext(native);alc_error(d,ALC_OUT_OF_MEMORY);return 0;}
    *c=(audio_context){id,native,d,0,contexts};contexts=c;++context_creates;void_log("[openal] real context %08X for device %08X",id,d->token);return id;
}

AUDIO_EXPORT(alcMakeContextCurrent,0,0)
{
    audio_context *c=context(ARG(0));if(ARG(0)&&!c)return 0;return (uint8_t)N.alcMakeContextCurrent(c?c->native:NULL);
}

AUDIO_EXPORT(alcProcessContext,0,0)
{
    audio_context *c=context(ARG(0));if(ARG(0)&&!c)return 0;N.alcProcessContext(c?c->native:NULL);return 0;
}

AUDIO_EXPORT(alcSuspendContext,0,0)
{
    audio_context *c=context(ARG(0));if(ARG(0)&&!c)return 0;N.alcSuspendContext(c?c->native:NULL);return 0;
}

AUDIO_EXPORT(alcDestroyContext,0,0)
{
    audio_context *c=context(ARG(0));if(!c){if(!ARG(0))N.alcDestroyContext(NULL);return 0;}
    N.alcDestroyContext(c->native);audio_context **link=&contexts;while(*link && *link!=c)link=&(*link)->next;if(*link)*link=c->next;free(c);return 0;
}

AUDIO_EXPORT(alcGetCurrentContext,0,0)
{
    audio_context *c=current_context();return c?c->token:0;
}

AUDIO_EXPORT(alcGetContextsDevice,0,0)
{
    audio_context *c=context(ARG(0));if(!c){if(!ARG(0))alc_error(NULL,ALC_INVALID_CONTEXT);return 0;}return c->device->token;
}

AUDIO_EXPORT(alcOpenDevice,0,0)
{
    const char *name=text(ARG(0),1,NULL);if(ARG(0)&&!name)return 0;
    if(name && !strcasecmp(name,"Generic Software")){void_log("[openal] Generic Software maps explicitly to native default Oboe output");name=NULL;}
    void_native_worker_affinity_begin();
    ALCdevice *native=N.alcOpenDevice(name);
    void_native_worker_affinity_end();
    return wrap_device(native,0,0);
}

AUDIO_EXPORT(alcCloseDevice,0,0)
{
    audio_device *d=device(ARG(0));if(!d){if(!ARG(0))alc_error(NULL,ALC_INVALID_DEVICE);return 0;}
    ALCboolean result=N.alcCloseDevice(d->native);if(result)remove_device(d);return (uint8_t)result;
}

AUDIO_EXPORT(alcGetError,0,1)
{
    audio_device *d=device(ARG(0));if(ARG(0)&&!d)return ALC_INVALID_DEVICE;
    ALCenum *slot=d?&d->error:&null_device_error;ALCenum result=*slot;*slot=0;
    if(result && native_ok)N.alcGetError(d?d->native:NULL);
    if(!result)result=native_ok?N.alcGetError(d?d->native:NULL):ALC_INVALID_DEVICE;return result;
}

AUDIO_EXPORT(alcIsExtensionPresent,0,0)
{
    audio_device *d=device(ARG(0));if(ARG(0)&&!d)return 0;const char *name=text(ARG(1),1,d);
    if(!name){if(!ARG(1))alc_error(d,ALC_INVALID_VALUE);return 0;}return supported_extension(name,1)?(uint8_t)N.alcIsExtensionPresent(d?d->native:NULL,name):0;
}

AUDIO_EXPORT(alcGetProcAddress,0,0)
{
    audio_device *d=device(ARG(0));if(ARG(0)&&!d)return 0;const char *name=text(ARG(1),1,d);
    if(!name){if(!ARG(1))alc_error(d,ALC_INVALID_VALUE);return 0;}return N.alcGetProcAddress(d?d->native:NULL,name)?guest_proc(name):0;
}

AUDIO_EXPORT(alcGetEnumValue,0,0)
{
    audio_device *d=device(ARG(0));if(ARG(0)&&!d)return 0;const char *name=text(ARG(1),1,d);
    if(!name){if(!ARG(1))alc_error(d,ALC_INVALID_VALUE);return 0;}return N.alcGetEnumValue(d?d->native:NULL,name);
}

AUDIO_EXPORT(alcGetString,0,0)
{
    audio_device *d=device(ARG(0));if(ARG(0)&&!d)return 0;ALCenum param=ARG(1);const char *value=N.alcGetString(d?d->native:NULL,param);
    if(param==ALC_EXTENSIONS)return extension_string(value,1);
    int list=!d&&(param==ALC_DEVICE_SPECIFIER || param==ALC_CAPTURE_DEVICE_SPECIFIER || param==0x1013);return intern(value,list);
}

AUDIO_EXPORT(alcGetIntegerv,0,0)
{
    audio_device *d=device(ARG(0));if(ARG(0)&&!d)return 0;ALCsizei count=(ALCsizei)ARG(2);
    if(count<0){alc_error(d,ALC_INVALID_VALUE);return 0;}ALCint *out=checked(ARG(3),(uint64_t)(uint32_t)count*4,1,1,d);if(count&&!out)return 0;
    N.alcGetIntegerv(d?d->native:NULL,ARG(1),count,out);return 0;
}

AUDIO_EXPORT(alcCaptureOpenDevice,0,0)
{
    const char *name=text(ARG(0),1,NULL);if(ARG(0)&&!name)return 0;
    int frame=ARG(2)==AL_FORMAT_MONO8?1:ARG(2)==AL_FORMAT_MONO16||ARG(2)==AL_FORMAT_STEREO8?2:ARG(2)==AL_FORMAT_STEREO16?4:0;
    return wrap_device(N.alcCaptureOpenDevice(name,ARG(1),ARG(2),(ALCsizei)ARG(3)),1,frame);
}

AUDIO_EXPORT(alcCaptureCloseDevice,0,0)
{
    audio_device *d=device(ARG(0));if(!d){if(!ARG(0))alc_error(NULL,ALC_INVALID_DEVICE);return 0;}
    ALCboolean result=N.alcCaptureCloseDevice(d->native);if(result)remove_device(d);return (uint8_t)result;
}

AUDIO_EXPORT(alcCaptureStart,0,0)
{
    audio_device *d=device(ARG(0));if(!d){if(!ARG(0))alc_error(NULL,ALC_INVALID_DEVICE);return 0;}N.alcCaptureStart(d->native);return 0;
}

AUDIO_EXPORT(alcCaptureStop,0,0)
{
    audio_device *d=device(ARG(0));if(!d){if(!ARG(0))alc_error(NULL,ALC_INVALID_DEVICE);return 0;}N.alcCaptureStop(d->native);return 0;
}

AUDIO_EXPORT(alcCaptureSamples,0,0)
{
    audio_device *d=device(ARG(0));if(!d){if(!ARG(0))alc_error(NULL,ALC_INVALID_DEVICE);return 0;}ALCsizei samples=(ALCsizei)ARG(2);
    if(!d->capture){alc_error(d,ALC_INVALID_DEVICE);return 0;}
    if(samples<0 || !d->frame_bytes){alc_error(d,ALC_INVALID_VALUE);return 0;}
    void *out=checked(ARG(1),(uint64_t)(uint32_t)samples*d->frame_bytes,1,1,d);if(samples&&!out)return 0;N.alcCaptureSamples(d->native,out,samples);return 0;
}

AUDIO_EXPORT(alGenEffects,0,0)
{
    ALuint * p1=array(ARG(1),(int32_t)ARG(0),4,1);if(((int32_t)ARG(0)) && !p1)return 0;
    N.alGenEffects((ALsizei)ARG(0),p1);return 0;
}

AUDIO_EXPORT(alDeleteEffects,0,0)
{
    const ALuint * p1=array(ARG(1),(int32_t)ARG(0),4,0);if(((int32_t)ARG(0)) && !p1)return 0;
    N.alDeleteEffects((ALsizei)ARG(0),p1);return 0;
}

AUDIO_EXPORT(alIsEffect,0,0)
{
    return (uint8_t)N.alIsEffect((ALuint)ARG(0));
}

AUDIO_EXPORT(alEffectf,0,0)
{
    N.alEffectf((ALuint)ARG(0),(ALenum)ARG(1),farg(2));return 0;
}

AUDIO_EXPORT(alEffecti,0,0)
{
    N.alEffecti((ALuint)ARG(0),(ALenum)ARG(1),(ALint)ARG(2));return 0;
}

AUDIO_EXPORT(alEffectfv,0,0)
{
    const ALfloat * p2=array(ARG(2),effect_vector_count(ARG(0),ARG(1)),4,0);if((effect_vector_count(ARG(0),ARG(1))) && !p2)return 0;
    N.alEffectfv((ALuint)ARG(0),(ALenum)ARG(1),p2);return 0;
}

AUDIO_EXPORT(alEffectiv,0,0)
{
    const ALint * p2=array(ARG(2),1,4,0);if((1) && !p2)return 0;
    N.alEffectiv((ALuint)ARG(0),(ALenum)ARG(1),p2);return 0;
}

AUDIO_EXPORT(alGetEffectf,0,0)
{
    ALfloat * p2=array(ARG(2),1,4,1);if((1) && !p2)return 0;
    N.alGetEffectf((ALuint)ARG(0),(ALenum)ARG(1),p2);return 0;
}

AUDIO_EXPORT(alGetEffecti,0,0)
{
    ALint * p2=array(ARG(2),1,4,1);if((1) && !p2)return 0;
    N.alGetEffecti((ALuint)ARG(0),(ALenum)ARG(1),p2);return 0;
}

AUDIO_EXPORT(alGetEffectfv,0,0)
{
    ALfloat * p2=array(ARG(2),effect_vector_count(ARG(0),ARG(1)),4,1);if((effect_vector_count(ARG(0),ARG(1))) && !p2)return 0;
    N.alGetEffectfv((ALuint)ARG(0),(ALenum)ARG(1),p2);return 0;
}

AUDIO_EXPORT(alGetEffectiv,0,0)
{
    ALint * p2=array(ARG(2),1,4,1);if((1) && !p2)return 0;
    N.alGetEffectiv((ALuint)ARG(0),(ALenum)ARG(1),p2);return 0;
}

AUDIO_EXPORT(alGenFilters,0,0)
{
    ALuint * p1=array(ARG(1),(int32_t)ARG(0),4,1);if(((int32_t)ARG(0)) && !p1)return 0;
    N.alGenFilters((ALsizei)ARG(0),p1);return 0;
}

AUDIO_EXPORT(alDeleteFilters,0,0)
{
    const ALuint * p1=array(ARG(1),(int32_t)ARG(0),4,0);if(((int32_t)ARG(0)) && !p1)return 0;
    N.alDeleteFilters((ALsizei)ARG(0),p1);return 0;
}

AUDIO_EXPORT(alIsFilter,0,0)
{
    return (uint8_t)N.alIsFilter((ALuint)ARG(0));
}

AUDIO_EXPORT(alFilterf,0,0)
{
    N.alFilterf((ALuint)ARG(0),(ALenum)ARG(1),farg(2));return 0;
}

AUDIO_EXPORT(alFilteri,0,0)
{
    N.alFilteri((ALuint)ARG(0),(ALenum)ARG(1),(ALint)ARG(2));return 0;
}

AUDIO_EXPORT(alFilterfv,0,0)
{
    const ALfloat * p2=array(ARG(2),1,4,0);if((1) && !p2)return 0;
    N.alFilterfv((ALuint)ARG(0),(ALenum)ARG(1),p2);return 0;
}

AUDIO_EXPORT(alFilteriv,0,0)
{
    const ALint * p2=array(ARG(2),1,4,0);if((1) && !p2)return 0;
    N.alFilteriv((ALuint)ARG(0),(ALenum)ARG(1),p2);return 0;
}

AUDIO_EXPORT(alGetFilterf,0,0)
{
    ALfloat * p2=array(ARG(2),1,4,1);if((1) && !p2)return 0;
    N.alGetFilterf((ALuint)ARG(0),(ALenum)ARG(1),p2);return 0;
}

AUDIO_EXPORT(alGetFilteri,0,0)
{
    ALint * p2=array(ARG(2),1,4,1);if((1) && !p2)return 0;
    N.alGetFilteri((ALuint)ARG(0),(ALenum)ARG(1),p2);return 0;
}

AUDIO_EXPORT(alGetFilterfv,0,0)
{
    ALfloat * p2=array(ARG(2),1,4,1);if((1) && !p2)return 0;
    N.alGetFilterfv((ALuint)ARG(0),(ALenum)ARG(1),p2);return 0;
}

AUDIO_EXPORT(alGetFilteriv,0,0)
{
    ALint * p2=array(ARG(2),1,4,1);if((1) && !p2)return 0;
    N.alGetFilteriv((ALuint)ARG(0),(ALenum)ARG(1),p2);return 0;
}

AUDIO_EXPORT(alGenAuxiliaryEffectSlots,0,0)
{
    ALuint * p1=array(ARG(1),(int32_t)ARG(0),4,1);if(((int32_t)ARG(0)) && !p1)return 0;
    N.alGenAuxiliaryEffectSlots((ALsizei)ARG(0),p1);return 0;
}

AUDIO_EXPORT(alDeleteAuxiliaryEffectSlots,0,0)
{
    const ALuint * p1=array(ARG(1),(int32_t)ARG(0),4,0);if(((int32_t)ARG(0)) && !p1)return 0;
    N.alDeleteAuxiliaryEffectSlots((ALsizei)ARG(0),p1);return 0;
}

AUDIO_EXPORT(alIsAuxiliaryEffectSlot,0,0)
{
    return (uint8_t)N.alIsAuxiliaryEffectSlot((ALuint)ARG(0));
}

AUDIO_EXPORT(alAuxiliaryEffectSlotf,0,0)
{
    N.alAuxiliaryEffectSlotf((ALuint)ARG(0),(ALenum)ARG(1),farg(2));return 0;
}

AUDIO_EXPORT(alAuxiliaryEffectSloti,0,0)
{
    N.alAuxiliaryEffectSloti((ALuint)ARG(0),(ALenum)ARG(1),(ALint)ARG(2));return 0;
}

AUDIO_EXPORT(alAuxiliaryEffectSlotfv,0,0)
{
    const ALfloat * p2=array(ARG(2),1,4,0);if((1) && !p2)return 0;
    N.alAuxiliaryEffectSlotfv((ALuint)ARG(0),(ALenum)ARG(1),p2);return 0;
}

AUDIO_EXPORT(alAuxiliaryEffectSlotiv,0,0)
{
    const ALint * p2=array(ARG(2),1,4,0);if((1) && !p2)return 0;
    N.alAuxiliaryEffectSlotiv((ALuint)ARG(0),(ALenum)ARG(1),p2);return 0;
}

AUDIO_EXPORT(alGetAuxiliaryEffectSlotf,0,0)
{
    ALfloat * p2=array(ARG(2),1,4,1);if((1) && !p2)return 0;
    N.alGetAuxiliaryEffectSlotf((ALuint)ARG(0),(ALenum)ARG(1),p2);return 0;
}

AUDIO_EXPORT(alGetAuxiliaryEffectSloti,0,0)
{
    ALint * p2=array(ARG(2),1,4,1);if((1) && !p2)return 0;
    N.alGetAuxiliaryEffectSloti((ALuint)ARG(0),(ALenum)ARG(1),p2);return 0;
}

AUDIO_EXPORT(alGetAuxiliaryEffectSlotfv,0,0)
{
    ALfloat * p2=array(ARG(2),1,4,1);if((1) && !p2)return 0;
    N.alGetAuxiliaryEffectSlotfv((ALuint)ARG(0),(ALenum)ARG(1),p2);return 0;
}

AUDIO_EXPORT(alGetAuxiliaryEffectSlotiv,0,0)
{
    ALint * p2=array(ARG(2),1,4,1);if((1) && !p2)return 0;
    N.alGetAuxiliaryEffectSlotiv((ALuint)ARG(0),(ALenum)ARG(1),p2);return 0;
}

typedef struct {const char *name;void_fn function;uint32_t guest;} audio_route;
static audio_route routes[]={
#define ROUTE(name) {#name,name##_entry,0},
    OPENAL_ROUTES(ROUTE)
#undef ROUTE
};
static uint32_t guest_proc(const char *name)
{
    for(size_t i=0;i<sizeof routes/sizeof *routes;++i)if(!strcmp(name,routes[i].name)){
        if(!routes[i].guest)routes[i].guest=void_thunk(routes[i].function,routes[i].name);return routes[i].guest;
    }
    return 0;
}
