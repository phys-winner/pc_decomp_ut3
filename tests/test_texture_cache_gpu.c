/* Real GLES cache upload and mod-write fallback using the production resource code. */
#include "../android/host/d3d9_internal.h"
#include "../android/texconv/ut3tx.h"
#include <EGL/egl.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

d3d_gl_limits_t d3d_gl_limits;
void d3d_gl_upload_unit(void) { glActiveTexture(GL_TEXTURE0); }
void d3d_gl_forget_texture(GLuint value) { (void)value; }
uint64_t void_perf_now_ns(void) { return 0; }
void void_perf_add(int kind,uint64_t ns,uint64_t bytes) { (void)kind;(void)ns;(void)bytes; }
void void_log(const char *format,...) { va_list args;va_start(args,format);vprintf(format,args);putchar('\n');va_end(args); }
char *void_path_to_host(const char *path) { (void)path;return NULL; }
/* Host runtime of d3d9_thread.c. Run with VOID_RENDER_THREAD=0: no device starts the render
 * thread here, every record runs inline in this test's GL context, and nothing holds a machine. */
void void_blocking_begin(void) {}
void void_blocking_end(void) {}
const char *void_data_dir(void) { return "."; }
uint64_t void_monotonic_ns(void) { struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000000u+(uint64_t)t.tv_nsec; }
_Noreturn void void_fatal(const char *format,...) { va_list args;va_start(args,format);vfprintf(stderr,format,args);fputc('\n',stderr);va_end(args);abort(); }
extern void void_texture_cache_init(const char*);
extern const utx_entry *void_texture_cache_find(uint32_t,uint32_t,uint32_t,uint32_t,const uint8_t*,uint32_t);
extern const uint8_t *void_texture_cache_raw(const utx_entry*);

