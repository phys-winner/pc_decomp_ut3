/* Real native D3D9/Vulkan occlusion oracle. No Android window or presentation.
 * The visible and depth-hidden draws must be distinguished by actual GPU data. */
#include <d3d9.h>
#include <dlfcn.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>
#include "../android/dxvk/turnip.inc"
#ifndef UT3_ORACLE_CYCLES
#define UT3_ORACLE_CYCLES 2
#endif
#ifndef UT3_ORACLE_GENERATIONS
#define UT3_ORACLE_GENERATIONS 2
#endif

static unsigned failures;
static bool checked(HRESULT hr,const char *operation) {
  if(FAILED(hr)){printf("FAIL %s HRESULT=%08x\n",operation,unsigned(hr));++failures;return false;}return true;
}
struct Vertex {float x,y,z,rhw;DWORD color;};
static HRESULT triangle(IDirect3DDevice9 *device,float z) {
  const Vertex vertices[]={{8,8,z,1,0xffffffff},{120,8,z,1,0xffffffff},{8,120,z,1,0xffffffff}};
  return device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,1,vertices,sizeof(Vertex));
}
static bool image(IDirect3DDevice9 *device,IDirect3DSurface9 *readback,const char *name,bool colored) {
  IDirect3DSurface9 *target=nullptr;
  if(!checked(device->GetRenderTarget(0,&target),"GetRenderTarget"))return false;
  D3DSURFACE_DESC description{};checked(target->GetDesc(&description),"GetRenderTargetDesc");
  bool copied=checked(device->GetRenderTargetData(target,readback),"GetRenderTargetData");target->Release();
  if(!copied)return false;
  D3DLOCKED_RECT lock{};if(!checked(readback->LockRect(&lock,nullptr,D3DLOCK_READONLY),"ReadbackLock"))return false;
  unsigned nonblack=0,white=0;
  for(unsigned y=0;y<description.Height;++y)for(unsigned x=0;x<description.Width;++x) {
    DWORD pixel=*reinterpret_cast<const DWORD*>(static_cast<const char*>(lock.pBits)+y*lock.Pitch+x*4);
    if(pixel&0x00ffffff)++nonblack;
    if((pixel&0x00ffffff)==0x00ffffff)++white;
  }
  checked(readback->UnlockRect(),"ReadbackUnlock");
  bool pass=colored?white>100:nonblack==0;
  printf("%s %s readback=%ux%u format=%u nonblack-pixels=%u white-pixels=%u expected=%s\n",pass?"PASS":"FAIL",name,description.Width,description.Height,unsigned(description.Format),nonblack,white,colored?"white triangle":"black");
  if(!pass)++failures;return pass;
}
static bool result(IDirect3DQuery9 *query,const char *name,bool visible) {
  DWORD samples=0xa5a55a5a;HRESULT hr=S_FALSE;unsigned polls=0;
  auto start=std::chrono::steady_clock::now();
  while(hr==S_FALSE&&std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<10) {
#if defined(UT3_ORACLE_QUERY_GETDATA)
    hr=UT3_ORACLE_QUERY_GETDATA(query,&samples,sizeof samples,D3DGETDATA_FLUSH);++polls;
#else
    hr=query->GetData(&samples,sizeof samples,D3DGETDATA_FLUSH);++polls;
#endif
    if(hr==S_FALSE)std::this_thread::sleep_for(std::chrono::microseconds(100));
  }
  bool pass=hr==S_OK&&((samples!=0)==visible);
  printf("%s %s HRESULT=%08x actual-samples=%u polls=%u expected=%s\n",pass?"PASS":"FAIL",name,unsigned(hr),samples,polls,visible?"positive":"zero");
  if(!pass)++failures;return pass;
}
int main() {
  setenv("DXVK_WSI_DRIVER","Android",1);setenv("DXVK_FORCE_WINDOWED","1",1);
  void *library=dlopen("libdxvk_d3d9.so",RTLD_NOW|RTLD_LOCAL);
  if(!library){puts(dlerror());return 2;}
  if(const char *directory=getenv("UT3_TURNIP_DIR")) {
    char error[512]{};auto loader=ut3_open_turnip(directory,nullptr,error,sizeof error);
    auto set_loader=reinterpret_cast<void(*)(PFN_vkGetInstanceProcAddr)>(dlsym(library,"ut3_dxvk_set_vulkan_loader"));
    if(!loader||!set_loader){puts(loader?"FAIL custom loader callback missing":error);return 2;}
    set_loader(loader);
  }
  auto create=reinterpret_cast<IDirect3D9*(*)(UINT)>(dlsym(library,"Direct3DCreate9"));
  if(!create){puts(dlerror());return 2;}
  IDirect3D9 *d3d=create(D3D_SDK_VERSION);if(!d3d){puts("FAIL Direct3DCreate9");return 2;}
  D3DADAPTER_IDENTIFIER9 adapter{};d3d->GetAdapterIdentifier(0,0,&adapter);
  printf("Actual native adapter: %s vendor=%04x device=%04x\n",adapter.Description,adapter.VendorId,adapter.DeviceId);
  D3DPRESENT_PARAMETERS parameters{};parameters.BackBufferWidth=128;parameters.BackBufferHeight=128;
  parameters.BackBufferFormat=D3DFMT_A8R8G8B8;parameters.BackBufferCount=1;
  parameters.SwapEffect=D3DSWAPEFFECT_DISCARD;parameters.Windowed=TRUE;
  parameters.EnableAutoDepthStencil=TRUE;parameters.AutoDepthStencilFormat=D3DFMT_D24S8;
  parameters.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
  IDirect3DDevice9 *device=nullptr;
  if(!checked(d3d->CreateDevice(0,D3DDEVTYPE_HAL,nullptr,D3DCREATE_HARDWARE_VERTEXPROCESSING,&parameters,&device),"CreateDevice offscreen")){d3d->Release();return 2;}
  IDirect3DQuery9 *query=nullptr;if(!checked(device->CreateQuery(D3DQUERYTYPE_OCCLUSION,&query),"CreateQuery"))return 2;
  IDirect3DSurface9 *readback=nullptr;
  if(!checked(device->CreateOffscreenPlainSurface(128,128,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&readback,nullptr),"CreateReadback"))return 2;
  D3DVIEWPORT9 viewport{0,0,128,128,0,1};checked(device->SetViewport(&viewport),"SetViewport");
  checked(device->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE),"SetFVF");
  checked(device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE),"CullNone");
  checked(device->SetRenderState(D3DRS_LIGHTING,FALSE),"LightingOff");
  checked(device->SetRenderState(D3DRS_ZENABLE,TRUE),"ZEnable");
  checked(device->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESSEQUAL),"ZFunc");
  checked(device->SetRenderState(D3DRS_ZWRITEENABLE,FALSE),"ZWriteOff");
  checked(device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1),"ColorOp");
  checked(device->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE),"ColorArg");
  for(unsigned cycle=0;cycle<UT3_ORACLE_CYCLES;++cycle) {
#if defined(UT3_ORACLE_CYCLE_BEGIN)
    UT3_ORACLE_CYCLE_BEGIN(cycle);
#endif
    for(unsigned depth_write=0;depth_write<2;++depth_write)for(unsigned color=0;color<2;++color)for(unsigned hidden=0;hidden<2;++hidden) {
    checked(device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL,0,hidden?0.25f:1.0f,0),"Clear");
    checked(device->SetRenderState(D3DRS_COLORWRITEENABLE,color?0xf:0),"ColorWrite");
    checked(device->SetRenderState(D3DRS_ZWRITEENABLE,depth_write),"ZWrite");
    checked(device->BeginScene(),"BeginScene");
    checked(query->Issue(D3DISSUE_BEGIN),"IssueBegin");
    checked(triangle(device,0.5f),"DrawPrimitiveUP");
    checked(query->Issue(D3DISSUE_END),"IssueEnd");
    checked(device->EndScene(),"EndScene");
    char name[96];snprintf(name,sizeof name,"cycle=%u color-write=%u depth-write=%u depth-hidden=%u",cycle,color,depth_write,hidden);
#if defined(QUERY_READBACK_BEFORE_GETDATA)
    image(device,readback,name,color&&!hidden);
    result(query,name,!hidden);
#else
    result(query,name,!hidden);
    image(device,readback,name,color&&!hidden);
#endif
    }
  }
  // Keep more than two full 64-slot pools outstanding before reading anything.
  // Reuse every logical query in a second generation, including real zeros.
  std::vector<IDirect3DQuery9*> outstanding(129);
  for(auto &entry:outstanding)if(!checked(device->CreateQuery(D3DQUERYTYPE_OCCLUSION,&entry),"CreateOutstandingQuery"))return 2;
  for(unsigned generation=0;generation<UT3_ORACLE_GENERATIONS;++generation) {
#if defined(UT3_ORACLE_BATCH_BEGIN)
    UT3_ORACLE_BATCH_BEGIN(generation);
#endif
    checked(device->SetRenderState(D3DRS_COLORWRITEENABLE,(generation&1)?0xf:0),"BatchColorWrite");
    checked(device->SetRenderState(D3DRS_ZWRITEENABLE,FALSE),"BatchZWriteOff");
    for(unsigned i=0;i<outstanding.size();++i) {
      bool hidden=i%3==1;
      checked(device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL,0,hidden?0.25f:1.0f,0),"BatchClear");
      checked(device->BeginScene(),"BatchBeginScene");
      checked(outstanding[i]->Issue(D3DISSUE_BEGIN),"BatchIssueBegin");
      checked(triangle(device,0.5f),"BatchDraw");
      checked(outstanding[i]->Issue(D3DISSUE_END),"BatchIssueEnd");
      checked(device->EndScene(),"BatchEndScene");
    }
    image(device,readback,"129-outstanding final draw",(generation&1)!=0);
    unsigned before=failures;
    for(unsigned i=0;i<outstanding.size();++i) {
      char name[96];snprintf(name,sizeof name,"129-outstanding generation=%u query=%u",generation,i);
      result(outstanding[i],name,i%3!=1);
    }
    printf("%s 129-outstanding generation=%u failures=%u; no query read until all129 issued\n",before==failures?"PASS":"FAIL",generation,failures-before);
  }
  for(auto *entry:outstanding)entry->Release();
  readback->Release();query->Release();device->Release();d3d->Release();
  printf("%s real GPU native D3D9 occlusion oracle failures=%u; no surface/Present/app input\n",failures?"FAIL":"PASS",failures);
  return failures?1:0;
}
