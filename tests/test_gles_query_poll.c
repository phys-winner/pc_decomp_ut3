/* Pending status/guest output contract and actual visible/hidden GLES queries. */
#include "../android/host/d3d9_internal.h"
#include <EGL/egl.h>
#include <assert.h>
#include <errno.h>
#include <sys/mman.h>
#include <time.h>

static int mock, ready, visible, interrupt_once;
static unsigned sleeps, enters, leaves;
static void query_value(GLuint name, GLenum param, GLuint *value)
{
    if (!mock) { glGetQueryObjectuiv(name,param,value); return; }
    assert(name && (param==GL_QUERY_RESULT_AVAILABLE || (param==GL_QUERY_RESULT && ready)));
    *value=param==GL_QUERY_RESULT_AVAILABLE ? ready : visible;
}
static int wait_slice(const struct timespec *delay, struct timespec *remaining)
{
    assert(delay->tv_sec==0 && delay->tv_nsec>0 && delay->tv_nsec<=50000);
    ++sleeps;
    if(interrupt_once){interrupt_once=0;remaining->tv_sec=0;remaining->tv_nsec=10000;errno=EINTR;return -1;}
    return 0;
}
#define glGetQueryObjectuiv query_value
#define nanosleep wait_slice
#include "../android/host/d3d9_resource.c"

uint32_t g_eax,g_ecx,g_edx,g_esp;
static d3d_query_rt test_query_rt;
#define test_query (test_query_rt.q)
void *void_com_host(uint32_t guest){assert(guest==0x80);return &test_query;}
/* An end as Issue records it: the game's serial and state, the render thread's view of it. */
static void ended(void){++test_query_rt.serial;test_query_rt.game_state=2;test_query_rt.rt_serial=test_query_rt.serial;test_query.gl=1;test_query.ended=QUERY_PENDING;}
void void_blocking_begin(void){++enters;}
void void_blocking_end(void){++leaves;}
void void_log(const char *format,...){(void)format;}

static uint32_t get_data(uint32_t flags)
{
    MEM32(g_esp+4)=0x80;MEM32(g_esp+8)=0x200;MEM32(g_esp+12)=4;MEM32(g_esp+16)=flags;
    return qry_GetData_impl();
}
static GLuint shader(GLenum kind,const char *text)
{
    GLuint s=glCreateShader(kind);glShaderSource(s,1,&text,NULL);glCompileShader(s);
    GLint good=0;glGetShaderiv(s,GL_COMPILE_STATUS,&good);assert(good);return s;
}
int main(void)
{
    assert(mmap((void*)VOID_GUEST_BASE,16384,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)==(void*)VOID_GUEST_BASE);
    g_esp=0x1000;
    EGLDisplay display=eglGetDisplay(EGL_DEFAULT_DISPLAY);assert(eglInitialize(display,NULL,NULL));
    EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_DEPTH_SIZE,16,EGL_NONE};
    EGLConfig cfg;EGLint count;assert(eglChooseConfig(display,attrs,&cfg,1,&count)&&count);
    EGLint context_attrs[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE},surface_attrs[]={EGL_WIDTH,32,EGL_HEIGHT,32,EGL_NONE};
    EGLContext context=eglCreateContext(display,cfg,EGL_NO_CONTEXT,context_attrs);
    EGLSurface surface=eglCreatePbufferSurface(display,cfg,surface_attrs);assert(eglMakeCurrent(display,surface,surface,context));
    d3d_device device={0};device.st.viewport[2]=device.st.viewport[3]=32;
    test_query.dev=&device;test_query.type=9;ended();
    mock=1;query_names[0]=1;MEM32(0x200)=0xa5a55a5a;
    for(unsigned i=0;i<8;++i)assert(get_data(1)==S_FALSE_D3D&&!sleeps&&MEM32(0x200)==0xa5a55a5a);
    interrupt_once=1;assert(get_data(1)==S_FALSE_D3D&&sleeps==2&&enters==1&&leaves==1&&MEM32(0x200)==0xa5a55a5a);
    assert(get_data(0)==S_FALSE_D3D&&sleeps==2);
    for(unsigned i=0;i<8;++i)assert(get_data(1)==S_FALSE_D3D&&sleeps==2);
    ready=1;visible=0;assert(get_data(1)==D3D_OK&&MEM32(0x200)==0&&query_pending_spins==0);
    ended();visible=1;
    assert(get_data(1)==D3D_OK&&MEM32(0x200)==1024&&query_pending_spins==0);
    assert(sleeps==2&&enters==leaves);
    mock=0;query_free_count=0;
    GLuint vs=shader(GL_VERTEX_SHADER,"#version 300 es\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0.,1.);}");
    GLuint fs=shader(GL_FRAGMENT_SHADER,"#version 300 es\nprecision mediump float;out vec4 c;void main(){c=vec4(1.);}");
    GLuint program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,fs);glLinkProgram(program);
    GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);assert(linked);glUseProgram(program);glViewport(0,0,32,32);
    glEnable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);
    for(int hidden=0;hidden<2;++hidden){
        GLuint q;glGenQueries(1,&q);query_names[0]=q;query_owners[0]=&test_query;
        ended();
        glDepthFunc(hidden?GL_NEVER:GL_ALWAYS);
        glBeginQuery(GL_ANY_SAMPLES_PASSED_CONSERVATIVE,q);glDrawArrays(GL_TRIANGLES,0,3);glEndQuery(GL_ANY_SAMPLES_PASSED_CONSERVATIVE);
        MEM32(0x200)=0xa5a55a5a;uint32_t result=S_FALSE_D3D;
        for(unsigned polls=0;polls<100000&&result==S_FALSE_D3D;++polls)result=get_data(1);
        assert(result==D3D_OK&&MEM32(0x200)==(hidden?0:1024));assert(glGetError()==GL_NO_ERROR);glDeleteQueries(1,&q);
    }
    puts("PASS GLES query polling: eight fast probes, bounded 50us wait/EINTR, balanced VM ownership, non-FLUSH never waits, S_FALSE leaves output untouched, actual GPU visible/hidden results preserved");
    return 0;
}
