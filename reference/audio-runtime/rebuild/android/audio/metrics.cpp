/* Opt-in measurements of a real backend callback, not an audio implementation. */
#include "metrics.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <ctime>
namespace {
std::atomic<bool> enabled{false};
std::atomic<uint64_t> callbacks{},frames{},nonzero{},nonfinite{},cpu{},wall{};
std::atomic<int32_t> api{},state{},channels{},rate{},format{},burst{},performance{},sharing{},device{},xruns{};
std::atomic<int64_t> written{},read{};
std::atomic<double> peak{},squares{};
}
void FrostpunkAudio_ProbeEnable(int value){enabled.store(value!=0,std::memory_order_release);}
int FrostpunkAudio_ProbeEnabled(void){return enabled.load(std::memory_order_acquire);}
uint64_t FrostpunkAudio_Clock(int thread_cpu){timespec t{};if(clock_gettime(thread_cpu?CLOCK_THREAD_CPUTIME_ID:CLOCK_MONOTONIC,&t))return 0;return uint64_t(t.tv_sec)*1000000000u+uint64_t(t.tv_nsec);}
void FrostpunkAudio_RecordStream(int a,int s,int c,int r,int f,int b,int p,int h,int d){api.store(a);state.store(s);channels.store(c);rate.store(r);format.store(f);burst.store(b);performance.store(p);sharing.store(h);device.store(d);}
void FrostpunkAudio_RecordCallback(const void *pcm,int count,int c,int f,uint64_t cpu_ns,uint64_t wall_ns,int s,int x,int64_t w,int64_t r){
    if(!FrostpunkAudio_ProbeEnabled())return;
    uint64_t nz=0,nf=0;double maximum=0,sum=0;
    for(int64_t i=0;i<int64_t(count)*c;++i){double value=0;
        if(f==1)value=static_cast<const int16_t*>(pcm)[i]/32768.0;
        else if(f==2)value=static_cast<const float*>(pcm)[i];
        else if(f==4)value=static_cast<const int32_t*>(pcm)[i]/2147483648.0;
        else{++nf;continue;}
        if(!std::isfinite(value)){++nf;continue;}nz+=value!=0;maximum=std::max(maximum,std::abs(value));sum+=value*value;
    }
    callbacks.fetch_add(1,std::memory_order_relaxed);frames.fetch_add(uint64_t(count),std::memory_order_relaxed);nonzero.fetch_add(nz,std::memory_order_relaxed);nonfinite.fetch_add(nf,std::memory_order_relaxed);cpu.fetch_add(cpu_ns,std::memory_order_relaxed);wall.fetch_add(wall_ns,std::memory_order_relaxed);squares.fetch_add(sum,std::memory_order_relaxed);
    double old=peak.load(std::memory_order_relaxed);while(old<maximum&&!peak.compare_exchange_weak(old,maximum,std::memory_order_relaxed)){}
    state.store(s,std::memory_order_relaxed);xruns.store(x,std::memory_order_relaxed);written.store(w,std::memory_order_relaxed);read.store(r,std::memory_order_relaxed);
}
int FrostpunkAudio_GetMetrics(FrostpunkAudioMetrics*out,uint32_t bytes){
    if(!out||bytes!=sizeof(*out))return 0;FrostpunkAudioMetrics m{};m.abi_version=1;m.struct_bytes=sizeof(m);
    m.callbacks=callbacks.load();m.frames=frames.load();m.nonzero_samples=nonzero.load();m.nonfinite_samples=nonfinite.load();m.render_cpu_ns=cpu.load();m.render_wall_ns=wall.load();m.audio_api=api.load();m.stream_state=state.load();m.channels=channels.load();m.sample_rate=rate.load();m.format=format.load();m.frames_per_burst=burst.load();m.performance_mode=performance.load();m.sharing_mode=sharing.load();m.device_id=device.load();m.xruns=xruns.load();m.frames_written=written.load();m.frames_read=read.load();m.peak_abs=peak.load();m.sum_squares=squares.load();std::memcpy(out,&m,sizeof(m));return 1;
}
