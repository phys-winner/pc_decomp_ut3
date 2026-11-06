/* Pending status/guest output contract and actual visible/hidden GLES queries. */
#include "../android/host/d3d9_internal.h"
#include <EGL/egl.h>
#include <assert.h>
#include <errno.h>
#include <sys/mman.h>
#include <time.h>

static int mock, ready, visible, interrupt_once;
static unsigned sleeps, enters, leaves, flushes;
static void query_flush(void) { ++flushes; glFlush(); }
static void query_value(GLuint name, GLenum param, GLuint *value)
{
    if (!mock) { glGetQueryObjectuiv(name,param,value); return; }
    assert(name && (param==GL_QUERY_RESULT_AVAILABLE || (param==GL_QUERY_RESULT && ready)));
    *value=param==GL_QUERY_RESULT_AVAILABLE ? ready : visible;
}
static int wait_slice(const struct timespec *delay, struct timespec *remaining)
{
    if (!mock) return nanosleep(delay, remaining);
    assert(delay->tv_sec==0 && delay->tv_nsec>0 && delay->tv_nsec<=50000);
    ++sleeps;
    if(interrupt_once){interrupt_once=0;remaining->tv_sec=0;remaining->tv_nsec=10000;errno=EINTR;return -1;}
    return 0;
}
#define glGetQueryObjectuiv query_value
#define glFlush query_flush
#define nanosleep wait_slice
#include "../android/host/d3d9_resource.c"

uint32_t g_eax,g_ecx,g_edx,g_esp;
static d3d_query_rt test_query_rt;
#define test_query (test_query_rt.q)
void *void_com_host(uint32_t guest){assert(guest==0x80);return &test_query;}
/* An end as Issue records it: the game's serial and state, the render thread's view of it. */
static void ended(void){++test_query_rt.serial;test_query_rt.game_state=2;test_query_rt.rt_serial=test_query_rt.serial;test_query.gl=1;test_query.ended=QUERY_PENDING;watch_push(&test_query);}
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
    assert(query_pool_size("Adreno (TM) 618")==4096);
    assert(query_pool_size("PowerVR DXT-48-1536")==1024);
    assert(query_pool_size("Mali-G720")==1024&&query_pool_size(NULL)==1024);
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
    assert(flushes==1);
    interrupt_once=1;assert(get_data(1)==S_FALSE_D3D&&sleeps==2&&enters==1&&leaves==1&&MEM32(0x200)==0xa5a55a5a);
    assert(get_data(0)==S_FALSE_D3D&&sleeps==2);
    for(unsigned i=0;i<8;++i)assert(get_data(1)==S_FALSE_D3D&&sleeps==2);
    ready=1;visible=0;assert(get_data(1)==D3D_OK&&MEM32(0x200)==0&&query_pending_spins==0);
    assert(flushes==1);
    ended();visible=1;
    assert(get_data(1)==D3D_OK&&MEM32(0x200)==1024&&query_pending_spins==0);
    assert(flushes==2);
    ended();ready=0;
    assert(get_data(0)==S_FALSE_D3D&&flushes==2);
    assert(get_data(1)==S_FALSE_D3D&&flushes==3);
    ready=1;assert(get_data(1)==D3D_OK&&flushes==3);
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
    query_count=query_free_count=0;
    uint32_t limit=query_pool_size((const char *)glGetString(GL_RENDERER));
    d3d_query_rt *queries=calloc(limit,sizeof *queries);assert(queries);
    for(unsigned pass=0;pass<3;++pass){
        for(uint32_t i=0;i<limit;++i){
            d3d_query *q=&queries[i].q;q->type=9;q->dev=&device;
            queries[i].rt_serial=pass+1;
            q->gl=query_take(q);assert(q->gl);
            glDepthFunc((i+pass)&1?GL_NEVER:GL_ALWAYS);
            glBeginQuery(GL_ANY_SAMPLES_PASSED_CONSERVATIVE,query_names[q->gl-1]);
            glDrawArrays(GL_TRIANGLES,0,3);glEndQuery(GL_ANY_SAMPLES_PASSED_CONSERVATIVE);
            q->ended=QUERY_PENDING;
        }
        glFinish();
        for(uint32_t i=0;i<limit;++i){
            uint32_t at=(i*1327u+17u)%limit;
            assert(query_collect(&queries[at].q));
            assert(queries[at].q.ended==(((at+pass)&1)?QUERY_HIDDEN:QUERY_VISIBLE));
        }
        assert(query_free_count==limit&&glGetError()==GL_NO_ERROR);
    }
    printf("PASS query pool: %u outstanding queries, three randomized visible/hidden passes, all slots reused\n",limit);
    glDeleteQueries((GLsizei)query_count,query_names);free(queries);
    puts("PASS GLES query polling: one flush per issued end, non-FLUSH to FLUSH promotion, eight fast probes, bounded 50us wait/EINTR, balanced VM ownership, S_FALSE leaves output untouched, actual GPU visible/hidden results preserved");
    return 0;
}
