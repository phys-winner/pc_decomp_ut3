/* Real producers, snapshots, ordered pump and exact DirectInput reader body. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include "touch_host_win.h"
#else
#include <sys/mman.h>
#endif
#include "../android/host/input.c"
#include "dinput_overflow_reader.h"

static uint32_t ticks,posted_count;
static struct {uint32_t msg,key;} posted[2048];
void void_log(const char *s,...){(void)s;}
uint32_t void_tick_ms(void){return ++ticks;}
uint64_t void_monotonic_ns(void){return (uint64_t)++ticks*1000000;}
uint32_t void_window_main(void){return 1;}
void void_post_message(uint32_t hwnd,uint32_t msg,uint32_t wp,uint32_t lp)
{(void)hwnd;(void)lp;assert(posted_count<2048);posted[posted_count].msg=msg;posted[posted_count++].key=wp;}

static uint32_t read_di(int mouse,uint32_t count,uint32_t out,uint32_t peek)
{
    di_fixture.kind=mouse?DEV_MOUSE:DEV_KEYBOARD;di_fixture.acquired=1;
    di_args[0]=1;di_args[1]=20;di_args[2]=out;di_args[3]=0x1000;di_args[4]=peek;
    MEM32(0x1000)=count;return dev_GetDeviceData_impl();
}
static void same_prefix(const void_input_event *expected,int count,uint32_t out)
{
    assert(MEM32(0x1000)==(uint32_t)count);
    for(int i=0;i<count;++i){assert(!memcmp(GPTR(out+(uint32_t)i*20),expected+i,16));assert(!MEM32(out+(uint32_t)i*20+16));}
}
int main(void)
{
    (void)ui_begin;(void)ui_control;(void)ui_scroll;(void)text_submit;(void)text_delete;(void)touch_stick;(void)touch_weapon;
#ifdef _WIN32
    assert(VirtualAlloc((void *)(uintptr_t)VOID_GUEST_BASE,0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==(void *)(uintptr_t)VOID_GUEST_BASE);
#else
    assert(mmap((void *)(uintptr_t)VOID_GUEST_BASE,0x10000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)==(void *)(uintptr_t)VOID_GUEST_BASE);
#endif
    /* Original recovery (00A0DCDD): Poll fails until acquired, then needs no polling;
     * the window procedure's Unacquire must make the next Poll fail again. */
    di_fixture.kind=DEV_MOUSE;di_fixture.acquired=0;di_args[0]=1;
    assert(dev_Poll_impl()==DIERR_NOTACQUIRED);
    di_args[1]=20;di_args[2]=0;di_args[3]=0x1000;di_args[4]=0;MEM32(0x1000)=1;
    assert(dev_GetDeviceData_impl()==DIERR_NOTACQUIRED);
    assert(dev_Acquire_impl()==DI_OK&&dev_Acquire_impl()==DI_NOEFFECT&&dev_Poll_impl()==DI_NOEFFECT);
    assert(dev_Unacquire_impl()==DI_OK&&dev_Poll_impl()==DIERR_NOTACQUIRED);
    puts("PASS DirectInput Poll fails until Acquire and after Unacquire, as the original mouse recovery requires");
    void_input_cursor_visible(0);uint64_t context=touch_cursor_context;assert((context&3u)==2);
    for(unsigned i=0;i<128;++i){void_input_key('A',1);void_input_key('A',0);}
    assert(key_ring.count==256&&!key_ring.overflow);void_input_pump();assert(!pending_count);posted_count=0;
    for(unsigned i=0;i<128;++i)void_input_mouse_move(1,1);
    assert(mouse_ring.count==256&&!mouse_ring.overflow);void_input_pump();posted_count=0;
    void_input_event keys_before[256],mouse_before[256];
    assert(void_input_keyboard_events(keys_before,256,1)==256&&void_input_mouse_events(mouse_before,256,1)==256);
    uint8_t keys[256],buttons[8],pad[16];int32_t dx,dy,dz;
    void_input_keyboard_snapshot(keys);void_input_mouse_snapshot(&dx,&dy,&dz,buttons);
    /* Both DI histories are full, yet move/look/jump/duck/use/fire remain live. */
    assert(touch_submit(.5f,-.5f,2,3,31,context));assert(touch_held==31&&void_input_touch_pad(0,pad));
    assert(touch_submit(0,0,0,0,0,context));assert(!touch_held&&!touch_blocked);
    assert(key_ring.count==256&&mouse_ring.count==256&&key_ring.overflow&&mouse_ring.overflow);
    void_input_pump();assert(posted_count==11);
    unsigned use_down=0,use_up=0;
    for(unsigned i=0;i<posted_count;++i)if(posted[i].key=='E'){
        if(posted[i].msg==0x100){assert(!use_down&&!use_up);use_down=1;}
        if(posted[i].msg==0x101){assert(use_down&&!use_up);use_up=1;}
    }
    assert(use_down&&use_up);
    void_input_keyboard_snapshot(keys);void_input_mouse_snapshot(&dx,&dy,&dz,buttons);
    assert(keys[void_vk_to_dik('E')]&&keys[void_vk_to_dik('C')]&&keys[void_vk_to_dik(32)]&&buttons[0]&&buttons[1]&&dx==2&&dy==3);
    void_input_keyboard_snapshot(keys);void_input_mouse_snapshot(&dx,&dy,&dz,buttons);
    assert(!keys[void_vk_to_dik('E')]&&!keys[void_vk_to_dik('C')]&&!keys[void_vk_to_dik(32)]&&!buttons[0]&&!buttons[1]);
    /* Cancel must preserve physical ownership and unacknowledged physical overflow. */
    void_input_key('E',1);void_input_mouse_button(0,1);
    assert(touch_submit(0,0,0,0,17,context));void_input_touch_reset();void_input_touch_reset();
    assert(void_input_key_state('E')<0&&void_input_key_state(1)<0&&key_ring.overflow&&mouse_ring.overflow);
    void_input_key('E',0);void_input_mouse_button(0,0);void_input_pump();
    assert(read_di(0,UINT32_MAX,0,1)==DI_BUFFEROVERFLOW&&MEM32(0x1000)==256&&key_ring.count==256&&key_ring.overflow);
    assert(read_di(0,256,0x2000,1)==DI_BUFFEROVERFLOW);same_prefix(keys_before,256,0x2000);
    assert(read_di(0,0,0,1)==DI_BUFFEROVERFLOW&&key_ring.overflow);
    assert(read_di(0,17,0x2000,0)==DI_BUFFEROVERFLOW);same_prefix(keys_before,17,0x2000);
    assert(!key_ring.overflow&&key_ring.count==239&&mouse_ring.overflow);
    assert(read_di(0,239,0x2000,0)==DI_OK);same_prefix(keys_before+17,239,0x2000);assert(!key_ring.count);
    assert(read_di(1,256,0x2000,1)==DI_BUFFEROVERFLOW);same_prefix(mouse_before,256,0x2000);
    assert(read_di(1,0,0,0)==DI_BUFFEROVERFLOW&&!mouse_ring.overflow&&mouse_ring.count==256);
    assert(read_di(1,UINT32_MAX,0,1)==DI_OK&&MEM32(0x1000)==256&&mouse_ring.count==256);
    assert(read_di(1,UINT32_MAX,0,0)==DI_OK&&MEM32(0x1000)==256&&!mouse_ring.count);
    assert(read_di(1,0,0,0)==DI_OK&&MEM32(0x1000)==0);
    /* A fresh buffered short tap preserves the complete ordered down/up pair. */
    assert(touch_submit(0,0,0,0,16,touch_cursor_context));assert(touch_submit(0,0,0,0,0,touch_cursor_context));
    assert(read_di(0,2,0x2000,0)==DI_OK&&MEM32(0x1000)==2);
    assert(MEM32(0x2000)==void_vk_to_dik('E')&&MEM32(0x2004)==0x80&&MEM32(0x2014)==void_vk_to_dik('E')&&!MEM32(0x2018));
    assert(MEM32(0x200c)<MEM32(0x2020));void_input_pump();
    /* The actual ordered queue still enforces fail-neutral backpressure. */
    for(unsigned i=0;i<465;++i)pending[i]=(pending_t){0x100,'Z',0,0,0};pending_count=465;
    assert(!touch_submit(1,0,1,0,17,touch_cursor_context)&&!touch_held&&!touch_connected&&touch_blocked);
    assert(pending_count>=465&&pending_count<=512);
    for(unsigned i=0;i<465;++i)assert(pending[i].msg==0x100&&pending[i].wparam=='Z'&&!pending[i].touch);
    void_input_pump();assert(!pending_count);
    assert(touch_submit(0,0,0,0,0,touch_cursor_context)&&!touch_blocked);
    puts("PASS production DI overflow: full keyboard+mouse never blocks touch/analog/look/vehicle actions; ordered WM/short taps and physical owners survive; oldest prefix retained; overflow sticky/PEEK/NULL query/ack exact and independent; pending queue still fails neutral");
    return 0;
}