static GLuint shader(GLenum kind,const char *source) {
    GLuint s=glCreateShader(kind);glShaderSource(s,1,&source,NULL);glCompileShader(s);GLint ok;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);assert(ok);return s;
}
static GLuint program;
static GLuint cube_program;
static void sample_cube(d3d_texture *texture,int face,int level,uint8_t pixel[4]) {
    static const float direction[6][3]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,1,1);glUseProgram(cube_program);glBindTexture(GL_TEXTURE_CUBE_MAP,texture->gl);
    glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_MIN_FILTER,GL_NEAREST_MIPMAP_NEAREST);glTexParameteri(GL_TEXTURE_CUBE_MAP,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glUniform1i(glGetUniformLocation(cube_program,"image"),0);glUniform1f(glGetUniformLocation(cube_program,"level"),(float)level);
    glUniform3fv(glGetUniformLocation(cube_program,"direction"),1,direction[face]);
    glDrawArrays(GL_TRIANGLES,0,3);glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);assert(glGetError()==GL_NO_ERROR);
}
static void sample(d3d_texture *texture,int level,uint8_t pixel[4]) {
    glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,1,1);glUseProgram(program);glBindTexture(GL_TEXTURE_2D,texture->gl);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST_MIPMAP_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glUniform1i(glGetUniformLocation(program,"image"),0);glUniform1f(glGetUniformLocation(program,"level"),(float)level);
    glDrawArrays(GL_TRIANGLES,0,3);glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);assert(glGetError()==GL_NO_ERROR);
}
int main(int argc,char **argv) {
    assert(argc==2||argc==4);
    EGLDisplay display=eglGetDisplay(EGL_DEFAULT_DISPLAY);assert(eglInitialize(display,NULL,NULL));
    EGLint attr[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,0x40,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    EGLConfig config;EGLint count;assert(eglChooseConfig(display,attr,&config,1,&count)&&count);
    EGLint ctx[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE},pbuf[]={EGL_WIDTH,1,EGL_HEIGHT,1,EGL_NONE};
    EGLContext context=eglCreateContext(display,config,EGL_NO_CONTEXT,ctx);EGLSurface surface=eglCreatePbufferSurface(display,config,pbuf);
    assert(eglMakeCurrent(display,surface,surface,context));
    const char *ext=(const char*)glGetString(GL_EXTENSIONS);d3d_gl_limits.astc_ldr=ext&&strstr(ext,"GL_KHR_texture_compression_astc_ldr");
    GLuint vs=shader(GL_VERTEX_SHADER,"#version 300 es\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0.,1.);}");
    GLuint fs=shader(GL_FRAGMENT_SHADER,"#version 300 es\nprecision highp float;uniform sampler2D image;uniform float level;out vec4 color;void main(){color=textureLod(image,vec2(0.),level);}");
    program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,fs);glLinkProgram(program);GLint linked;glGetProgramiv(program,GL_LINK_STATUS,&linked);assert(linked);
    GLuint cube_fs=shader(GL_FRAGMENT_SHADER,"#version 300 es\nprecision highp float;uniform samplerCube image;uniform vec3 direction;uniform float level;out vec4 color;void main(){color=textureLod(image,direction,level);}");
    cube_program=glCreateProgram();glAttachShader(cube_program,vs);glAttachShader(cube_program,cube_fs);glLinkProgram(cube_program);glGetProgramiv(cube_program,GL_LINK_STATUS,&linked);assert(linked);
    void_texture_cache_init(argv[1]);
    /* A rejected cache may contain invalid offsets; take the known texture from its valid source. */
    char path[4096];snprintf(path,sizeof path,"%s/UTGame/" TVTX_FILE,argc==4?argv[3]:argv[1]);FILE *file=fopen(path,"rb");assert(file);
    tvtx_header header;assert(fread(&header,1,sizeof header,file)==sizeof header);assert(fseeko(file,header.index_offset,SEEK_SET)==0);
    utx_entry selected={0};
    for(uint32_t i=0;i<header.count;i++) { utx_entry e;assert(fread(&e,1,sizeof e,file)==sizeof e);if(e.width>=8&&e.width==e.height&&e.levels>=3){selected=e;break;} }
    assert(selected.width);uint32_t format=selected.dxt==1?FOURCC('D','X','T','1'):selected.dxt==3?FOURCC('D','X','T','3'):FOURCC('D','X','T','5');
    uint8_t *raw=malloc(selected.raw_bytes);assert(raw&&fseeko(file,selected.raw_offset,SEEK_SET)==0&&fread(raw,1,selected.raw_bytes,file)==selected.raw_bytes);fclose(file);
    uint32_t pitch=((selected.width+3)/4)*(selected.dxt==1?8:16);
    const utx_entry *entry=void_texture_cache_find(format,selected.width,selected.height,3,raw,pitch);
    if(argc==4) { assert(!entry);puts("PASS: malformed cache rejected; ordinary DXT loader remains available");return 0; }
    assert(entry);
    for(int partial=0;partial<2;partial++) {
        d3d_texture *texture=d3d_texture_new(NULL,D3DRTYPE_TEXTURE,selected.width,selected.height,1,3,0,format,D3DPOOL_DEFAULT);assert(texture);
        d3d_texture_upload_level(texture,0,0,raw,pitch);
        assert(texture->cache_active&&texture->native);uint8_t pixel[4];sample(texture,0,pixel);
        uint8_t *changed=malloc((size_t)utx_level_size(format,selected.width,selected.height));assert(changed);
        size_t bytes=(size_t)utx_level_size(format,selected.width,selected.height);memcpy(changed,raw,bytes);
        for(size_t b=0;b<bytes;b+=(selected.dxt==1?8:16)) {
            uint8_t *color=changed+b+(selected.dxt==1?0:8);color[0]=color[2]=0; color[1]=color[3]=248;memset(color+4,0,4);
        }
        d3d_texture_upload_rect(texture,0,0,0,0,partial?4:selected.width,partial?4:selected.height,changed,pitch);
        assert(!texture->cache_active&&!texture->native&&texture->cache_disabled);sample(texture,0,pixel);assert(pixel[0]>245&&pixel[1]<10&&pixel[2]<10);
        const uint8_t *mip=raw;for(unsigned l=0;l<2;l++) {
            uint32_t a=selected.width>>l,b=selected.height>>l;mip+=utx_level_size(format,a?a:1,b?b:1);
        }
        uint32_t w=selected.width>>2,h=selected.height>>2;
        float *expected=malloc((size_t)w*h*4*sizeof *expected);assert(expected);
        d3d_format_decode(texture->fmt,mip,w,h,d3d_format_pitch(texture->fmt,w),expected);
        sample(texture,2,pixel);for(unsigned c=0;c<3;c++)assert(abs((int)pixel[c]-(int)lrintf(expected[c]*255))<=2);
        glDeleteTextures(1,&texture->gl);free(texture->surfaces);free(texture);free(changed);free(expected);
    }
    size_t bytes=(size_t)utx_level_size(format,selected.width,selected.height);
    uint8_t *changed=malloc(bytes);assert(changed);memcpy(changed,raw,bytes);
    for(size_t b=0;b<bytes;b+=(selected.dxt==1?8:16)) {
        uint8_t *color=changed+b+(selected.dxt==1?0:8);color[0]=color[2]=0;color[1]=color[3]=248;memset(color+4,0,4);
    }
    for(int miss_first=0;miss_first<2;miss_first++) {
        d3d_texture *cube=d3d_texture_new(NULL,D3DRTYPE_CUBETEXTURE,selected.width,selected.height,1,3,0,format,D3DPOOL_DEFAULT);assert(cube);
        for(unsigned face=0;face<6;face++)d3d_texture_upload_level(cube,face,0,face==0&&miss_first?changed:raw,pitch);
        assert(miss_first?cube->cache_disabled&&!cube->cache_active:cube->cache_active);
        uint8_t pixel[4];
        if(miss_first) {
            sample_cube(cube,0,0,pixel);assert(pixel[0]>245&&pixel[1]<10&&pixel[2]<10);
        } else {
            d3d_texture_upload_rect(cube,2,0,0,0,4,4,changed,pitch);
            assert(!cube->cache_active&&cube->cache_disabled);
            const uint8_t *mip=raw+utx_level_size(format,selected.width,selected.height)+utx_level_size(format,selected.width/2,selected.height/2);
            uint32_t w=selected.width/4,h=selected.height/4;
            float *expected=malloc((size_t)w*h*4*sizeof *expected);assert(expected);
            d3d_format_decode(cube->fmt,mip,w,h,d3d_format_pitch(cube->fmt,w),expected);
            sample_cube(cube,5,2,pixel);
            for(unsigned c=0;c<3;c++)assert(abs((int)pixel[c]-(int)lrintf(expected[((h/2)*w+w/2)*4+c]*255))<=2);
            free(expected);
        }
        glDeleteTextures(1,&cube->gl);free(cube->surfaces);free(cube);
    }
    free(changed);free(raw);puts("PASS: real GLES converted upload, full/partial mod writes decode DXT, lower mips and cube faces retained, first-face cache miss cannot discard earlier uploads");
    return 0;
}
