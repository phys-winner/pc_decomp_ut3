/* Native C API/AAudio proof. Does not load the game, guestCPU, Wine or Box64. */
#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#include "metrics.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <chrono>
#include <vector>
static void check(bool ok,const char*why){if(!ok){std::fprintf(stderr,"AUDIO_PROBE_FAIL %s AL=%x ALC=%x\n",why,alGetError(),alcGetError(nullptr));std::fflush(stderr);std::_Exit(1);}}
static void noerror(const char*why){check(alGetError()==AL_NO_ERROR,why);}
int main(){
    static_assert(sizeof(ALint)==4&&sizeof(ALuint)==4&&sizeof(ALfloat)==4&&sizeof(ALdouble)==8&&sizeof(ALboolean)==1&&sizeof(ALCboolean)==1&&sizeof(void*)==8);
    FrostpunkAudio_ProbeEnable(1);ALCdevice*device=alcOpenDevice(nullptr);check(device!=nullptr,"real AAudio device open");
    ALCint attrs[]={ALC_SYNC,ALC_TRUE,0};ALCcontext*context=alcCreateContext(device,attrs);check(context&&alcMakeContextCurrent(context),"original sync context attributes/make current");check(alcGetError(device)==ALC_NO_ERROR,"context ALC error");
    const char*renderer=alGetString(AL_RENDERER);const char*version=alGetString(AL_VERSION);const char*name=alcGetString(device,ALC_DEVICE_SPECIFIER);check(renderer&&version&&name,"real strings");
    check(alcGetProcAddress(nullptr,"alBufferData")!=nullptr,"actual native procedure lookup");check(alcGetProcAddress(nullptr,"Frostpunk_NoSuchAudioProcedure_323190")==nullptr,"unknown procedure lookup");
    float position[]={0,0,0},orientation[]={0,0,-1,0,1,0},returned[6]={};alListenerfv(AL_POSITION,position);alListenerfv(AL_ORIENTATION,orientation);alGetListenerfv(AL_ORIENTATION,returned);check(!std::memcmp(returned,orientation,sizeof(orientation)),"listener orientation float-vector roundtrip");float x=1,y=1,z=1;alGetListener3f(AL_POSITION,&x,&y,&z);check(x==0&&y==0&&z==0,"listener three pointer outputs");alDistanceModel(AL_INVERSE_DISTANCE_CLAMPED);noerror("listener/model");
    ALuint buffers[4],source;alGenBuffers(4,buffers);alGenSources(1,&source);noerror("actual buffer/source allocation");
    constexpr int sample_rate=48000,block=4800;std::vector<int16_t>pcm(block*2);
    for(int i=0;i<block;++i){int16_t sample=int16_t(std::sin(double(i)*2.0*3.14159265358979323846*440/sample_rate)*7000);pcm[2*i]=sample;pcm[2*i+1]=sample;}
    for(unsigned i=0;i<4;++i)alBufferData(buffers[i],AL_FORMAT_STEREO16,pcm.data(),ALsizei(pcm.size()*sizeof(int16_t)),sample_rate);
    alSourcef(source,AL_GAIN,0.08f);alSourcei(source,AL_SOURCE_RELATIVE,AL_TRUE);float source_position[]={0,0,-1};alSourcefv(source,AL_POSITION,source_position);alSourceQueueBuffers(source,4,buffers);alSourcePlay(source);noerror("PCM upload/queue/play");
    std::this_thread::sleep_for(std::chrono::milliseconds(250));ALint playing=0,processed=0;alGetSourcei(source,AL_SOURCE_STATE,&playing);alGetSourcei(source,AL_BUFFERS_PROCESSED,&processed);check(playing==AL_PLAYING&&processed>0,"actual source playback/queue progress");
    std::vector<ALuint>recycled(static_cast<size_t>(processed));alSourceUnqueueBuffers(source,processed,recycled.data());alSourceQueueBuffers(source,processed,recycled.data());noerror("streaming unqueue/requeue");alcSuspendContext(context);alcProcessContext(context);
    std::this_thread::sleep_for(std::chrono::milliseconds(180));FrostpunkAudioMetrics m{};check(FrostpunkAudio_GetMetrics(&m,sizeof(m))!=0,"metrics ABI");
    check(m.audio_api==2,"underlying AudioApi must actually be AAudio");check(m.stream_state==4,"AAudio stream actually STARTED");check(m.callbacks>0&&m.frames>0&&m.nonzero_samples>0&&m.peak_abs>0.001&&m.nonfinite_samples==0,"real callback rendered finite nonzero PCM above dither floor");
    alSourceStop(source);alGetSourcei(source,AL_BUFFERS_PROCESSED,&processed);check(processed==4,"stop completes queued buffers");ALuint unqueued[4];alSourceUnqueueBuffers(source,4,unqueued);alDeleteSources(1,&source);alDeleteBuffers(4,buffers);noerror("stop/queue/source/buffer cleanup");
    double audio_ns=double(m.frames)*1000000000.0/m.sample_rate;
    std::printf("{\"success\":true,\"renderer\":\"%s\",\"version\":\"%s\",\"device\":\"%s\",\"audio_api\":%d,\"stream_state\":%d,\"callbacks\":%llu,\"frames\":%llu,\"nonzero_samples\":%llu,\"nonfinite_samples\":%llu,\"sample_rate\":%d,\"channels\":%d,\"format\":%d,\"frames_per_burst\":%d,\"performance_mode\":%d,\"sharing_mode\":%d,\"device_id\":%d,\"xruns\":%d,\"frames_written\":%lld,\"frames_read\":%lld,\"peak_abs\":%.9g,\"render_cpu_ns\":%llu,\"render_wall_ns\":%llu,\"render_cpu_percent_audio_time\":%.6f,\"original_x64_executed\":false,\"game_boot_proven\":false}\n",renderer,version,name,m.audio_api,m.stream_state,(unsigned long long)m.callbacks,(unsigned long long)m.frames,(unsigned long long)m.nonzero_samples,(unsigned long long)m.nonfinite_samples,m.sample_rate,m.channels,m.format,m.frames_per_burst,m.performance_mode,m.sharing_mode,m.device_id,m.xruns,(long long)m.frames_written,(long long)m.frames_read,m.peak_abs,(unsigned long long)m.render_cpu_ns,(unsigned long long)m.render_wall_ns,100.0*m.render_cpu_ns/audio_ns);
    check(alcMakeContextCurrent(nullptr)!=ALC_FALSE,"clear current context");alcDestroyContext(context);check(alcCloseDevice(device)!=ALC_FALSE,"close real device");return 0;
}
