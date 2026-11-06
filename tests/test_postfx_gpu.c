/* Real GLES: spatial reconstruction/AA, VPOS row gradient, restored D3D9 state and resource reset. */
#include <assert.h>
#include <unistd.h>
#include "../android/host/d3d9_device.c"
#include "../android/host/postfx.c"
void void_process_exit(uint32_t code,int detach){(void)detach;_exit((int)code);}

static uint32_t shader(uint32_t device,int pixel,const char *source,uint32_t scratch)
{
    const MOJOSHADER_parseData *p=MOJOSHADER_assemble("postfx-test",source,(unsigned)strlen(source),NULL,0,NULL,0,NULL,0,NULL,NULL,NULL,NULL,NULL);
    if(p->error_count)for(int i=0;i<p->error_count;++i)puts(p->errors[i].error);
    assert(!p->error_count&&p->output_len>0);uint32_t code=void_galloc(p->output_len);memcpy(GPTR(code),p->output,p->output_len);
    assert(!FX(device,pixel?106:91,code,scratch));uint32_t result=MEM32(scratch);void_gfree(code);MOJOSHADER_freeParseData(p);return result;
}
int main(void)
{
    setvbuf(stdout,NULL,_IONBF,0);assert(void_guest_reserve());void_threads_init();void_dispatch_init();void_machine_enter();
    setenv("VOID_RENDER_THREAD","1",1);
    d3d9_t *root=calloc(1,sizeof *root);uint32_t api=void_com_wrap(&root->com,&d3d9_class,0);
    uint32_t scratch=void_galloc(2048),device;
    uint32_t pp[]={128,64,21,1,0,0,1,0,1,0,0,0,0,0};memcpy(GPTR(scratch),pp,sizeof pp);
    assert(!d3d_device_create(api,0,0,scratch,&device));d3d_device *d=void_com_host(device);
    assert(!FX(device,23,64,32,1,1,21,0,scratch,0));uint32_t texture=MEM32(scratch);
    assert(!FX(texture,19,0,scratch,0,0));uint32_t pitch=MEM32(scratch),data=MEM32(scratch+4);
    for(unsigned y=0;y<32;++y)for(unsigned x=0;x<64;++x)MEM32(data+y*pitch+x*4)=0xff000040u|((x*255/63)<<16)|((y*255/31)<<8);
    assert(!FX(texture,20,0));
    const char *vs="vs_3_0\ndcl_position0 v0\ndcl_texcoord0 v1\ndcl_position0 o0\ndcl_texcoord0 o1\nmov o0,v0\nmov o1,v1\n";
    const char *ps="ps_3_0\ndcl_texcoord0 v0.xy\ndcl_2d s0\ntexld r0,v0,s0\nmov oC0,r0\n";
    uint32_t vertex=shader(device,0,vs,scratch),pixel=shader(device,1,ps,scratch);
    const d3d_vertex_element elements[]={{0,0,3,0,0,0},{0,16,1,0,5,0},{0xff,0,17,0,0,0}};
    memcpy(GPTR(scratch),elements,sizeof elements);assert(!FX(device,86,scratch,scratch+64));uint32_t decl=MEM32(scratch+64);
    float vertices[]={-1,1,0,1,0,0,1,1,0,1,1,0,-1,-1,0,1,0,1,1,-1,0,1,1,1};memcpy(GPTR(scratch+128),vertices,sizeof vertices);
    assert(!FX(device,87,decl)&&!FX(device,92,vertex)&&!FX(device,107,pixel)&&!FX(device,65,0,texture));
    assert(!FX(device,57,7,0)&&!FX(device,57,27,0)&&!FX(device,57,15,0)&&!FX(device,57,22,1));
    assert(!FX(device,69,0,1,3)&&!FX(device,69,0,2,3)&&!FX(device,69,0,5,1)&&!FX(device,69,0,6,1));
    d3d_state saved=d->st;float *rgba=malloc(128*64*4*sizeof *rgba);uint32_t rect[]={0,0,128,64};
    for(unsigned mode=0;mode<4;++mode){
        setenv("UT3_FSR1",(mode&1)?"1":"0",1);setenv("UT3_ANTIALIAS",(mode&2)?"1":"0",1);void_postfx_present(device);
        assert(!FX(device,83,5,2,scratch+128,24));assert(!memcmp(&d->st,&saved,sizeof saved));assert(d->rt[0]==d->backbuffer);
        assert(d3d_gl_read_rgba(d->backbuffer,rect,rgba));
        float top=rgba[(8*128+64)*4+1],bottom=rgba[(55*128+64)*4+1];
        printf("mode %u row gradient %.4f..%.4f\n",mode,top,bottom);
        assert(fabsf(bottom-top)>.6f);
        assert(fabsf(rgba[(32*128+112)*4]-rgba[(32*128+16)*4])>.6f);
        for(unsigned i=0;i<128*64*4;++i)assert(isfinite(rgba[i])&&rgba[i]>=0&&rgba[i]<=1);
        assert(mode==0||postfx.frame_done);
    }
    void_postfx_reset(device);assert(!postfx.device&&!postfx.scratch&&!postfx.texture[0]);
    puts("PASS real GLES FSR1/FXAA, both axes and finite output, exact D3D9 state restoration, resource reset");
    return 0;
}
