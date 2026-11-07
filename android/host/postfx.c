/* Scene post-processing through the same D3D9 interface on GLES and DXVK. */
#include "host.h"
#include <math.h>
#include <errno.h>
#include <jni.h>
#include "postfx_shaders.inc"

static struct {
    uint32_t device,scratch,texture[3],surface[3],shader[3];
    uint32_t width,height,input_w,input_h;
    int busy,reported,frame_done;
} postfx;

void void_postfx_present(uint32_t device){if(!device||device==postfx.device)postfx.frame_done=0;}

/* The choices of this run: 1 FSR1, 2 FXAA. Read once: every DrawPrimitiveUP asks, and two
 * getenv scans per draw cost the game thread time. */
enum { POSTFX_FSR1 = 1, POSTFX_FXAA = 2 };
static int postfx_choices = -1;
static int postfx_enabled(void)
{
    int choices = __atomic_load_n(&postfx_choices, __ATOMIC_RELAXED);
    if (choices < 0) {
        const char *fsr = getenv("UT3_FSR1"), *aa = getenv("UT3_ANTIALIAS");
        choices = (fsr && atoi(fsr) ? POSTFX_FSR1 : 0) | (aa && atoi(aa) ? POSTFX_FXAA : 0);
        __atomic_store_n(&postfx_choices, choices, __ATOMIC_RELAXED);
    }
    return choices;
}

JNIEXPORT jint JNICALL Java_org_ut3_port_GameActivity_nativePostfx(JNIEnv *env,jclass cls,jboolean fsr,jboolean aa)
{
    (void)env;(void)cls;
    __atomic_store_n(&postfx_choices, -1, __ATOMIC_RELAXED);
    return setenv("UT3_FSR1",fsr?"1":"0",1)||setenv("UT3_ANTIALIAS",aa?"1":"0",1)?errno:0;
}

static uint32_t fxcall(uint32_t object,unsigned slot,unsigned count,const uint32_t *values)
{
    uint32_t args[10]={object};
    if(count>9)void_fatal("Post-process COM argument overflow");
    if(count)memcpy(args+1,values,count*4);
    return void_call_guest(MEM32(MEM32(object)+slot*4),count+1,args,1);
}
#define FX(o,s,...) fxcall(o,s,sizeof((uint32_t[]){__VA_ARGS__})/4,(uint32_t[]){__VA_ARGS__})
static void fxrelease(uint32_t *object){if(*object){fxcall(*object,2,0,NULL);*object=0;}}

void void_postfx_reset(uint32_t device)
{
    if(postfx.busy||device!=postfx.device)return;
    postfx.busy=1;
    for(unsigned i=0;i<3;++i){fxrelease(&postfx.surface[i]);fxrelease(&postfx.texture[i]);fxrelease(&postfx.shader[i]);}
    if(postfx.scratch)void_gfree(postfx.scratch);
    memset(&postfx,0,sizeof postfx);
}

static int fxresources(uint32_t device,uint32_t w,uint32_t h,uint32_t iw,uint32_t ih)
{
    if(postfx.device==device&&postfx.width==w&&postfx.height==h&&postfx.input_w==iw&&postfx.input_h==ih&&postfx.texture[2])return 1;
    /* The caller has no live scratch data at this point. */
    postfx.busy=0;void_postfx_reset(postfx.device);postfx.busy=1;
    postfx.device=device;postfx.width=w;postfx.height=h;postfx.input_w=iw;postfx.input_h=ih;
    postfx.scratch=void_galloc(4096);
    uint32_t s=postfx.scratch;
    const uint32_t *programs[]={fsr_easu,fsr_rcas,postfx_aa};
    const size_t sizes[]={sizeof fsr_easu,sizeof fsr_rcas,sizeof postfx_aa};
    for(unsigned i=0;i<3;++i){
        uint32_t code=void_galloc((uint32_t)sizes[i]);memcpy(GPTR(code),programs[i],sizes[i]);
        uint32_t hr=FX(device,106,code,s);void_gfree(code);if(hr)return 0;
        postfx.shader[i]=MEM32(s);
        if(FX(device,23,i==2?w:iw,i==2?h:ih,1,1,21,0,s,0))return 0;
        postfx.texture[i]=MEM32(s);
        if(FX(postfx.texture[i],18,0,s))return 0;
        postfx.surface[i]=MEM32(s);
    }
    return 1;
}

