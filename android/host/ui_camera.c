/* Hor+ at the original unconstrained world projection, after camera/zoom logic. */
#include "host.h"
#include <errno.h>

extern void __real_sub_00486270(void);
extern void __real_sub_00C85710(void);
extern int void_machine_is_owned(void);

static int camera_projection_ready;
typedef struct { uint32_t stack,frame,player; } camera_call_scope;
static _Thread_local camera_call_scope camera_call;
static _Thread_local uint64_t camera_frame;

/* Same trusted frame boundary as HUD cleanup, including a nonlocal unwind. */
void void_ui_camera_frame_begin(void)
{
    if(!void_machine_is_owned())return;
    ++camera_frame;
    memset(&camera_call,0,sizeof camera_call);
}

void __wrap_sub_00C85710(void)
{
    int saved_errno=errno;
    camera_call_scope previous=camera_call;
    uint64_t frame=camera_frame;
    memset(&camera_call,0,sizeof camera_call);
    if(void_machine_is_owned()&&g_esp>=0x1E0u&&g_ecx) {
        camera_call.frame=g_esp-4;
        camera_call.stack=(camera_call.frame&~15u)-0x1D8u;
        camera_call.player=g_ecx;
    }
    errno=saved_errno;
    __real_sub_00C85710();
    saved_errno=errno;
    if(frame==camera_frame)camera_call=previous;
    else memset(&camera_call,0,sizeof camera_call);
    errno=saved_errno;
}

static int camera_projection_pinned(void)
{
    static const uint8_t entry[]={0xD9,0x44,0x24,0x04,0xF3,0x0F,0x10,0x54,0x24,0x10,0xD9,0xF2};
    static const uint8_t caller[]={0xE8,0x62,0x06,0x80,0xFF,0xB9,0x10,0,0,0};
    static const uint8_t frame[]={0x55,0x8B,0xEC,0x83,0xE4,0xF0,0x6A,0xFF,0x68,0x89,0xD1,0x66,0x01};
    static const uint8_t output[]={0x8D,0x8C,0x24,0x84,0x01,0,0};
    if(camera_projection_ready)return camera_projection_ready>0;
    if(!void_vm_readable(0x00486270u,sizeof entry)||!void_vm_readable(0x00C85C09u,sizeof caller)||
       !void_vm_readable(0x00C85710u,sizeof frame)||!void_vm_readable(0x00C85BF7u,sizeof output))return 0;
    camera_projection_ready=!memcmp(GPTR(0x00486270u),entry,sizeof entry)&&
        !memcmp(GPTR(0x00C85C09u),caller,sizeof caller)&&!memcmp(GPTR(0x00C85710u),frame,sizeof frame)&&
        !memcmp(GPTR(0x00C85BF7u),output,sizeof output) ? 1 : -1;
    return camera_projection_ready>0;
}

void __wrap_sub_00486270(void)
{
    int saved_errno=errno,entered=0;
    uint32_t stack=g_esp,return_site,original_half=0;
    if(void_machine_is_owned()&&camera_call.stack==stack&&camera_call.frame==g_ebp&&
       camera_call.player&&camera_call.player==g_ebx&&stack<=UINT32_MAX-0x1C8u&&
       void_vm_readable(stack,20)) {
        memcpy(&return_site,GPTR(stack),4);
        /* The other original caller is the constrained/cinematic camera. */
        if(return_site==RECOMP_RETADDR&&g_ecx==stack+0x188u&&
           camera_projection_pinned()&&void_vm_writable(stack+4,4)&&
           void_vm_writable(g_ecx,64)) {
            float args[4];memcpy(args,GPTR(stack+4),sizeof args);
            if(isfinite(args[0])&&args[0]>0&&args[0]<1.5707963267948966&&
               isfinite(args[1])&&args[1]>0&&isfinite(args[2])&&args[2]>0&&
               isfinite(args[3])&&args[3]>0) {
                double aspect=(double)args[1]/args[2];
                if(aspect>4.0/3.0) {
                    float half=(float)atan(tan((double)args[0])*aspect/(4.0/3.0));
                    if(isfinite(half)&&half>args[0]&&half<1.5707963267948966) {
                        memcpy(&original_half,GPTR(stack+4),4);
                        memcpy(GPTR(stack+4),&half,4);entered=1;
                    }
                }
            }
        }
    }
    errno=saved_errno;
    __real_sub_00486270();
    if(entered) {
        /* This pinned leaf has no calls/yields; restore only its input slot. */
        saved_errno=errno;
        memcpy(GPTR(stack+4),&original_half,4);
        errno=saved_errno;
    }
}
