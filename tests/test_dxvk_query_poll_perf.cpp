/* Real native GPU latency/polling comparison. No Android surface or app input. */
#include "../reference/dxvk/src/d3d9/d3d9_interfaces.h"
#define main correctness_oracle_main
#include "test_dxvk_occlusion_gpu.cpp"
#undef main
#include <sched.h>
#include <time.h>
#include <cstring>
#include "../android/dxvk/turnip.inc"

static uint64_t clockns(clockid_t clock){timespec t{};clock_gettime(clock,&t);return uint64_t(t.tv_sec)*1000000000+t.tv_nsec;}
int main(int argc,char **argv) {
  const char *mode=argc>1?argv[1]:"spin";
  unsigned count=argc>2?unsigned(strtoul(argv[2],nullptr,10)):64;
  unsigned frames=argc>3?unsigned(strtoul(argv[3],nullptr,10)):120;
  const char *submit=argc>4?argv[4]:"explicit";
  if(count<1||count>512||frames<10||frames>1000||
     (strcmp(mode,"spin")&&strcmp(mode,"yield")&&strcmp(mode,"sleep50"))||
     (strcmp(submit,"explicit")&&strcmp(submit,"implicit")))return 2;
  setenv("DXVK_WSI_DRIVER","Android",1);setenv("DXVK_FORCE_WINDOWED","1",1);
  void *library=dlopen("libdxvk_d3d9.so",RTLD_NOW|RTLD_LOCAL);if(!library){puts(dlerror());return 2;}
  if(const char *directory=getenv("UT3_TURNIP_DIR")) {
    char error[512]{};auto loader=ut3_open_turnip(directory,nullptr,error,sizeof error);
    auto set_loader=reinterpret_cast<void(*)(PFN_vkGetInstanceProcAddr)>(dlsym(library,"ut3_dxvk_set_vulkan_loader"));
    if(!loader||!set_loader){puts(loader?"FAIL custom loader callback missing":error);return 2;}
    set_loader(loader);
  }
  auto create=reinterpret_cast<IDirect3D9*(*)(UINT)>(dlsym(library,"Direct3DCreate9"));if(!create)return 2;
  IDirect3D9 *d3d=create(D3D_SDK_VERSION);if(!d3d)return 2;
  D3DPRESENT_PARAMETERS pp{};pp.BackBufferWidth=128;pp.BackBufferHeight=128;pp.BackBufferFormat=D3DFMT_A8R8G8B8;
  pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.Windowed=TRUE;
  pp.EnableAutoDepthStencil=TRUE;pp.AutoDepthStencilFormat=D3DFMT_D24S8;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
  IDirect3DDevice9 *device=nullptr;
  if(!checked(d3d->CreateDevice(0,D3DDEVTYPE_HAL,nullptr,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,&device),"CreateDevice"))return 2;
  ID3D9VkInteropDevice *interop=nullptr;
  if(!checked(device->QueryInterface(__uuidof(ID3D9VkInteropDevice),reinterpret_cast<void**>(&interop)),"NativeInterop"))return 2;
  checked(device->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE),"SetFVF");
  checked(device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE),"CullNone");checked(device->SetRenderState(D3DRS_LIGHTING,FALSE),"LightingOff");
  checked(device->SetRenderState(D3DRS_ZENABLE,TRUE),"ZEnable");checked(device->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL),"ZFunc");
  checked(device->SetRenderState(D3DRS_ZWRITEENABLE,FALSE),"ZWriteOff");checked(device->SetRenderState(D3DRS_COLORWRITEENABLE,0),"ColorWriteOff");
  D3DVIEWPORT9 viewport{0,0,128,128,0,1};checked(device->SetViewport(&viewport),"Viewport");
  std::vector<IDirect3DQuery9*> queries(count);for(auto &q:queries)if(!checked(device->CreateQuery(D3DQUERYTYPE_OCCLUSION,&q),"CreateQuery"))return 2;
  uint64_t wall=0,cpu=0,poll_wall=0,poll_cpu=0,polls=0,pending=0;unsigned measured=0;
  for(unsigned frame=0;frame<frames+10;++frame) {
    uint64_t start=clockns(CLOCK_MONOTONIC),cpu_start=clockns(CLOCK_THREAD_CPUTIME_ID);
    checked(device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL,0,1.0f,0),"Clear");
    checked(device->BeginScene(),"BeginScene");
    for(auto *q:queries){checked(q->Issue(D3DISSUE_BEGIN),"Begin");checked(triangle(device,.5f),"Draw");checked(q->Issue(D3DISSUE_END),"End");}
    checked(device->EndScene(),"EndScene");
    if(!strcmp(submit,"explicit"))interop->FlushRenderingCommands();
    uint64_t wait_start=clockns(CLOCK_MONOTONIC),wait_cpu=clockns(CLOCK_THREAD_CPUTIME_ID),frame_polls=0,frame_pending=0;
    for(auto *q:queries) {
      HRESULT hr=S_FALSE;DWORD samples=0xa5a55a5a;
      while(hr==S_FALSE&&clockns(CLOCK_MONOTONIC)-wait_start<10000000000ull) {
        hr=q->GetData(&samples,4,D3DGETDATA_FLUSH);++frame_polls;
        if(hr==S_FALSE){
          ++frame_pending;
          if(!strcmp(mode,"yield"))sched_yield();
          else if(!strcmp(mode,"sleep50")){timespec delay{0,50000};nanosleep(&delay,nullptr);}
        }
      }
      if(hr!=S_OK||samples!=6328){printf("FAIL real query frame=%u hr=%08x samples=%u\n",frame,unsigned(hr),samples);++failures;}
    }
    uint64_t end=clockns(CLOCK_MONOTONIC),cpu_end=clockns(CLOCK_THREAD_CPUTIME_ID);
    if(frame>=10){++measured;wall+=end-start;cpu+=cpu_end-cpu_start;poll_wall+=end-wait_start;poll_cpu+=cpu_end-wait_cpu;polls+=frame_polls;pending+=frame_pending;}
    if(failures)break;
  }
  double scale=1e-6/(measured?measured:1);
  printf("%s query-poll-perf mode=%s submit=%s queries-per-frame=%u frames=%u wall-ms=%.3f owner-cpu-ms=%.3f poll-wall-ms=%.3f poll-cpu-ms=%.3f polls-per-frame=%.1f pending-per-frame=%.1f genuine-precise-count=6328 failures=%u\n",failures?"FAIL":"PASS",mode,submit,count,measured,wall*scale,cpu*scale,poll_wall*scale,poll_cpu*scale,double(polls)/(measured?measured:1),double(pending)/(measured?measured:1),failures);
  for(auto *q:queries)q->Release();interop->Release();device->Release();d3d->Release();return failures?1:0;
}