static int fxpass(uint32_t source,uint32_t target,unsigned shader,uint32_t w,uint32_t h,uint32_t iw,uint32_t ih)
{
    uint32_t d=postfx.device,s=postfx.scratch;
    float vertices[]={-.5f,-.5f,0,1,0,0,(float)w-.5f,-.5f,0,1,1,0,
        -.5f,(float)h-.5f,0,1,0,1,(float)w-.5f,(float)h-.5f,0,1,1,1};
    memcpy(GPTR(s+512),vertices,sizeof vertices);
    uint32_t viewport[]={0,0,w,h,0,0x3f800000};memcpy(GPTR(s+384),viewport,24);
    float constants[20]={
        (float)iw/w,(float)ih/h,.5f*iw/w-.5f,.5f*ih/h-.5f,
        1.f/iw,1.f/ih,1.f/iw,-1.f/ih,
        -1.f/iw,2.f/ih,1.f/iw,2.f/ih,0,4.f/ih,0,0,
        (float)iw,(float)ih,1.f/iw,1.f/ih};
    if(shader==1)constants[0]=.5f;
    memcpy(GPTR(s+640),constants,sizeof constants);
    return !(FX(d,37,0,target)||FX(d,47,s+384)||FX(d,65,0,source)||FX(d,107,postfx.shader[shader])||
        FX(d,109,0,s+640,5)||FX(d,83,5,2,s+512,24));
}

