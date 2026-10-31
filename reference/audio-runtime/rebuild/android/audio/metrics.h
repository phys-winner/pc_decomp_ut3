#pragma once
#include <stdint.h>
#if defined(__cplusplus)
extern "C" {
#endif
typedef struct FrostpunkAudioMetrics {
    uint32_t abi_version,struct_bytes;
    uint64_t callbacks,frames,nonzero_samples,nonfinite_samples,render_cpu_ns,render_wall_ns;
    int32_t audio_api,stream_state,channels,sample_rate,format,frames_per_burst,performance_mode,sharing_mode,device_id,xruns;
    int64_t frames_written,frames_read;
    double peak_abs,sum_squares;
} FrostpunkAudioMetrics;
__attribute__((visibility("default"))) void FrostpunkAudio_ProbeEnable(int enabled);
__attribute__((visibility("default"))) int FrostpunkAudio_GetMetrics(FrostpunkAudioMetrics *out,uint32_t bytes);
/* Backend-only instrumentation. Disabled by default; no guest/Win64 ABI here. */
int FrostpunkAudio_ProbeEnabled(void);
uint64_t FrostpunkAudio_Clock(int thread_cpu);
void FrostpunkAudio_RecordStream(int api,int state,int channels,int rate,int format,int burst,int performance,int sharing,int device);
void FrostpunkAudio_RecordCallback(const void *pcm,int frames,int channels,int format,uint64_t cpu_ns,uint64_t wall_ns,int state,int xruns,int64_t written,int64_t read);
#if defined(__cplusplus)
}
#endif
