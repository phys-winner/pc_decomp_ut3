/* Typed x86 D3D9 -> native ARM64 DXVK boundary. Native pointers never enter guest memory. */
#include <d3d9.h>
#include <android/native_window.h>
#include <dlfcn.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <time.h>
#include <limits>
#include <map>
#include <mutex>
#include <atomic>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <vector>
#include "turnip.inc"

extern "C" {
void *ut3_dxvk_guest_memory(uint32_t,uint32_t,int);
uint32_t ut3_dxvk_guest_alloc(uint32_t);
void ut3_dxvk_guest_free(uint32_t);
void ut3_dxvk_host_log(const char *);
void *ut3_dxvk_unwrap_guest(uint32_t);
uint32_t ut3_dxvk_wrap_native(unsigned,void *);
void *ut3_dxvk_native_window();
void void_android_frame_presented();
void void_input_set_screen_size(int,int);
int void_android_render_ready();
void void_android_window_ack(uint32_t);
void *void_android_window_current(uint32_t *,int);
uint64_t void_lock_wait_ns();
void void_native_worker_affinity_begin();
void void_native_worker_affinity_end();
int void_native_worker_affinity_apply();
}
#include "bridge_metadata.inc"

namespace {
template<class T>T *memory(uint32_t va,size_t count=1,bool write=false) {
  if(!va)return nullptr;
  if(count>UINT32_MAX/sizeof(T))throw std::runtime_error("guest span overflow");
  return static_cast<T*>(ut3_dxvk_guest_memory(va,uint32_t(count*sizeof(T)),write));
}
template<class T>T &ref(uint32_t va,bool write=false) {
  if(!va)throw std::runtime_error("NULL required guest pointer");
  return *memory<T>(va,1,write);
}
uint32_t narrow_size(uint64_t n){if(n>UINT32_MAX)throw std::runtime_error("guest size overflow");return uint32_t(n);}
template<class T>uint32_t wrap(T *p){
  if(!p)return 0;
  if constexpr(std::is_same_v<T,IDirect3DBaseTexture9>||std::is_same_v<T,IDirect3DResource9>){
    switch(p->GetType()){
      case D3DRTYPE_TEXTURE:return ut3_dxvk_wrap_native(unsigned(Kind::IDirect3DTexture9),p);
      case D3DRTYPE_CUBETEXTURE:return ut3_dxvk_wrap_native(unsigned(Kind::IDirect3DCubeTexture9),p);
      case D3DRTYPE_VOLUMETEXTURE:return ut3_dxvk_wrap_native(unsigned(Kind::IDirect3DVolumeTexture9),p);
      case D3DRTYPE_SURFACE:return ut3_dxvk_wrap_native(unsigned(Kind::IDirect3DSurface9),p);
      case D3DRTYPE_VERTEXBUFFER:return ut3_dxvk_wrap_native(unsigned(Kind::IDirect3DVertexBuffer9),p);
      case D3DRTYPE_INDEXBUFFER:return ut3_dxvk_wrap_native(unsigned(Kind::IDirect3DIndexBuffer9),p);
      default:throw std::runtime_error("unknown D3D9 resource type");
    }
  }
  return ut3_dxvk_wrap_native(unsigned(KindOf<T>::value),p);
}
template<class T>T *object(uint32_t va){return static_cast<T*>(ut3_dxvk_unwrap_guest(va));}
struct Context{Kind kind;void *self;unsigned slot;const uint32_t *a;};
struct WorkerAffinityScope {
  WorkerAffinityScope(){void_native_worker_affinity_begin();}
  ~WorkerAffinityScope(){void_native_worker_affinity_end();}
};
std::atomic<uint64_t> render_cpu_ns{},machine_wait_ns{},present_cpu_ns{},present_wall_ns{},native_calls{},draw_calls{},query_polls{};
uint64_t clock_ns(clockid_t clock) {
  struct timespec t{};
  if(clock_gettime(clock,&t))return 0;
  return uint64_t(t.tv_sec)*1000000000ull+uint64_t(t.tv_nsec);
}
void sample_render_thread() {
  static thread_local uint64_t previous_cpu,previous_wait;
  uint64_t cpu=clock_ns(CLOCK_THREAD_CPUTIME_ID),wait=void_lock_wait_ns();
  if(previous_cpu&&cpu>=previous_cpu)render_cpu_ns.fetch_add(cpu-previous_cpu,std::memory_order_relaxed);
  if(previous_cpu&&wait>=previous_wait)machine_wait_ns.fetch_add(wait-previous_wait,std::memory_order_relaxed);
  previous_cpu=cpu;previous_wait=wait;
}
struct PresentTime {
  uint64_t cpu=clock_ns(CLOCK_THREAD_CPUTIME_ID),wall=clock_ns(CLOCK_MONOTONIC);
  ~PresentTime(){
    present_cpu_ns.fetch_add(clock_ns(CLOCK_THREAD_CPUTIME_ID)-cpu,std::memory_order_relaxed);
    present_wall_ns.fetch_add(clock_ns(CLOCK_MONOTONIC)-wall,std::memory_order_relaxed);
    sample_render_thread();
  }
};
std::atomic<uint32_t> display_width{640},display_height{480};
uint32_t render_dimension(const char *name,uint32_t fallback) {
  const char *value=getenv(name);if(!value||!*value)return fallback;
  char *end=nullptr;errno=0;unsigned long result=strtoul(value,&end,10);
  return errno||!end||*end||result<16||result>16384?fallback:uint32_t(result);
}
RECT aspect_destination(int width,int height,uint32_t logical_width,uint32_t logical_height) {
  double scale=std::min(double(width)/logical_width,double(height)/logical_height);
  int rw=int(std::lround(logical_width*scale)),rh=int(std::lround(logical_height*scale));
  return {LONG((width-rw)/2),LONG((height-rh)/2),LONG((width+rw)/2),LONG((height+rh)/2)};
}
std::map<void*,ANativeWindow*> device_windows;
std::map<void*,std::array<uint32_t,14>> parameters;
struct WindowLease {
  ANativeWindow *value;
  WindowLease():value(static_cast<ANativeWindow*>(ut3_dxvk_native_window())){}
  ~WindowLease(){if(value)ANativeWindow_release(value);}
  ANativeWindow *take(){auto *p=value;value=nullptr;return p;}
};
ANativeWindow *device_window(IDirect3DDevice9 *device) {
  auto it=device_windows.find(device);return it==device_windows.end()?nullptr:it->second;
}
ANativeWindow *context_window(const Context &c) {
  if(c.kind==Kind::IDirect3DDevice9)return device_window(static_cast<IDirect3DDevice9*>(c.self));
  IDirect3DDevice9 *device=nullptr;
  HRESULT hr=static_cast<IDirect3DSwapChain9*>(c.self)->GetDevice(&device);
  if(FAILED(hr)||!device)return nullptr;
  auto *window=device_window(device);device->Release();return window;
}
uint32_t surface_status(const Context &c) {
  uint32_t serial;void *current=void_android_window_current(&serial,0);
  if(!current||!void_android_render_ready()){void_android_window_ack(serial);return D3DERR_DEVICELOST;}
  if(current!=context_window(c)){void_android_window_ack(serial);return D3DERR_DEVICENOTRESET;}
  return D3D_OK;
}

size_t elements(const Context &c,const char *method,size_t index) {
  if(index==1&&c.kind==Kind::IDirect3DDevice9&&(c.slot==94||c.slot==109))return size_t(c.a[2])*4;
  if((strstr(method,"ShaderConstantF")||strstr(method,"ShaderConstantI"))&&index==1)return size_t(c.a[2])*4;
  if(strstr(method,"ShaderConstantB")&&index==1)return c.a[2];
  if((!strcmp(method,"SetClipPlane")||!strcmp(method,"GetClipPlane"))&&index==1)return 4;
  if((!strcmp(method,"SetPaletteEntries")||!strcmp(method,"GetPaletteEntries"))&&index==1)return 256;
  if(!strcmp(method,"Clear")&&index==1)return c.a[0];
  if(!strcmp(method,"GetDeclaration")&&index==0)return ref<UINT>(c.a[1]);
  return 1;
}
template<class T,class=void>struct IsInterface:std::false_type{};
template<class T>struct IsInterface<T,std::void_t<decltype(KindOf<std::remove_cv_t<T>>::value)>>:std::true_type{};
template<class T,class Enable=void>struct Argument {
  T value;
  Argument(const Context &c,size_t i,const char*) {
    if constexpr(std::is_same_v<T,float>)memcpy(&value,&c.a[i],4);
    else value=T(c.a[i]);
  }
  operator T(){return value;}
  void finish(bool){}
};
template<class T>struct Argument<T*,std::enable_if_t<!IsInterface<T>::value&&!std::is_pointer_v<T>>> {
  T *value;
  Argument(const Context &c,size_t i,const char *name):value(memory<T>(c.a[i],elements(c,name,i),!std::is_const_v<T>)){}
  operator T*(){return value;}
  void finish(bool){}
};
template<class T>struct Argument<T*,std::enable_if_t<IsInterface<T>::value>> {
  T *value;
  Argument(const Context &c,size_t i,const char*):value(object<T>(c.a[i])){}
  operator T*(){return value;}
  void finish(bool){}
};
template<class T>struct Argument<T**,std::enable_if_t<IsInterface<T>::value>> {
  T *value=nullptr;uint32_t *out;
  Argument(const Context &c,size_t i,const char*):out(memory<uint32_t>(c.a[i],1,true)){}
  operator T**(){return out?&value:nullptr;}
  void finish(bool ok){if(out){*out=ok?wrap(value):0;if(!ok&&value)value->Release();}}
};
template<class T>struct Argument<const T&,void> {
  const T &value;
  Argument(const Context &c,size_t i,const char*):value(ref<T>(c.a[i])){}
  operator const T&(){return value;}
  void finish(bool){}
};
template<>struct Argument<void**,void> {
  void **value=nullptr;
  Argument(const Context &c,size_t i,const char*) {if(c.a[i])throw std::runtime_error("shared Windows handle unsupported");}
  operator void**(){return value;}
  void finish(bool){}
};
template<class R>uint32_t returned(R value) {
  if constexpr(std::is_same_v<R,float>){uint32_t out;memcpy(&out,&value,4);return out;}
  else return uint32_t(value);
}
template<class O,class R,class...A,size_t...I>
uint32_t invoke_impl(Context &c,R(O::*method)(A...),const char *name,std::index_sequence<I...>) {
  std::tuple<Argument<A>...> args{Argument<A>(c,I,name)...};
  if constexpr(std::is_void_v<R>){(static_cast<O*>(c.self)->*method)(static_cast<A>(std::get<I>(args))...);(std::get<I>(args).finish(true),...);return 0;}
  else {R result=(static_cast<O*>(c.self)->*method)(static_cast<A>(std::get<I>(args))...);bool ok=true;if constexpr(std::is_same_v<R,HRESULT>)ok=SUCCEEDED(result);(std::get<I>(args).finish(ok),...);return returned(result);}
}
template<class O,class R,class...A>uint32_t invoke(Context &c,R(O::*method)(A...),const char *name){return invoke_impl(c,method,name,std::index_sequence_for<A...>{});}
template<class O,class R,class...A>uint32_t create_resource(Context &c,R(O::*method)(A...),const char *name){return invoke(c,method,name);}

D3DPRESENT_PARAMETERS native_parameters(uint32_t va,ANativeWindow *window) {
  auto *p=memory<uint32_t>(va,14,true);if(!p)throw std::runtime_error("NULL presentation parameters");
  if(!window)throw std::runtime_error("Android surface unavailable");
  D3DPRESENT_PARAMETERS v{};
  v.BackBufferWidth=p[0];v.BackBufferHeight=p[1];v.BackBufferFormat=D3DFORMAT(p[2]);v.BackBufferCount=p[3];
  v.MultiSampleType=D3DMULTISAMPLE_TYPE(p[4]);v.MultiSampleQuality=p[5];v.SwapEffect=D3DSWAPEFFECT(p[6]);v.hDeviceWindow=window;
  v.Windowed=TRUE;v.EnableAutoDepthStencil=p[9];v.AutoDepthStencilFormat=D3DFORMAT(p[10]);v.Flags=p[11];v.FullScreen_RefreshRateInHz=0;v.PresentationInterval=p[13];
  display_width=v.BackBufferWidth? v.BackBufferWidth:640;display_height=v.BackBufferHeight?v.BackBufferHeight:480;
  void_input_set_screen_size(display_width,display_height);
  return v;
}
void save_parameters(void *key,uint32_t va,const D3DPRESENT_PARAMETERS &p) {
  auto *original=memory<uint32_t>(va,14,true);std::array<uint32_t,14> copy;
  std::copy(original,original+14,copy.begin());copy[0]=p.BackBufferWidth;copy[1]=p.BackBufferHeight;copy[2]=p.BackBufferFormat;copy[3]=p.BackBufferCount;
  std::copy(copy.begin(),copy.end(),original);parameters[key]=copy;
}
uint32_t create_device(Context &c) {
  WorkerAffinityScope workers;
  auto *out=memory<uint32_t>(c.a[5],1,true);if(!out)return D3DERR_INVALIDCALL;*out=0;
  WindowLease next;if(!next.value)return D3DERR_DEVICELOST;
  D3DPRESENT_PARAMETERS pp=native_parameters(c.a[4],next.value);IDirect3DDevice9 *device=nullptr;
  HRESULT hr=static_cast<IDirect3D9*>(c.self)->CreateDevice(c.a[0],D3DDEVTYPE(c.a[1]),next.value,c.a[3],&pp,&device);
  if(SUCCEEDED(hr)){save_parameters(device,c.a[4],pp);device_windows[device]=next.take();*out=wrap(device);ut3_dxvk_host_log("D3D9 device created; Android fullscreen surface, native windowed WSI, original guest parameters retained");}
  return hr;
}
uint32_t reset(Context &c) {
  WorkerAffinityScope workers;
  WindowLease next;if(!next.value)return D3DERR_DEVICELOST;
  D3DPRESENT_PARAMETERS pp=native_parameters(c.a[0],next.value);HRESULT hr=static_cast<IDirect3DDevice9*>(c.self)->Reset(&pp);
  if(SUCCEEDED(hr)){
    save_parameters(c.self,c.a[0],pp);
    auto *old=device_windows[c.self];device_windows[c.self]=next.take();if(old)ANativeWindow_release(old);
  }return hr;
}
uint32_t additional_swapchain(Context &c) {
  WorkerAffinityScope workers;
  auto *out=memory<uint32_t>(c.a[1],1,true);if(!out)return D3DERR_INVALIDCALL;*out=0;
  auto *window=device_window(static_cast<IDirect3DDevice9*>(c.self));if(!window)return D3DERR_DEVICELOST;
  D3DPRESENT_PARAMETERS pp=native_parameters(c.a[0],window);IDirect3DSwapChain9 *chain=nullptr;
  HRESULT hr=static_cast<IDirect3DDevice9*>(c.self)->CreateAdditionalSwapChain(&pp,&chain);
  if(SUCCEEDED(hr)){save_parameters(chain,c.a[0],pp);*out=wrap(chain);}return hr;
}
uint64_t present_frames;
auto present_start=std::chrono::steady_clock::now(),present_sample=present_start;
uint64_t present_sample_frames;
struct PerfSample{uint64_t render_cpu,machine_wait,present_cpu,present_wall,process_cpu,calls,draws,polls;};
PerfSample perf_sample() {
  return {render_cpu_ns.load(std::memory_order_relaxed),machine_wait_ns.load(std::memory_order_relaxed),present_cpu_ns.load(std::memory_order_relaxed),present_wall_ns.load(std::memory_order_relaxed),clock_ns(CLOCK_PROCESS_CPUTIME_ID),native_calls.load(std::memory_order_relaxed),draw_calls.load(std::memory_order_relaxed),query_polls.load(std::memory_order_relaxed)};
}
PerfSample sample_start{};
std::mutex present_metrics_lock;
std::map<const void*,uint64_t> presented_ids;
void presented(const void *presenter,uint64_t id,int32_t result) {
  std::lock_guard<std::mutex> guard(present_metrics_lock);
  if(!id){presented_ids.erase(presenter);return;}
  if(result!=0&&result!=1000001003)return;
  if(id<=presented_ids[presenter])return;
  presented_ids[presenter]=id;
  auto now=std::chrono::steady_clock::now();if(!present_frames){present_sample=now;sample_start=perf_sample();}
  ++present_frames;void_android_frame_presented();
  double seconds=std::chrono::duration<double>(now-present_sample).count();
  if(seconds>=5){
    char text[512];double frames=double(present_frames-present_sample_frames);auto current=perf_sample();double scale=1e-6/frames;
    snprintf(text,sizeof text,"frame %llu: %.1f fps; per frame %.2f ms wall, %.2f ms render cpu, %.2f ms machine wait, %.2f ms process cpu, %.2f/%.2f ms native Present cpu/wall, %.0f calls, %.0f draws, %.1f query polls; accepted unique vkQueuePresentKHR",(unsigned long long)present_frames,frames/seconds,seconds*1000/frames,(current.render_cpu-sample_start.render_cpu)*scale,(current.machine_wait-sample_start.machine_wait)*scale,(current.process_cpu-sample_start.process_cpu)*scale,(current.present_cpu-sample_start.present_cpu)*scale,(current.present_wall-sample_start.present_wall)*scale,(current.calls-sample_start.calls)/frames,(current.draws-sample_start.draws)/frames,(current.polls-sample_start.polls)/frames);
    ut3_dxvk_host_log(text);present_sample=now;present_sample_frames=present_frames;sample_start=current;
  }
}
uint32_t present(Context &c) {
  sample_render_thread();
  if(c.a[2]||c.a[3])return D3DERR_INVALIDCALL; // No HWND override or nested Windows region bridge.
  uint32_t serial;void *current=void_android_window_current(&serial,0);
  if(!current||!void_android_render_ready()){void_android_window_ack(serial);return D3DERR_DEVICELOST;}
  auto *native_window=context_window(c);
  if(!native_window||current!=native_window){void_android_window_ack(serial);return D3DERR_DEVICELOST;}
  const RECT *src=memory<const RECT>(c.a[0]),*dst=memory<const RECT>(c.a[1]);
  RECT aspect{};
  if(!dst){aspect=aspect_destination(ANativeWindow_getWidth(native_window),ANativeWindow_getHeight(native_window),display_width.load(),display_height.load());dst=&aspect;}
  PresentTime time;
  HRESULT hr=c.kind==Kind::IDirect3DSwapChain9?static_cast<IDirect3DSwapChain9*>(c.self)->Present(src,dst,nullptr,nullptr,c.a[4]):static_cast<IDirect3DDevice9*>(c.self)->Present(src,dst,nullptr,nullptr);
  // A surface replaced during the native call is a recoverable display loss.
  if(FAILED(hr)&&surface_status(c)!=D3D_OK)return D3DERR_DEVICELOST;
  if(SUCCEEDED(hr)) {
    void_android_window_ack(serial);
  }
  return hr;
}
uint32_t shader_bytes(uint32_t va) {
  uint32_t version=ref<uint32_t>(va);if((version>>16)!=0xfffe&&(version>>16)!=0xffff)throw std::runtime_error("invalid SM shader header");
  if(((version>>8)&255)<2)throw std::runtime_error("SM1 shader boundary not implemented");
  uint32_t offset=4;
  while(offset<(16u<<20)){uint32_t token=ref<uint32_t>(va+offset);if((token&0xffff)==0xffff)return offset+4;uint32_t length=(token&0xffff)==0xfffe?((token>>16)&0x7fff):((token>>24)&15);offset=narrow_size(uint64_t(offset)+4u*(length+1));}
  throw std::runtime_error("unterminated shader");
}
uint32_t create_shader(Context &c,bool pixel) {
  auto *code=memory<const DWORD>(c.a[0],shader_bytes(c.a[0])/4);auto *out=memory<uint32_t>(c.a[1],1,true);if(!out)return D3DERR_INVALIDCALL;*out=0;
  auto *device=static_cast<IDirect3DDevice9*>(c.self);
  if(pixel){IDirect3DPixelShader9 *p=nullptr;HRESULT hr=device->CreatePixelShader(code,&p);if(SUCCEEDED(hr))*out=wrap(p);return hr;}
  IDirect3DVertexShader9 *p=nullptr;HRESULT hr=device->CreateVertexShader(code,&p);if(SUCCEEDED(hr))*out=wrap(p);return hr;
}
uint32_t create_declaration(Context &c) {
  uint32_t count=0;for(;count<65;++count)if(ref<D3DVERTEXELEMENT9>(c.a[0]+count*8).Stream==0xff)break;
  if(count==65)return D3DERR_INVALIDCALL;auto *input=memory<const D3DVERTEXELEMENT9>(c.a[0],count+1);auto *out=memory<uint32_t>(c.a[1],1,true);if(!out)return D3DERR_INVALIDCALL;*out=0;
  IDirect3DVertexDeclaration9 *p=nullptr;HRESULT hr=static_cast<IDirect3DDevice9*>(c.self)->CreateVertexDeclaration(input,&p);if(SUCCEEDED(hr))*out=wrap(p);return hr;
}
size_t vertex_count(uint32_t type,uint32_t count){switch(type){case 1:return count;case 2:return size_t(count)*2;case 3:return size_t(count)+1;case 4:return size_t(count)*3;case 5:case 6:return size_t(count)+2;default:throw std::runtime_error("invalid primitive topology");}}
uint32_t draw_up(Context &c,bool indexed) {
  auto *d=static_cast<IDirect3DDevice9*>(c.self);
  if(!indexed)return d->DrawPrimitiveUP(D3DPRIMITIVETYPE(c.a[0]),c.a[1],memory<const uint8_t>(c.a[2],vertex_count(c.a[0],c.a[1])*c.a[3]),c.a[3]);
  size_t isize=c.a[5]==D3DFMT_INDEX16?2:c.a[5]==D3DFMT_INDEX32?4:0;if(!isize)return D3DERR_INVALIDCALL;
  return d->DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE(c.a[0]),c.a[1],c.a[2],c.a[3],memory<const uint8_t>(c.a[4],vertex_count(c.a[0],c.a[3])*isize),D3DFORMAT(c.a[5]),memory<const uint8_t>(c.a[6],(size_t(c.a[1])+c.a[2])*c.a[7]),c.a[7]);
}
struct Mapping{uint32_t guest;uint8_t *native;uint32_t row_pitch,slice_pitch,row_bytes,rows,slices;bool readonly;};
std::map<std::pair<void*,uint32_t>,Mapping> mappings;
uint32_t rows_for(D3DFORMAT f,uint32_t h){return (f==D3DFMT_DXT1||f==D3DFMT_DXT2||f==D3DFMT_DXT3||f==D3DFMT_DXT4||f==D3DFMT_DXT5)?(h+3)/4:h;}
uint32_t row_bytes(D3DFORMAT f,uint32_t w){switch(f){
  case D3DFMT_DXT1:return ((w+3)/4)*8;
  case D3DFMT_DXT2:case D3DFMT_DXT3:case D3DFMT_DXT4:case D3DFMT_DXT5:return ((w+3)/4)*16;
  case D3DFMT_A8:case D3DFMT_L8:case D3DFMT_P8:case D3DFMT_R3G3B2:case D3DFMT_A4L4:return w;
  case D3DFMT_R5G6B5:case D3DFMT_X1R5G5B5:case D3DFMT_A1R5G5B5:case D3DFMT_A4R4G4B4:case D3DFMT_X4R4G4B4:case D3DFMT_A8R3G3B2:case D3DFMT_A8L8:case D3DFMT_A8P8:case D3DFMT_V8U8:case D3DFMT_L6V5U5:case D3DFMT_L16:case D3DFMT_R16F:case D3DFMT_CxV8U8:case D3DFMT_D16:case D3DFMT_D16_LOCKABLE:case D3DFMT_D15S1:return narrow_size(uint64_t(w)*2);
  case D3DFMT_R8G8B8:return narrow_size(uint64_t(w)*3);
  case D3DFMT_A8R8G8B8:case D3DFMT_X8R8G8B8:case D3DFMT_A8B8G8R8:case D3DFMT_X8B8G8R8:case D3DFMT_A2R10G10B10:case D3DFMT_A2B10G10R10:case D3DFMT_G16R16:case D3DFMT_V16U16:case D3DFMT_Q8W8V8U8:case D3DFMT_X8L8V8U8:case D3DFMT_A2W10V10U10:case D3DFMT_G16R16F:case D3DFMT_R32F:case D3DFMT_D32:case D3DFMT_D24S8:case D3DFMT_D24X8:case D3DFMT_D24X4S4:case D3DFMT_D32F_LOCKABLE:case D3DFMT_D24FS8:case D3DFMT_D32_LOCKABLE:return narrow_size(uint64_t(w)*4);
  case D3DFMT_A16B16G16R16:case D3DFMT_A16B16G16R16F:case D3DFMT_G32R32F:case D3DFMT_Q16W16V16U16:return narrow_size(uint64_t(w)*8);
  case D3DFMT_A32B32G32R32F:return narrow_size(uint64_t(w)*16);
  case D3DFMT_UYVY:case D3DFMT_YUY2:return narrow_size(((uint64_t(w)+1)/2)*4);
  default:throw std::runtime_error("native locked format has no checked guest row layout");
}}
Mapping map_memory(void *native,uint32_t pitch,uint32_t slice,uint32_t bytes,uint32_t rows,uint32_t slices,uint32_t flags) {
  if(!native||!bytes||!rows||!slices||pitch<bytes||slice<uint64_t(pitch)*(rows-1)+bytes)throw std::runtime_error("invalid native lock geometry");
  uint32_t length=narrow_size(uint64_t(slice)*(slices-1)+uint64_t(pitch)*(rows-1)+bytes),guest=ut3_dxvk_guest_alloc(length);if(!guest)throw std::bad_alloc();
  Mapping m{guest,static_cast<uint8_t*>(native),pitch,slice,bytes,rows,slices,(flags&D3DLOCK_READONLY)!=0};
  try {
    auto *destination=memory<uint8_t>(guest,length,true);
    for(uint32_t z=0;z<slices;++z)for(uint32_t y=0;y<rows;++y)memcpy(destination+size_t(z)*slice+size_t(y)*pitch,m.native+size_t(z)*slice+size_t(y)*pitch,bytes);
  }catch(...){ut3_dxvk_guest_free(guest);throw;}
  return m;
}
void retain_mapping(const std::pair<void*,uint32_t> &key,const Mapping &m) {
  try {if(!mappings.emplace(key,m).second)throw std::runtime_error("duplicate native lock");}
  catch(...){ut3_dxvk_guest_free(m.guest);throw;}
}
void flush_mapping(const Mapping &m) {
  if(m.readonly)return;auto *source=memory<const uint8_t>(m.guest,size_t(m.slice_pitch)*(m.slices-1)+size_t(m.row_pitch)*(m.rows-1)+m.row_bytes);
  for(uint32_t z=0;z<m.slices;++z)for(uint32_t y=0;y<m.rows;++y)memcpy(m.native+size_t(z)*m.slice_pitch+size_t(y)*m.row_pitch,source+size_t(z)*m.slice_pitch+size_t(y)*m.row_pitch,m.row_bytes);
}
uint32_t buffer_lock(Context &c,bool unlock) {
  auto key=std::make_pair(c.self,0u);bool vertex=c.kind==Kind::IDirect3DVertexBuffer9;
  if(unlock){auto it=mappings.find(key);if(it==mappings.end())return D3DERR_INVALIDCALL;flush_mapping(it->second);HRESULT hr=vertex?static_cast<IDirect3DVertexBuffer9*>(c.self)->Unlock():static_cast<IDirect3DIndexBuffer9*>(c.self)->Unlock();if(SUCCEEDED(hr)){ut3_dxvk_guest_free(it->second.guest);mappings.erase(it);}return hr;}
  if(mappings.count(key))return D3DERR_INVALIDCALL;auto *out=memory<uint32_t>(c.a[2],1,true);if(!out)return D3DERR_INVALIDCALL;*out=0;
  uint32_t total=0;if(vertex){D3DVERTEXBUFFER_DESC d;HRESULT hr=static_cast<IDirect3DVertexBuffer9*>(c.self)->GetDesc(&d);if(FAILED(hr))return hr;total=d.Size;}else{D3DINDEXBUFFER_DESC d;HRESULT hr=static_cast<IDirect3DIndexBuffer9*>(c.self)->GetDesc(&d);if(FAILED(hr))return hr;total=d.Size;}
  uint32_t offset=c.a[0],length=c.a[1]?c.a[1]:total-offset;if(offset>total||length>total-offset||!length)return D3DERR_INVALIDCALL;
  void *p=nullptr;HRESULT hr=vertex?static_cast<IDirect3DVertexBuffer9*>(c.self)->Lock(offset,c.a[1],&p,c.a[3]):static_cast<IDirect3DIndexBuffer9*>(c.self)->Lock(offset,c.a[1],&p,c.a[3]);
  if(SUCCEEDED(hr)){
    try{auto m=map_memory(p,length,length,length,1,1,c.a[3]);retain_mapping(key,m);*out=m.guest;}
    catch(...){HRESULT rollback=vertex?static_cast<IDirect3DVertexBuffer9*>(c.self)->Unlock():static_cast<IDirect3DIndexBuffer9*>(c.self)->Unlock();if(FAILED(rollback))ut3_dxvk_host_log("native buffer Unlock rollback failed");throw;}
  }return hr;
}
uint32_t texture_lock(Context &c,bool unlock) {
  bool texture=c.kind==Kind::IDirect3DTexture9,cube=c.kind==Kind::IDirect3DCubeTexture9;
  uint32_t face=cube?c.a[0]:0,level=(texture?c.a[0]:cube?c.a[1]:0),sub=(face<<16)|level;auto key=std::make_pair(c.self,sub);
  if(unlock){auto it=mappings.find(key);if(it==mappings.end())return D3DERR_INVALIDCALL;flush_mapping(it->second);HRESULT hr=texture?static_cast<IDirect3DTexture9*>(c.self)->UnlockRect(level):cube?static_cast<IDirect3DCubeTexture9*>(c.self)->UnlockRect(D3DCUBEMAP_FACES(face),level):static_cast<IDirect3DSurface9*>(c.self)->UnlockRect();if(SUCCEEDED(hr)){ut3_dxvk_guest_free(it->second.guest);mappings.erase(it);}return hr;}
  if(mappings.count(key))return D3DERR_INVALIDCALL;uint32_t shift=texture?1:cube?2:0;auto *out=memory<uint32_t>(c.a[shift],2,true);if(!out)return D3DERR_INVALIDCALL;
  auto *rect=memory<const RECT>(c.a[shift+1]);uint32_t flags=c.a[shift+2];D3DSURFACE_DESC desc{};
  HRESULT hr=texture?static_cast<IDirect3DTexture9*>(c.self)->GetLevelDesc(level,&desc):cube?static_cast<IDirect3DCubeTexture9*>(c.self)->GetLevelDesc(level,&desc):static_cast<IDirect3DSurface9*>(c.self)->GetDesc(&desc);if(FAILED(hr))return hr;
  uint32_t w=desc.Width,h=desc.Height;if(rect){if(rect->left<0||rect->top<0||rect->right<=rect->left||rect->bottom<=rect->top||uint32_t(rect->right)>w||uint32_t(rect->bottom)>h)return D3DERR_INVALIDCALL;w=rect->right-rect->left;h=rect->bottom-rect->top;}
  D3DLOCKED_RECT lock{};hr=texture?static_cast<IDirect3DTexture9*>(c.self)->LockRect(level,&lock,rect,flags):cube?static_cast<IDirect3DCubeTexture9*>(c.self)->LockRect(D3DCUBEMAP_FACES(face),level,&lock,rect,flags):static_cast<IDirect3DSurface9*>(c.self)->LockRect(&lock,rect,flags);
  if(SUCCEEDED(hr)){
    try{uint32_t rows=rows_for(desc.Format,h),bytes=row_bytes(desc.Format,w);auto m=map_memory(lock.pBits,uint32_t(lock.Pitch),narrow_size(uint64_t(lock.Pitch)*rows),bytes,rows,1,flags);retain_mapping(key,m);out[0]=lock.Pitch;out[1]=m.guest;}
    catch(...){HRESULT rollback=texture?static_cast<IDirect3DTexture9*>(c.self)->UnlockRect(level):cube?static_cast<IDirect3DCubeTexture9*>(c.self)->UnlockRect(D3DCUBEMAP_FACES(face),level):static_cast<IDirect3DSurface9*>(c.self)->UnlockRect();if(FAILED(rollback))ut3_dxvk_host_log("native texture Unlock rollback failed");throw;}
  }return hr;
}
uint32_t volume_lock(Context &c,bool unlock) {
  bool texture=c.kind==Kind::IDirect3DVolumeTexture9;uint32_t level=texture?c.a[0]:0;auto key=std::make_pair(c.self,level);
  if(unlock){auto it=mappings.find(key);if(it==mappings.end())return D3DERR_INVALIDCALL;flush_mapping(it->second);HRESULT hr=texture?static_cast<IDirect3DVolumeTexture9*>(c.self)->UnlockBox(level):static_cast<IDirect3DVolume9*>(c.self)->UnlockBox();if(SUCCEEDED(hr)){ut3_dxvk_guest_free(it->second.guest);mappings.erase(it);}return hr;}
  if(mappings.count(key))return D3DERR_INVALIDCALL;unsigned shift=texture?1:0;auto *out=memory<uint32_t>(c.a[shift],3,true);if(!out)return D3DERR_INVALIDCALL;auto *box=memory<const D3DBOX>(c.a[shift+1]);uint32_t flags=c.a[shift+2];D3DVOLUME_DESC desc{};
  HRESULT hr=texture?static_cast<IDirect3DVolumeTexture9*>(c.self)->GetLevelDesc(level,&desc):static_cast<IDirect3DVolume9*>(c.self)->GetDesc(&desc);if(FAILED(hr))return hr;
  uint32_t w=desc.Width,h=desc.Height,z=desc.Depth;if(box){if(box->Right<=box->Left||box->Bottom<=box->Top||box->Back<=box->Front||box->Right>w||box->Bottom>h||box->Back>z)return D3DERR_INVALIDCALL;w=box->Right-box->Left;h=box->Bottom-box->Top;z=box->Back-box->Front;}
  D3DLOCKED_BOX lock{};hr=texture?static_cast<IDirect3DVolumeTexture9*>(c.self)->LockBox(level,&lock,box,flags):static_cast<IDirect3DVolume9*>(c.self)->LockBox(&lock,box,flags);
  if(SUCCEEDED(hr)){
    try{auto m=map_memory(lock.pBits,lock.RowPitch,lock.SlicePitch,row_bytes(desc.Format,w),rows_for(desc.Format,h),z,flags);retain_mapping(key,m);out[0]=lock.RowPitch;out[1]=lock.SlicePitch;out[2]=m.guest;}
    catch(...){HRESULT rollback=texture?static_cast<IDirect3DVolumeTexture9*>(c.self)->UnlockBox(level):static_cast<IDirect3DVolume9*>(c.self)->UnlockBox();if(FAILED(rollback))ut3_dxvk_host_log("native volume Unlock rollback failed");throw;}
  }return hr;
}
uint32_t query_interface(Context &c,bool container) {
  auto &iid=ref<GUID>(c.a[0]);auto *out=memory<uint32_t>(c.a[1],1,true);if(!out)return E_POINTER;*out=0;
  Kind kind=kind_for_iid(iid);if(kind==Kind::Count)return E_NOINTERFACE;void *native=nullptr;HRESULT hr;
  if(container){if(c.kind==Kind::IDirect3DSurface9)hr=static_cast<IDirect3DSurface9*>(c.self)->GetContainer(iid,&native);else hr=static_cast<IDirect3DVolume9*>(c.self)->GetContainer(iid,&native);}
  else hr=static_cast<IUnknown*>(c.self)->QueryInterface(iid,&native);
  if(SUCCEEDED(hr)){
    if(kind==Kind::IDirect3DBaseTexture9)*out=wrap(static_cast<IDirect3DBaseTexture9*>(native));
    else if(kind==Kind::IDirect3DResource9)*out=wrap(static_cast<IDirect3DResource9*>(native));
    else *out=ut3_dxvk_wrap_native(unsigned(kind),native);
  }return hr;
}
uint32_t special(Context &c,const char *name) {
  if(!strcmp(name,"QueryInterface"))return query_interface(c,false);
  if(!strcmp(name,"GetContainer"))return query_interface(c,true);
  if(!strcmp(name,"CreateDevice"))return create_device(c);
  if(!strcmp(name,"Reset"))return reset(c);
  if(!strcmp(name,"TestCooperativeLevel")){uint32_t status=surface_status(c);return status?status:static_cast<IDirect3DDevice9*>(c.self)->TestCooperativeLevel();}
  if(!strcmp(name,"CreateAdditionalSwapChain"))return additional_swapchain(c);
  if(!strcmp(name,"Present"))return present(c);
  if(!strcmp(name,"GetAdapterIdentifier")){
    D3DADAPTER_IDENTIFIER9 id{};auto *out=memory<uint8_t>(c.a[2],1100,true);
    HRESULT hr=static_cast<IDirect3D9*>(c.self)->GetAdapterIdentifier(c.a[0],c.a[1],&id);
    if(SUCCEEDED(hr)&&out)memcpy(out,&id,1100);return out?hr:D3DERR_INVALIDCALL;
  }
  if(!strcmp(name,"GetAdapterMonitor"))return static_cast<IDirect3D9*>(c.self)->GetAdapterMonitor(c.a[0])?0x70001:0;
  if(!strcmp(name,"GetPresentParameters")){auto it=parameters.find(c.self);if(it==parameters.end()){D3DPRESENT_PARAMETERS p{};HRESULT hr=static_cast<IDirect3DSwapChain9*>(c.self)->GetPresentParameters(&p);if(FAILED(hr))return hr;std::array<uint32_t,14> v={p.BackBufferWidth,p.BackBufferHeight,uint32_t(p.BackBufferFormat),p.BackBufferCount,uint32_t(p.MultiSampleType),p.MultiSampleQuality,uint32_t(p.SwapEffect),0,1,uint32_t(p.EnableAutoDepthStencil),uint32_t(p.AutoDepthStencilFormat),p.Flags,0,p.PresentationInterval};memcpy(memory<uint32_t>(c.a[0],14,true),v.data(),56);}else memcpy(memory<uint32_t>(c.a[0],14,true),it->second.data(),56);return 0;}
  if(!strcmp(name,"GetCreationParameters")){D3DDEVICE_CREATION_PARAMETERS p{};HRESULT hr=static_cast<IDirect3DDevice9*>(c.self)->GetCreationParameters(&p);if(SUCCEEDED(hr)){auto *out=memory<uint32_t>(c.a[0],4,true);out[0]=p.AdapterOrdinal;out[1]=p.DeviceType;out[2]=parameters.count(c.self)?parameters[c.self][7]:0;out[3]=p.BehaviorFlags;}return hr;}
  if(!strcmp(name,"CreateVertexShader"))return create_shader(c,false);
  if(!strcmp(name,"CreatePixelShader"))return create_shader(c,true);
  if(!strcmp(name,"CreateVertexDeclaration"))return create_declaration(c);
  if(!strcmp(name,"DrawPrimitiveUP"))return draw_up(c,false);
  if(!strcmp(name,"DrawIndexedPrimitiveUP"))return draw_up(c,true);
  if(!strcmp(name,"Lock")||!strcmp(name,"Unlock"))return buffer_lock(c,name[0]=='U');
  if(!strcmp(name,"LockRect")||!strcmp(name,"UnlockRect"))return texture_lock(c,name[0]=='U');
  if(!strcmp(name,"LockBox")||!strcmp(name,"UnlockBox"))return volume_lock(c,name[0]=='U');
  if(!strcmp(name,"GetFunction")){auto *size=memory<UINT>(c.a[1],1,true);if(!size)return D3DERR_INVALIDCALL;void *out=memory<uint8_t>(c.a[0],*size,true);return c.kind==Kind::IDirect3DVertexShader9?static_cast<IDirect3DVertexShader9*>(c.self)->GetFunction(out,size):static_cast<IDirect3DPixelShader9*>(c.self)->GetFunction(out,size);}
  if(!strcmp(name,"GetDC")||!strcmp(name,"ReleaseDC")||!strcmp(name,"RegisterSoftwareDevice"))return D3DERR_INVALIDCALL;
  // Private-data methods are dispatched below with the concrete interface type.
  return 0xffffffff;
}
template<class T>uint32_t private_data(Context &c,T *p,const char *name) {
  auto &guid=ref<GUID>(c.a[0]);
  if(!strcmp(name,"FreePrivateData"))return p->FreePrivateData(guid);
  if(!strcmp(name,"SetPrivateData")){if(c.a[3]&D3DSPD_IUNKNOWN)return D3DERR_INVALIDCALL;return p->SetPrivateData(guid,memory<const uint8_t>(c.a[1],c.a[2]),c.a[2],c.a[3]);}
  auto *length=memory<DWORD>(c.a[2],1,true);if(!length)return D3DERR_INVALIDCALL;return p->GetPrivateData(guid,memory<uint8_t>(c.a[1],*length,true),length);
}
}