/* Called before the original UP draw. A full-screen scene resolve precedes all HUD draws. */
int void_postfx_draw(uint32_t device,unsigned slot,const uint32_t *a,uint32_t *result)
{
    int choices=postfx_enabled(),fsr=choices&POSTFX_FSR1,aa=choices&POSTFX_FXAA;
    if(postfx.busy||postfx.frame_done||!choices)return 0;
    if(!((a[0]==4||a[0]==5)&&a[slot==83?1:3]==2))return 0;
    postfx.busy=1;
    uint32_t s=void_galloc(1024),rt=0,bb=0,tex=0,decl=0,ds=0,state=0;
    int handled=0;
    if(FX(device,58,7,s)||MEM32(s)||FX(device,58,27,s)||MEM32(s))goto done;
    if(FX(device,38,0,s)||(rt=MEM32(s))==0||FX(device,18,0,0,0,s)||(bb=MEM32(s))!=rt)goto done;
    if(FX(bb,12,s+32)||FX(device,48,s+80))goto done;
    uint32_t w=MEM32(s+56),h=MEM32(s+60);
    if(MEM32(s+80)||MEM32(s+84)||MEM32(s+88)!=w||MEM32(s+92)!=h)goto done;
    if(FX(device,64,0,s)||(tex=MEM32(s))==0||fxcall(tex,10,0,NULL)!=3||FX(tex,17,0,s+32)||!(MEM32(s+40)&1))goto done;
    uint32_t tw=MEM32(s+56),th=MEM32(s+60);
    if(FX(device,88,s)||(decl=MEM32(s))==0)goto done;
    MEM32(s+4)=65;if(FX(decl,4,s+128,s+4))goto done;
    uint32_t uv=UINT32_MAX,pos=UINT32_MAX;
    for(unsigned i=0;i<MEM32(s+4)&&i<65;++i){
        const uint8_t *e=GPTR(s+128+i*8);uint16_t stream,offset;memcpy(&stream,e,2);memcpy(&offset,e+2,2);
        if(stream==0&&e[4]==1&&e[6]==5&&e[7]==0)uv=offset;
        if(stream==0&&e[4]<=3&&e[6]==0&&e[7]==0)pos=offset;
    }
    uint32_t data=a[slot==83?2:6],stride=a[slot==83?3:7],count=slot==83?(a[0]==4?6:4):a[2];
    if(uv==UINT32_MAX||pos==UINT32_MAX||stride<uv+8||stride<pos+8||count<4||count>6||!void_vm_readable(data,stride*count))goto done;
    float lo[2]={1.e10f,1.e10f},hi[2]={-1.e10f,-1.e10f},pmin[2]={1.e10f,1.e10f},pmax[2]={-1.e10f,-1.e10f};
    for(unsigned i=0;i<count;++i){
        float p[2];memcpy(p,GPTR(data+i*stride+uv),8);
        for(unsigned j=0;j<2;++j){if(!isfinite(p[j]))goto done;lo[j]=fminf(lo[j],p[j]);hi[j]=fmaxf(hi[j],p[j]);}
        memcpy(p,GPTR(data+i*stride+pos),8);
        for(unsigned j=0;j<2;++j){if(!isfinite(p[j]))goto done;pmin[j]=fminf(pmin[j],p[j]);pmax[j]=fmaxf(pmax[j],p[j]);}
    }
    float spanx=pmax[0]-pmin[0],spany=pmax[1]-pmin[1];
    if(!((fabsf(spanx-2)<.02f&&fabsf(spany-2)<.02f)||(fabsf(spanx-w)<2&&fabsf(spany-h)<2)))goto done;
    uint32_t iw=(uint32_t)lroundf((hi[0]-lo[0])*tw),ih=(uint32_t)lroundf((hi[1]-lo[1])*th);
    if(iw<w/4||ih<h/4||iw>w||ih>h||lo[0]<-.01f||lo[1]<-.01f||hi[0]>1.01f||hi[1]>1.01f)goto done;
    if(!fsr){iw=w;ih=h;}
    if(!fxresources(device,w,h,iw,ih))goto done;
    if(FX(device,59,1,s)||(state=MEM32(s))==0)goto done;
    if(!FX(device,40,s))ds=MEM32(s);
    uint32_t viewport[]={0,0,iw,ih,0,0x3f800000};memcpy(GPTR(s+32),viewport,24);
    if(FX(device,39,0)||FX(device,37,0,postfx.surface[0])||FX(device,47,s+32))goto restore;
    *result=fxcall(device,slot,slot==83?4:8,a);
    if(*result)goto restore;
    /* Fixed-function POSITIONT quad; the original state is restored before UI continues. */
    if(FX(device,92,0)||FX(device,89,0x104)||FX(device,57,7,0)||FX(device,57,27,0)||
        FX(device,57,15,0)||FX(device,57,52,0)||FX(device,57,174,0)||FX(device,57,168,15)||
        FX(device,57,194,0)||FX(device,57,22,1)||FX(device,69,0,1,3)||FX(device,69,0,2,3)||
        FX(device,69,0,5,1)||FX(device,69,0,6,1)||FX(device,69,0,11,0))goto restore;
    uint32_t input=postfx.texture[0];
    if(aa){if(!fxpass(input,fsr?postfx.surface[1]:rt,2,iw,ih,iw,ih))goto restore;input=postfx.texture[1];}
    if(fsr){
        if(!fxpass(input,postfx.surface[2],0,w,h,iw,ih)||!fxpass(postfx.texture[2],rt,1,w,h,w,h))goto restore;
    }
    handled=1;
    postfx.frame_done=1;
    if(!postfx.reported++)void_log("[postfx] scene %ux%u -> %ux%u, FSR1 EASU/RCAS %s, FXAA %s; HUD follows",iw,ih,w,h,fsr?"on":"off",aa?"on":"off");
restore:
    FX(device,37,0,rt);FX(device,39,ds);
    if(state)FX(state,5);
    FX(device,47,s+80);
    if(handled){FX(device,100,0,0,0,0);if(slot==84)FX(device,104,0);}
done:
    fxrelease(&state);fxrelease(&ds);fxrelease(&decl);fxrelease(&tex);fxrelease(&bb);fxrelease(&rt);
    void_gfree(s);postfx.busy=0;
    return handled;
}
