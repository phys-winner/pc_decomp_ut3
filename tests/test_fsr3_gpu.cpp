/* Real SDK interpolation, moving-object midpoint, exact restoration and history reset on Turnip/DXVK. */
#include <d3d9.h>
#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <cassert>
#include "../android/dxvk/turnip.inc"
#include <cmath>
#include "../android/dxvk/vulkan_interop.inc"

#define OK(x) do {auto r=(x);if(r){printf("FAIL %s = %x\n",#x,unsigned(r));fflush(stdout);exit(2);}}while(0)
int main(){
    const char *dir=getenv("UT3_TURNIP_DIR");char error[512]{};
    auto loader=ut3_open_turnip(dir,nullptr,error,sizeof error);
    if(!loader){puts(error);return 2;}
    void *lib=dlopen("libdxvk_d3d9.so",RTLD_NOW|RTLD_LOCAL);if(!lib){puts(dlerror());return 2;}
    auto set=reinterpret_cast<void(*)(PFN_vkGetInstanceProcAddr)>(dlsym(lib,"ut3_dxvk_set_vulkan_loader"));set(loader);
    auto create=reinterpret_cast<IDirect3D9*(*)(UINT)>(dlsym(lib,"Direct3DCreate9"));
    setenv("DXVK_WSI_DRIVER","Android",1);setenv("DXVK_FORCE_WINDOWED","1",1);
    auto d3d=create(32);if(!d3d)return 2;
    D3DPRESENT_PARAMETERS p{};p.BackBufferWidth=320;p.BackBufferHeight=180;p.BackBufferFormat=D3DFMT_A8R8G8B8;
    p.BackBufferCount=1;p.SwapEffect=D3DSWAPEFFECT_DISCARD;p.Windowed=TRUE;
    IDirect3DDevice9 *d=nullptr;OK(d3d->CreateDevice(0,D3DDEVTYPE_HAL,nullptr,D3DCREATE_HARDWARE_VERTEXPROCESSING,&p,&d));
    IDirect3DSurface9 *depth=nullptr;OK(d->CreateDepthStencilSurface(320,180,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,TRUE,&depth,nullptr));
    void *plugin=dlopen("libut3_fsr3.so",RTLD_NOW|RTLD_LOCAL);if(!plugin){puts(dlerror());return 2;}
    using Frame=int(*)(void*,void*,const uint32_t*,float,float,float,int,int,PFN_vkGetInstanceProcAddr);
    auto frame=(Frame)dlsym(plugin,"ut3_fsr3_frame");auto errorText=(const char*(*)())dlsym(plugin,"ut3_fsr3_error");
    auto resetContext=(void(*)())dlsym(plugin,"ut3_fsr3_reset");
    auto camera=(void(*)(const float*))dlsym(plugin,"ut3_fsr3_camera");
    assert(frame&&errorText&&resetContext&&camera);
    const float view[]={0,0,0,0,1,0,1,0,0,0,0,1};camera(view);
    IDirect3DSurface9 *readback=nullptr;OK(d->CreateOffscreenPlainSurface(320,180,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&readback,nullptr));
    OK(d->SetDepthStencilSurface(depth));D3DVIEWPORT9 vp{0,0,320,180,0,1};OK(d->SetViewport(&vp));
    OK(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE));OK(d->SetRenderState(D3DRS_LIGHTING,FALSE));
    OK(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));OK(d->SetRenderState(D3DRS_ZENABLE,TRUE));
    OK(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));OK(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
    struct Vertex{float x,y,z,rhw;DWORD color;};
    auto quad=[&](float x,float y,float w,float h,float z,DWORD color){
        Vertex v[]={{x-.5f,y-.5f,z,1,color},{x+w-.5f,y-.5f,z,1,color},{x-.5f,y+h-.5f,z,1,color},{x+w-.5f,y+h-.5f,z,1,color}};
        return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,v,sizeof(Vertex));
    };
    double centroid=-1;
    auto pixels=[&](const std::string &name){
        IDirect3DSurface9 *bb;OK(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&bb));
        OK(d->GetRenderTargetData(bb,readback));bb->Release();D3DLOCKED_RECT locked;OK(readback->LockRect(&locked,nullptr,D3DLOCK_READONLY));
        std::vector<unsigned char> rgb(320*180*3);double sum=0,count=0;
        for(unsigned y=0;y<180;++y)for(unsigned x=0;x<320;++x){auto *p=(unsigned char*)locked.pBits+y*locked.Pitch+x*4;
            unsigned at=(y*320+x)*3;rgb[at]=p[2];rgb[at+1]=p[1];rgb[at+2]=p[0];
            if(p[2]>220&&p[1]>200&&p[0]<100){sum+=x;++count;}}
        OK(readback->UnlockRect());FILE *file=fopen((name+".ppm").c_str(),"wb");assert(file);fprintf(file,"P6\n320 180\n255\n");fwrite(rgb.data(),1,rgb.size(),file);fclose(file);
        centroid=count?sum/count:-1;assert(count>1000);
        printf("CENTROID %s x %.3f pixels %.0f\n",name.c_str(),count?sum/count:-1,count);fflush(stdout);
        return rgb;
    };
    uint32_t rect[]={0,0,320,180};unsigned generated=0;
    for(unsigned k=0;k<10;++k){
        OK(d->BeginScene());OK(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff202020,1,0));
        for(unsigned y=0;y<180;y+=20)for(unsigned x=0;x<320;x+=20)OK(quad(x,y,20,20,.8f,((x/20+y/20)&1)?0xff706858:0xff282c34));
        OK(quad(32+k*12,45,60,90,.25f,0xfff0e010));OK(d->EndScene());
        std::string tag="frame-"+std::to_string(k);auto current=pixels(tag+"-current");double currentX=centroid;
        int produced=frame(d,depth,rect,1.5707963268f,10.f,33.333f,k==0,0,loader);
        printf("FRAME %u generated %d %s\n",k,produced,errorText());fflush(stdout);if(produced<0)return 2;
        if(produced){auto middle=pixels(tag+"-middle");assert(middle!=current);assert(std::abs(centroid-(currentX-6))<2);++generated;
            assert(frame(d,depth,rect,1.5707963268f,10.f,33.333f,0,1,loader)==1);auto restored=pixels(tag+"-restored");assert(restored==current);}
    }
    // Camera cuts must discard history, and a destroyed context must initialize again.
    assert(frame(d,depth,rect,1.5707963268f,10.f,33.333f,1,0,loader)==0);
    resetContext();assert(frame(d,depth,rect,1.5707963268f,10.f,33.333f,0,0,loader)==0);
    resetContext();readback->Release();depth->Release();d->Release();d3d->Release();assert(generated==9);puts("PASS 9 actual interpolated frames differ from current; every real frame restored byte-exactly");fflush(stdout);
    return 0;
}
