/* Exercise production marshaling with guards, then query the actual native DXVK Vulkan adapter. */
#include "../android/dxvk/bridge.cpp"
#include <cassert>
#include <string>

static std::vector<uint8_t> fixture_memory(32u<<20,0xa5);
static uint32_t fixture_next=4096;
static bool fixture_fail_alloc;
static unsigned fixture_frees;
static std::map<uint32_t,std::pair<unsigned,void*>> fixture_objects;
static uint32_t fixture_object_id=0x100000;
extern "C" void *ut3_dxvk_guest_memory(uint32_t va,uint32_t n,int) {
  if(!va||uint64_t(va)+n>fixture_memory.size())throw std::runtime_error("fixture guest bounds");
  return fixture_memory.data()+va;
}
extern "C" uint32_t ut3_dxvk_guest_alloc(uint32_t n){if(fixture_fail_alloc)return 0;uint32_t v=fixture_next;fixture_next=(fixture_next+n+15)&~15u;assert(fixture_next<0x100000);return v;}
extern "C" void ut3_dxvk_guest_free(uint32_t){++fixture_frees;}
extern "C" void ut3_dxvk_host_log(const char *s){puts(s);}
extern "C" void *ut3_dxvk_unwrap_guest(uint32_t v){return v?fixture_objects.at(v).second:nullptr;}
extern "C" uint32_t ut3_dxvk_wrap_native(unsigned kind,void *p){if(!p)return 0;uint32_t v=fixture_object_id++;fixture_objects[v]={kind,p};return v;}
struct FixtureWindow {int refs=1;};
static FixtureWindow *fixture_window;
static FixtureWindow *fixture_current_window;
static bool fixture_render_ready=true;
extern "C" void ANativeWindow_acquire(ANativeWindow *window){++reinterpret_cast<FixtureWindow*>(window)->refs;}
extern "C" void ANativeWindow_release(ANativeWindow *window){assert(reinterpret_cast<FixtureWindow*>(window)->refs>0);--reinterpret_cast<FixtureWindow*>(window)->refs;}
extern "C" void *ut3_dxvk_native_window(){if(fixture_window)++fixture_window->refs;return fixture_window;}
extern "C" void void_android_frame_presented(){}
extern "C" void void_input_set_screen_size(int,int){}
extern "C" uint64_t void_lock_wait_ns(){return 0;}
extern "C" void void_native_worker_affinity_begin(){}
extern "C" void void_native_worker_affinity_end(){}
extern "C" int void_native_worker_affinity_apply(){return 1;}
extern "C" int void_android_render_ready(){return fixture_render_ready;}
extern "C" void void_android_window_ack(uint32_t){}
extern "C" void *void_android_window_current(uint32_t *serial,int){*serial=1;return fixture_current_window;}
class FixtureBuffer final:public IDirect3DVertexBuffer9 {
public:
  bool locked=false;unsigned unlocks=0;std::array<uint8_t,64> data{};
  HRESULT QueryInterface(REFIID,void **out) override{if(out)*out=nullptr;return E_NOINTERFACE;}
  ULONG AddRef() override{return 2;}
  ULONG Release() override{return 1;}
  HRESULT GetDevice(IDirect3DDevice9**) override{return E_FAIL;}
  HRESULT SetPrivateData(REFGUID,const void*,DWORD,DWORD) override{return E_NOTIMPL;}
  HRESULT GetPrivateData(REFGUID,void*,DWORD*) override{return E_NOTIMPL;}
  HRESULT FreePrivateData(REFGUID) override{return E_NOTIMPL;}
  DWORD SetPriority(DWORD) override{return 0;}
  DWORD GetPriority() override{return 0;}
  void PreLoad() override{}
  D3DRESOURCETYPE GetType() override{return D3DRTYPE_VERTEXBUFFER;}
  HRESULT Lock(UINT offset,UINT,void **out,DWORD) override{if(locked)return D3DERR_INVALIDCALL;locked=true;*out=data.data()+offset;return 0;}
  HRESULT Unlock() override{if(!locked)return D3DERR_INVALIDCALL;locked=false;++unlocks;return 0;}
  HRESULT GetDesc(D3DVERTEXBUFFER_DESC *d) override{memset(d,0,sizeof(*d));d->Size=data.size();return 0;}
};
int main(){
  static_assert(sizeof(D3DCAPS9)==304);static_assert(sizeof(D3DADAPTER_IDENTIFIER9)==1104);
  static_assert(sizeof(D3DDISPLAYMODE)==16);static_assert(sizeof(D3DVERTEXELEMENT9)==8);
  static_assert(sizeof(D3DSURFACE_DESC)==32);static_assert(sizeof(D3DVOLUME_DESC)==28);
  RECT wide=aspect_destination(2780,1264,1056,480);assert(wide.left==0&&wide.top==0&&wide.right==2780&&wide.bottom==1264);
  RECT legacy=aspect_destination(2780,1264,640,480);assert(legacy.left==547&&legacy.top==0&&legacy.right==2232&&legacy.bottom==1264);
  setenv("UT3_RENDER_WIDTH","1056",1);setenv("UT3_RENDER_HEIGHT","480",1);setenv("UT3_FPS_LIMIT","120",1);
  assert(render_dimension("UT3_RENDER_WIDTH",640)==1056);setenv("UT3_RENDER_WIDTH","-1",1);assert(render_dimension("UT3_RENDER_WIDTH",640)==640);setenv("UT3_RENDER_WIDTH","1056junk",1);assert(render_dimension("UT3_RENDER_WIDTH",640)==640);setenv("UT3_RENDER_WIDTH","1056",1);
  sample_render_thread();uint64_t cpu_before=render_cpu_ns.load(),present_before=present_wall_ns.load();
  {PresentTime measured;volatile uint64_t work=0;for(unsigned i=0;i<10000;++i)work=work+i;assert(work);}
  assert(render_cpu_ns.load()>=cpu_before&&present_wall_ns.load()>present_before&&perf_sample().process_cpu>0);
  const void *fixture_presenter=reinterpret_cast<const void*>(uintptr_t(0x1234));uint64_t frames_before=present_frames;
  presented(fixture_presenter,1,0);assert(present_frames==frames_before+1);
  presented(fixture_presenter,1,0);presented(fixture_presenter,2,-1000000000);assert(present_frames==frames_before+1);
  presented(fixture_presenter,2,1000001003);assert(present_frames==frames_before+2);
  presented(fixture_presenter,0,1);assert(!presented_ids.count(fixture_presenter));
  const uint32_t a[]={0x80000000,0x3f800000,12,0};Context c{Kind::IDirect3DDevice9,nullptr,0,a};
  assert(float(Argument<float>(c,1,"test"))==1.f);assert(int32_t(Argument<INT>(c,0,"test"))==INT32_MIN);
  assert(elements(c,"SetVertexShaderConstantF",1)==48);assert(elements(c,"GetPixelShaderConstantI",1)==48);assert(elements(c,"SetVertexShaderConstantB",1)==12);
  auto shader=ut3_dxvk_guest_alloc(24);const uint32_t valid[]={0xffff0300,0x02000001,0x80000000,0x0000ffff,0x0000ffff};memcpy(memory<uint32_t>(shader,5,true),valid,20);
  assert(shader_bytes(shader)==20); // END-shaped immediate is skipped with its instruction's operands.
  const uint32_t comment[]={0xfffe0300,0x0002fffe,0x0000ffff,0x12345678,0xffff};memcpy(memory<uint32_t>(shader,5,true),comment,20);assert(shader_bytes(shader)==20);
  assert(row_bytes(D3DFMT_DXT1,9)==24&&rows_for(D3DFMT_DXT1,9)==3);assert(row_bytes(D3DFMT_A16B16G16R16F,9)==72);
  std::array<uint8_t,160> source;for(unsigned i=0;i<source.size();++i)source[i]=uint8_t(i);
  auto original=source;auto m=map_memory(source.data()+3,16,64,7,3,2,0);auto *p=memory<uint8_t>(m.guest,103,true);
  for(unsigned z=0;z<2;++z)for(unsigned y=0;y<3;++y)for(unsigned x=0;x<7;++x){assert(p[z*64+y*16+x]==source[3+z*64+y*16+x]);p[z*64+y*16+x]^=0xff;}
  flush_mapping(m);
  for(unsigned i=0;i<source.size();++i){bool changed=false;for(unsigned z=0;z<2;++z)for(unsigned y=0;y<3;++y)changed|=i>=3+z*64+y*16&&i<10+z*64+y*16;assert(source[i]==(changed?uint8_t(original[i]^0xff):original[i]));}
  auto ro=map_memory(source.data(),16,64,7,3,1,D3DLOCK_READONLY);memset(memory<uint8_t>(ro.guest,39,true),0,39);auto before=source;flush_mapping(ro);assert(source==before);
  bool rejected=false;try{map_memory(source.data(),4,4,7,1,1,0);}catch(const std::exception&){rejected=true;}assert(rejected);
  assert(vertex_count(4,12)==36&&vertex_count(5,12)==14&&vertex_count(2,12)==24);
  FixtureWindow first,second;fixture_window=&first;{WindowLease next;assert(first.refs==2);}assert(first.refs==1);
  fixture_window=&second;try{WindowLease next;assert(second.refs==2);throw std::runtime_error("failed native Reset");}catch(...){}assert(second.refs==1);fixture_window=nullptr;
  int device_token;Context display_context{Kind::IDirect3DDevice9,&device_token,3,nullptr};device_windows[&device_token]=reinterpret_cast<ANativeWindow*>(&first);
  assert(surface_status(display_context)==uint32_t(D3DERR_DEVICELOST));
  fixture_current_window=&second;assert(surface_status(display_context)==uint32_t(D3DERR_DEVICENOTRESET));
  fixture_current_window=&first;assert(!surface_status(display_context));
  fixture_render_ready=false;assert(surface_status(display_context)==uint32_t(D3DERR_DEVICELOST));
  fixture_render_ready=true;assert(!surface_status(display_context));fixture_current_window=nullptr;device_windows.clear();
  FixtureBuffer buffer;uint32_t lockout=ut3_dxvk_guest_alloc(4),lockargs[]={0,0,lockout,0};
  fixture_fail_alloc=true;auto failure=ut3_dxvk_native_call(unsigned(Kind::IDirect3DVertexBuffer9),&buffer,11,lockargs);fixture_fail_alloc=false;
  assert(failure==uint32_t(E_OUTOFMEMORY)&&!buffer.locked&&buffer.unlocks==1&&!ref<uint32_t>(lockout));
  assert(mappings.empty());
  assert(!ut3_dxvk_native_call(unsigned(Kind::IDirect3DVertexBuffer9),&buffer,11,lockargs));assert(buffer.locked);
  memory<uint8_t>(ref<uint32_t>(lockout),64,true)[9]=123;
  assert(!ut3_dxvk_native_call(unsigned(Kind::IDirect3DVertexBuffer9),&buffer,12,lockargs));assert(!buffer.locked&&buffer.unlocks==2&&buffer.data[9]==123&&mappings.empty());
  // Actual native implementation: no Android surface or fake device is supplied.
  uint32_t guest=ut3_dxvk_native_create9(32);assert(guest);void *d3d=fixture_objects.at(guest).second;
  assert(display_width.load()==1056&&display_height.load()==480);assert(!strcmp(getenv("DXVK_FRAME_RATE"),"120"));
  uint32_t out=ut3_dxvk_guest_alloc(1200);uint32_t cap_args[]={0,D3DDEVTYPE_HAL,out};
  uint32_t hr=ut3_dxvk_native_call(unsigned(Kind::IDirect3D9),d3d,14,cap_args);assert(SUCCEEDED(HRESULT(hr)));
  auto &caps=ref<D3DCAPS9>(out);assert(caps.VertexShaderVersion>=D3DVS_VERSION(3,0)&&caps.PixelShaderVersion>=D3DPS_VERSION(3,0));
  memset(memory<uint8_t>(out,1200,true),0xa5,1200);
  uint32_t id_args[]={0,0,out};hr=ut3_dxvk_native_call(unsigned(Kind::IDirect3D9),d3d,5,id_args);assert(SUCCEEDED(HRESULT(hr)));
  for(unsigned i=1100;i<1200;++i)assert(*memory<uint8_t>(out+i)==0xa5);
  auto &id=ref<D3DADAPTER_IDENTIFIER9>(out);printf("Native DXVK adapter: %s vendor=%04x device=%04x; x86 1100-byte identifier guards intact\n",id.Description,id.VendorId,id.DeviceId);
  ut3_dxvk_native_release(d3d);
  puts("PASS production x86 scalar/span/SM3/count/strided-lock guards, native-lock allocation-failure rollback/retry, acquired-window release, lost/new-surface/not-reset/paused state, 1100-byte identifier, 1056x480 full-display aspect and native120fps option + real DXVK D3D9 Vulkan adapter/caps");
  return 0;
}
