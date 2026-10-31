/* Preserve the original list formatter and its resolved font/bindings. */
#include "host.h"
#include <errno.h>

extern void __real_sub_00F75DB0(void);
extern int void_touch_ui_list_scale(uint32_t string,float *scale);

static int list_format_ready;

static int list_format_pinned(void)
{
    static const uint8_t entry[]={0x6A,0xFF,0x68,0xC5,0x9A,0x69,0x01,0x64,0xA1};
    static const uint8_t policy[]={0x0F,0xB6,0x87,0xBC,0,0,0};
    if(list_format_ready)return list_format_ready>0;
    if(!void_vm_readable(0x00F75DB0u,sizeof entry)||!void_vm_readable(0x00F75E87u,sizeof policy))return 0;
    list_format_ready=!memcmp(GPTR(0x00F75DB0u),entry,sizeof entry)&&
        !memcmp(GPTR(0x00F75E87u),policy,sizeof policy) ? 1 : -1;
    return list_format_ready>0;
}

void __wrap_sub_00F75DB0(void)
{
    int saved_errno=errno;
    uint32_t string=g_ecx,bounds,flags;
    float factor,baseline[2],scaled;
    uint8_t policy[8];
    int entered=0;
    if(void_touch_ui_list_scale(string,&factor)&&list_format_pinned()&&
       string<=UINT32_MAX-0xE8u&&g_esp<=UINT32_MAX-8&&void_vm_readable(g_esp+4,4)) {
        memcpy(&bounds,GPTR(g_esp+4),4);
        if(bounds&&bounds<=UINT32_MAX-0x18u&&void_vm_readable(bounds+0x10,8)&&
           void_vm_readable(string+0xB8,8)&&void_vm_readable(string+0xE4,4)&&
           void_vm_writable(string+0xB8,8)&&void_vm_writable(string+0xDC,8)&&void_vm_writable(string+0xE4,4)) {
            memcpy(baseline,GPTR(bounds+0x10),8);memcpy(policy,GPTR(string+0xB8),8);
            scaled=baseline[0]*factor;
            if(!policy[4]&&isfinite(baseline[0])&&baseline[0]>0&&baseline[0]==baseline[1]&&
               isfinite(scaled)&&scaled>0) {
                memcpy(&flags,GPTR(string+0xE4),4);
                uint8_t normal[8];memcpy(normal,policy,8);memcpy(normal,&scaled,4);normal[4]=1;
                float scale[2]={scaled,scaled};uint32_t adjusted=flags|1u;
                memcpy(GPTR(string+0xB8),normal,8);memcpy(GPTR(string+0xDC),scale,8);
                memcpy(GPTR(string+0xE4),&adjusted,4);entered=1;
            }
        }
    }
    errno=saved_errno;
    __real_sub_00F75DB0();
    if(entered) {
        saved_errno=errno;
        memcpy(GPTR(string+0xB8),policy,8);memcpy(GPTR(string+0xE4),&flags,4);
        errno=saved_errno;
    }
}
