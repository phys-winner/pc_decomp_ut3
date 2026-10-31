/* Standalone production touch/input assertions; no renderer or guest thread. */
#include "../android/host/input.c"
#include <assert.h>
#include <stdio.h>

static uint32_t ticks;
static int published;
static uint32_t publication[4];
int void_touch_game_read(uint32_t out[4])
{ if(!published)return 0;memcpy(out,publication,sizeof publication);return 1; }
void void_log(const char *format,...) { (void)format; }
uint32_t void_tick_ms(void) { return ++ticks; }
uint64_t void_monotonic_ns(void) { return (uint64_t)++ticks * 1000000; }
uint32_t void_window_main(void) { return 0; }
void void_post_message(uint32_t hwnd,uint32_t msg,uint32_t wp,uint32_t lp)
{ (void)hwnd;(void)msg;(void)wp;(void)lp; }

int main(void)
{
    uint8_t pad[16],buttons[8],keys[256];int32_t dx,dy,dz;
    memset(pad,0xA5,16);assert(!void_input_touch_pad(0,pad));
    for(int i=0;i<16;++i)assert(pad[i]==0xA5);
    void_input_touch(0,0,0,0,1);void_input_touch(0,0,0,0,0);
    assert(!void_input_touch_pad(0,pad)); /* Unknown cursor clicks do not invent a pad. */
    void_input_touch_reset();
    void_input_touch(.5f,.5f,0,0,0);assert(void_input_touch_pad(0,pad));
    int16_t x,y;memcpy(&x,pad+8,2);memcpy(&y,pad+10,2);
    assert(x==y&&x>19000&&x<20000);assert(!void_input_touch_pad(1,pad));
    void_input_touch(0,0,0,0,0);assert(void_input_touch_pad(0,pad));
    memcpy(&x,pad+8,2);memcpy(&y,pad+10,2);assert(!x&&!y);
    void_input_touch_reset();memset(pad,0xA5,16);assert(!void_input_touch_pad(0,pad));
    for(int i=0;i<16;++i)assert(pad[i]==0xA5);
    for(int i=0;i<4;++i)void_input_touch(0,0,.25f,-.25f,0);
    void_input_mouse_snapshot(&dx,&dy,&dz,buttons);assert(dx==1&&dy==-1&&!dz);
    void_input_touch(0,0,0,0,16);void_input_touch(0,0,0,0,0);
    void_input_touch(0,0,0,0,16);void_input_touch(0,0,0,0,0);
    void_input_keyboard_snapshot(keys);assert(keys[void_vk_to_dik('E')]);
    void_input_keyboard_snapshot(keys);assert(!keys[void_vk_to_dik('E')]);
    void_input_event events[256];int n=void_input_keyboard_events(events,256,0),edges=0;
    for(int i=0;i<n;i++)if(events[i].ofs==void_vk_to_dik('E'))++edges;
    assert(edges==4);
    void_input_key('E',1);void_input_mouse_button(0,1);
    void_input_touch(0,0,0,0,17);void_input_touch_reset();void_input_touch_reset();
    assert(void_input_key_state('E')<0&&void_input_key_state(1)<0);
    void_input_mouse_snapshot(&dx,&dy,&dz,buttons);assert(buttons[0]);
    void_input_key('E',0);void_input_mouse_button(0,0);
    void_input_keyboard_snapshot(keys); /* Consume the deliberately preserved physical press latch. */
    n=void_input_keyboard_events(events,256,0);
    assert(n>=2); /* The physical DOWN/UP pair must survive touch cancellation too. */
    void_input_touch(0,0,0,0,17);void_input_touch_reset();void_input_touch_reset();
    void_input_keyboard_snapshot(keys);void_input_mouse_snapshot(&dx,&dy,&dz,buttons);
    assert(!keys[void_vk_to_dik('E')]&&!buttons[0]);
    n=void_input_keyboard_events(events,256,0);
    for(int i=0;i<n;i++)assert(events[i].data==0);
    void_input_touch(0,0,0,0,32);void_input_touch(0,0,0,0,0);
    void_input_mouse_snapshot(&dx,&dy,&dz,buttons);assert(dz==120);
    void_input_touch_reset();void_input_mouse_snapshot(&dx,&dy,&dz,buttons);assert(dz==0);
    void_input_touch(1,0,100,100,17);void_input_touch(NAN,0,0,0,0);
    void_input_keyboard_snapshot(keys);void_input_mouse_snapshot(&dx,&dy,&dz,buttons);
    assert(!dx&&!dy&&!keys[void_vk_to_dik('E')]&&!buttons[0]);
    for(int reset=0;reset<260;reset++) {
        void_input_touch(0,0,0,0,16);void_input_touch_reset();
        n=void_input_keyboard_events(events,256,0);
        for(int i=0;i<n;i++)assert(events[i].data==0);
    }
    assert(!touch_cursor_context);
    void_input_touch(1,0,1,0,17);void_input_cursor_visible(1);
    assert((touch_cursor_context&3)==3&&!touch_held&&!touch_stick_x);
    uint64_t context=touch_cursor_context;void_input_cursor_visible(1);assert(context==touch_cursor_context);
    assert(touch_submit(0,0,0,0,1,context));assert(!void_input_touch_pad(0,pad));
    assert(touch_submit(0,0,0,0,0,context));assert(!void_input_touch_pad(0,pad));
    void_input_cursor_visible(0);assert((touch_cursor_context&3)==2&&(touch_cursor_context>>2)==2);
    assert(!touch_submit(1,0,1,0,17,context));
    assert(!touch_held&&!touch_stick_x);
    assert(touch_submit(0,0,0,0,0,touch_cursor_context));
    void_input_touch(0,0,0,0,17);
    void_input_key('E',1);void_input_key('E',0);
    void_input_mouse_button(0,1);void_input_mouse_button(0,0);
    void_input_touch_reset();
    void_input_keyboard_snapshot(keys);void_input_mouse_snapshot(&dx,&dy,&dz,buttons);
    assert(keys[void_vk_to_dik('E')]&&buttons[0]);
    void_input_keyboard_snapshot(keys);void_input_mouse_snapshot(&dx,&dy,&dz,buttons);
    assert(!keys[void_vk_to_dik('E')]&&!buttons[0]);
    /* A real publisher context reaches this same production observation path;
     * no contact or guest reconnect-flag mutation is required for neutral. */
    published=1;publication[0]=1;publication[1]=1;publication[2]=0;publication[3]=0x1234;
    touch_observe_game();assert(touch_game_active&&(touch_cursor_context&3u)==2u&&!touch_connected);
    int old_pending=pending_count,old_keys=key_ring.count,old_mouse=mouse_ring.count;
    assert(void_input_touch_pad(0,pad));
    for(unsigned i=4;i<16;++i)assert(!pad[i]);
    assert(!touch_connected&&pending_count==old_pending&&key_ring.count==old_keys&&mouse_ring.count==old_mouse);
    for(unsigned slot=1;slot<4;++slot) {
        memset(pad,0xA5,sizeof pad);assert(!void_input_touch_pad(slot,pad));
        for(unsigned i=0;i<16;++i)assert(pad[i]==0xA5);
    }
    assert(touch_submit(1,1,0,0,0,touch_cursor_context)&&void_input_touch_pad(0,pad));
    memcpy(&x,pad+8,2);memcpy(&y,pad+10,2);assert(x==y&&x>25000&&x<25100);
    void_input_key('E',1);void_input_mouse_button(0,1);void_input_touch_reset();
    assert(void_input_touch_pad(0,pad));for(unsigned i=4;i<16;++i)assert(!pad[i]);
    assert(void_input_key_state('E')<0&&void_input_key_state(1)<0);
    publication[0]=2;++publication[1];touch_observe_game();
    memset(pad,0xA5,sizeof pad);assert(!void_input_touch_pad(0,pad));
    for(unsigned i=0;i<16;++i)assert(pad[i]==0xA5);
    assert(void_input_key_state('E')<0&&void_input_key_state(1)<0);
    publication[0]=0;++publication[1];touch_observe_game();assert(!void_input_touch_pad(0,pad));
    publication[0]=1;++publication[1];publication[2]=2;touch_observe_game();
    assert(!touch_connected&&void_input_touch_pad(0,pad));for(unsigned i=4;i<16;++i)assert(!pad[i]);
    void_input_key('E',0);void_input_mouse_button(0,0);
    puts("PASS real production input: persistent analog/XInput16, fractional look, short/rapid edges, source ownership, repeated cancel/neutral release ledger, stale event removal, wheel reset, invalid-packet fail-neutral");
    puts("PASS eager neutral gameplay XInput0 before contact/after reset/vehicleepoch; UI/loading disconnect, physicalowners andslots1..3 preserved, getter leavesqueues untouched; legacyunknown unchanged");
    return 0;
}