extern "C" int ut3_dxvk_get_display_mode(uint32_t *w,uint32_t *h,uint32_t *rate){*w=display_width;*h=display_height;*rate=60;return 1;}
extern "C" uint32_t ut3_dxvk_native_addref(void *p){native_calls.fetch_add(1,std::memory_order_relaxed);return static_cast<IUnknown*>(p)->AddRef();}
extern "C" uint32_t ut3_dxvk_native_release(void *p){
  native_calls.fetch_add(1,std::memory_order_relaxed);
  uint32_t refs=static_cast<IUnknown*>(p)->Release();
  if(!refs){
    auto it=device_windows.find(p);if(it!=device_windows.end()){ANativeWindow_release(it->second);device_windows.erase(it);}
    parameters.erase(p);
  }return refs;
}
extern "C" uint32_t ut3_dxvk_native_create9(uint32_t sdk) {
  try {
    WorkerAffinityScope workers;
    setenv("DXVK_WSI_DRIVER","Android",1);setenv("DXVK_FORCE_WINDOWED","1",1);
    display_width=render_dimension("UT3_RENDER_WIDTH",640);display_height=render_dimension("UT3_RENDER_HEIGHT",480);
    if(const char *limit=getenv("UT3_FPS_LIMIT"))setenv("DXVK_FRAME_RATE",limit,1);
    void *library=dlopen("libdxvk_d3d9.so",RTLD_NOW|RTLD_LOCAL);if(!library){ut3_dxvk_host_log(dlerror());return 0;}
    if(const char *turnip=getenv("UT3_DXVK_TURNIP");turnip&&!strcmp(turnip,"1")) {
      Dl_info info{};char error[512]{};
      if(!dladdr(reinterpret_cast<void*>(&ut3_dxvk_native_create9),&info)||!info.dli_fname){ut3_dxvk_host_log("Turnip cannot locate native library directory");return 0;}
      std::string directory=info.dli_fname;auto slash=directory.find_last_of('/');
      if(slash==std::string::npos){ut3_dxvk_host_log("Turnip native library directory is invalid");return 0;}
      directory.resize(slash);
      const char *custom=getenv("PORT_TURNIP_DRIVER");
      if(custom&&*custom)ut3_dxvk_host_log((std::string("Turnip: the launcher's own driver ")+custom).c_str());
      auto loader=ut3_open_turnip(directory.c_str(),custom,error,sizeof error);
      using SetLoader=void(*)(PFN_vkGetInstanceProcAddr);
      auto set_loader=reinterpret_cast<SetLoader>(dlsym(library,"ut3_dxvk_set_vulkan_loader"));
      if(!loader||!set_loader){ut3_dxvk_host_log(loader?"DXVK custom-loader callback unavailable":error);return 0;}
      set_loader(loader);ut3_dxvk_host_log("Using verified Turnip Vulkan1.3 custom driver; system driver retained");
    }
    using Callbacks=void(*)(int(*)(uint32_t*,uint32_t*,uint32_t*),void(*)(const void*,uint64_t,int32_t),void(*)());
    auto callbacks=reinterpret_cast<Callbacks>(dlsym(library,"ut3_dxvk_set_callbacks_v2"));
    if(!callbacks){ut3_dxvk_host_log(dlerror());return 0;}
    callbacks([](uint32_t*w,uint32_t*h,uint32_t*r){*w=display_width.load();*h=display_height.load();*r=60u;return 1;},presented,[]{(void)void_native_worker_affinity_apply();});
    using Create=IDirect3D9*(*)(UINT);auto create=reinterpret_cast<Create>(dlsym(library,"Direct3DCreate9"));if(!create){ut3_dxvk_host_log(dlerror());return 0;}
    return wrap(create(sdk));
  }catch(const std::exception &e){ut3_dxvk_host_log(e.what());return 0;}
}
extern "C" uint32_t ut3_dxvk_native_call(unsigned kind,void *native,unsigned slot,const uint32_t *args) {
  native_calls.fetch_add(1,std::memory_order_relaxed);
  if(Kind(kind)==Kind::IDirect3DDevice9&&((slot>=81&&slot<=84)||slot==115||slot==116))draw_calls.fetch_add(1,std::memory_order_relaxed);
  try {
    // A pending occlusion query can be polled thousands of times. Its known
    // interface and slot need no method-name search or general marshaling.
    if(Kind(kind)==Kind::IDirect3DQuery9&&slot==7) {
      query_polls.fetch_add(1,std::memory_order_relaxed);
      return static_cast<IDirect3DQuery9*>(native)->GetData(memory<uint8_t>(args[0],args[1],true),args[1],args[2]);
    }
    Context c{Kind(kind),native,slot,args};const char *name=nullptr;
    switch(c.kind) {
#include "bridge_special_names.inc"
      default:break;
    }
    if(name){uint32_t result=special(c,name);if(result!=0xffffffff)return result;}
    switch(c.kind) {
#include "bridge_dispatch.inc"
      default:break;
    }
    ut3_dxvk_host_log("Unsupported DXVK COM slot");return E_NOTIMPL;
  }catch(const std::bad_alloc &e){ut3_dxvk_host_log(e.what());return E_OUTOFMEMORY;}
  catch(const std::exception &e){ut3_dxvk_host_log(e.what());return D3DERR_INVALIDCALL;}
}
