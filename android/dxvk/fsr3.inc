/* The SDK records on DXVK's queue; two real Presents display midpoint and current frame. */
extern "C" int void_postfx_view(float*,uint32_t*) __attribute__((weak));
using Fsr3Frame=int(*)(void*,void*,const uint32_t*,float,float,float,int,int,PFN_vkGetInstanceProcAddr);
struct Fsr3State {
    IDirect3DSurface9 *depth=nullptr;uint32_t rect[4]{},epoch=0;uint64_t bestArea=0;
    std::chrono::steady_clock::time_point last{};bool reset=true,failed=false,reported=false;
};
static std::map<IDirect3DDevice9*,Fsr3State> fsr3States;
static PFN_vkGetInstanceProcAddr fsr3Loader;
static Fsr3Frame fsr3Frame;
static void (*fsr3Reset)();static void (*fsr3Camera)(const float*);static const char*(*fsr3Error)();
static bool fsr3Wanted(){const char *s=getenv("UT3_FRAME_GENERATION");return s&&!strcmp(s,"1");}
static void fsr3Clear(IDirect3DDevice9 *device) {
    auto it=fsr3States.find(device);if(it==fsr3States.end())return;
    if(fsr3Reset)fsr3Reset();if(it->second.depth)it->second.depth->Release();fsr3States.erase(it);
}
static void fsr3Observe(IDirect3DDevice9 *d) {
    if(!fsr3Wanted())return;
    DWORD z=0;if(FAILED(d->GetRenderState(D3DRS_ZENABLE,&z))||!z)return;
    D3DVIEWPORT9 v{};if(FAILED(d->GetViewport(&v)))return;
    uint32_t w=display_width.load(),h=display_height.load();
    if(v.Width<w/4||v.Height<h/4||std::abs(double(v.Width)*h-double(v.Height)*w)>double(w)*v.Height*.03)return;
    auto &s=fsr3States[d];if(uint64_t(v.Width)*v.Height<s.bestArea)return;
    IDirect3DSurface9 *depth=nullptr;if(FAILED(d->GetDepthStencilSurface(&depth))||!depth)return;
    D3DSURFACE_DESC desc{};
    if(FAILED(depth->GetDesc(&desc))||v.X+v.Width>desc.Width||v.Y+v.Height>desc.Height){depth->Release();return;}
    if(s.depth)s.depth->Release();s.depth=depth;s.rect[0]=v.X;s.rect[1]=v.Y;s.rect[2]=v.Width;s.rect[3]=v.Height;s.bestArea=uint64_t(v.Width)*v.Height;
}
static bool fsr3Begin(IDirect3DDevice9 *d) {
    if(!fsr3Wanted()||!void_postfx_view)return false;
    auto &s=fsr3States[d];float camera[13];uint32_t epoch;
    bool valid=s.bestArea&&s.depth&&void_postfx_view(camera,&epoch);s.bestArea=0;
    if(!valid){s.reset=true;return false;}if(s.failed)return false;
    if(!fsr3Frame){
        void *library=dlopen("libut3_fsr3.so",RTLD_NOW|RTLD_LOCAL);
        if(library){fsr3Frame=reinterpret_cast<Fsr3Frame>(dlsym(library,"ut3_fsr3_frame"));fsr3Reset=reinterpret_cast<void(*)()>(dlsym(library,"ut3_fsr3_reset"));fsr3Camera=reinterpret_cast<void(*)(const float*)>(dlsym(library,"ut3_fsr3_camera"));fsr3Error=reinterpret_cast<const char*(*)()>(dlsym(library,"ut3_fsr3_error"));}
        if(!fsr3Frame||!fsr3Reset||!fsr3Camera||!fsr3Error||!fsr3Loader){const char *detail=dlerror();ut3_dxvk_host_log(detail?detail:"FSR3 runtime/loader unavailable");s.failed=true;return false;}
    }
    auto now=std::chrono::steady_clock::now();float dt=s.last.time_since_epoch().count()?std::chrono::duration<float,std::milli>(now-s.last).count():16.667f;s.last=now;
    fsr3Camera(camera+1);
    int generated=fsr3Frame(d,s.depth,s.rect,camera[0],10.f,dt,s.reset||s.epoch!=epoch,0,fsr3Loader);
    s.reset=false;s.epoch=epoch;
    if(generated<0){ut3_dxvk_host_log((std::string("FSR3 disabled: ")+fsr3Error()).c_str());s.failed=true;return false;}
    if(generated&&!s.reported){ut3_dxvk_host_log("FSR3.1 optical-flow interpolation active: scene depth + camera, midpoint and real frame, two Presents");s.reported=true;}
    return generated==1;
}
static bool fsr3Restore(IDirect3DDevice9 *d) {
    auto &s=fsr3States.at(d);
    int result=fsr3Frame(d,s.depth,s.rect,1.f,10.f,16.f,0,1,fsr3Loader);
    if(result<0){ut3_dxvk_host_log((std::string("FSR3 restore failed: ")+fsr3Error()).c_str());s.failed=true;}
    return result==1;
}
