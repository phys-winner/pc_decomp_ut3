/* Native D3D9 format query against the actual Vulkan adapter; no VkDevice,
 * surface, swapchain, draw, Android app or CPU policy operations. The WSI
 * callback describes only a logical test monitor, never GPU capabilities. */
#include <d3d9.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <cstdint>

static int monitor(uint32_t* width,uint32_t* height,uint32_t* refresh) {
  *width=640;*height=480;*refresh=60;return 1;
}
int main(int argc,char** argv) {
  if(argc!=2)return 2;
  setenv("DXVK_LOG_PATH","none",1);
  setenv("DXVK_WSI_DRIVER","Android",1);
  void* module=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
  if(!module){std::fprintf(stderr,"%s\n",dlerror());return 2;}
  using Callbacks=void(*)(int(*)(uint32_t*,uint32_t*,uint32_t*),void(*)(const void*,uint64_t,int32_t),void(*)());
  auto callbacks=reinterpret_cast<Callbacks>(dlsym(module,"ut3_dxvk_set_callbacks_v2"));
  auto create=reinterpret_cast<IDirect3D9*(*)(UINT)>(dlsym(module,"Direct3DCreate9"));
  if(!callbacks||!create)return 2;
  callbacks(monitor,nullptr,nullptr);
  IDirect3D9* api=create(D3D_SDK_VERSION);
  if(!api)return 2;
  D3DCAPS9 caps{};HRESULT caps_hr=api->GetDeviceCaps(0,D3DDEVTYPE_HAL,&caps);
  std::printf("{\"native_d3d9_caps_hresult\":%u,\"pixel_shader_version\":%u,\"formats\":[",uint32_t(caps_hr),caps.PixelShaderVersion);
  const D3DFORMAT formats[]={D3DFMT_DXT1,D3DFMT_DXT2,D3DFMT_DXT3,D3DFMT_DXT4,D3DFMT_DXT5,D3DFORMAT(MAKEFOURCC('A','T','I','1')),D3DFORMAT(MAKEFOURCC('A','T','I','2'))};
  unsigned failures=FAILED(caps_hr)?1:0;
  for(unsigned i=0;i<sizeof(formats)/sizeof(*formats);++i){
    HRESULT sampled=api->CheckDeviceFormat(0,D3DDEVTYPE_HAL,D3DFMT_X8R8G8B8,0,D3DRTYPE_TEXTURE,formats[i]);
    HRESULT srgb=api->CheckDeviceFormat(0,D3DDEVTYPE_HAL,D3DFMT_X8R8G8B8,D3DUSAGE_QUERY_SRGBREAD,D3DRTYPE_TEXTURE,formats[i]);
    if(i)std::putchar(',');
    std::printf("{\"d3d_format\":%u,\"sampled_hresult\":%u,\"srgb_hresult\":%u}",uint32_t(formats[i]),uint32_t(sampled),uint32_t(srgb));
    if(FAILED(sampled)||(i<5&&FAILED(srgb)))++failures;
  }
  std::printf("],\"failures\":%u,\"scope\":\"Native D3D9 queries physical format support; logical test monitor only. No texture upload/device/surface/gameplay proof.\"}\n",failures);
  api->Release();return failures?1:0;
}
