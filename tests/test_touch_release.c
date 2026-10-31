/* Production touch release ledger and virtual weapon-picker pad, with the
 * original window-message order recorded as the game's queue receives it. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include "touch_host_win.h"
#endif
#include "../android/host/input.c"

enum { KEYDOWN=0x100, KEYUP=0x101, ESCAPE=27, SPACE=32, PAUSE_BIT=1u<<7, JUMP_BIT=1u<<2, TAUNT_BIT=1u<<26 };
typedef struct { uint32_t message,value; } posted_t;
static posted_t posted[4096];
static unsigned posted_count;
static int owned=1;
static uint32_t game_mode=1,game_epoch=1,game_vehicle;
static uint64_t ticks;

int void_machine_is_owned(void) { return owned; }
int void_touch_game_read(uint32_t out[4])
{
    if (!owned) return -1;
    out[0]=game_mode;out[1]=game_epoch;out[2]=game_vehicle;out[3]=0x110000;
    return 1;
}
/* Script details, look steering and the weapon mailbox as the script publishes them. */
static uint32_t detail_slots, detail_flags, mailbox[16], mailbox_count;
static int mailbox_busy, steer_ready;
static float steer_values[3];
static uint32_t steer_bits;
int void_touch_game_details(uint32_t out[2]) { out[0]=detail_slots;out[1]=detail_flags;return 1; }
int void_touch_weapon_request(uint32_t request)
{
    if (mailbox_busy) return 0;
    assert(mailbox_count<16);mailbox[mailbox_count++]=request;return 1;
}
int void_touch_steer(float out[3],uint32_t *flags)
{
    if (!steer_ready) return 0;
    memcpy(out,steer_values,sizeof steer_values);*flags=steer_bits;return 1;
}
static int16_t axis_at(const uint8_t *pad,unsigned offset) { int16_t v;memcpy(&v,pad+offset,2);return v; }
uint32_t void_tick_ms(void) { return (uint32_t)ticks; }
uint64_t void_monotonic_ns(void) { return ++ticks*1000000; }
uint32_t void_window_main(void) { return 1; }
void void_log(const char *format,...) { (void)format; }
void void_post_message(uint32_t window,uint32_t message,uint32_t value,uint32_t point)
{
    (void)point;
    assert(window==1 && posted_count<sizeof posted/sizeof *posted);
    if (message==KEYDOWN || message==KEYUP) posted[posted_count++]=(posted_t){message,value};
}
static unsigned count(uint32_t message,uint32_t vk)
{
    unsigned total=0;
    for (unsigned i=0;i<posted_count;++i) total+=posted[i].message==message && posted[i].value==vk;
    return total;
}
static uint64_t context(void) { return __atomic_load_n(&touch_cursor_context,__ATOMIC_ACQUIRE); }
static void scene(uint32_t mode) { game_mode=mode;++game_epoch;void_input_pump(); }

int main(void)
{
    uint8_t pad[16];int16_t value;uint16_t buttons;
    void_input_pump();
    assert((context()&3u)==2u && touch_game_active);

    /* Pause: Escape's DOWN and its own release open the original menu. The
     * scene change and Java's cancel must not repeat the release into it. */
    assert(touch_submit(0,0,0,0,PAUSE_BIT,context()) && touch_submit(0,0,0,0,0,context()));
    void_input_pump();
    assert(count(KEYDOWN,ESCAPE)==1 && count(KEYUP,ESCAPE)==1);
    scene(2);void_input_touch_reset();void_input_pump();void_input_touch_reset();void_input_pump();
    assert((context()&3u)==3u);
    assert(count(KEYDOWN,ESCAPE)==1 && count(KEYUP,ESCAPE)==1);
    scene(1);void_input_touch_reset();void_input_pump();
    assert(count(KEYUP,ESCAPE)==1);

    /* A held key whose DOWN reached the queue is released exactly once. */
    assert(touch_submit(0,0,0,0,JUMP_BIT,context()));void_input_pump();
    assert(count(KEYDOWN,SPACE)==1 && count(KEYUP,SPACE)==0);
    scene(2);void_input_touch_reset();void_input_pump();void_input_touch_reset();void_input_pump();
    assert(count(KEYDOWN,SPACE)==1 && count(KEYUP,SPACE)==1);

    /* A DOWN cancelled before the queue received it produces neither edge. */
    scene(1);
    assert(touch_submit(0,0,0,0,JUMP_BIT,context()));void_input_touch_reset();void_input_pump();
    assert(count(KEYDOWN,SPACE)==1 && count(KEYUP,SPACE)==1);
    assert(void_input_key_state(SPACE)>=0);

    /* Physical Escape (Android Back) stays with its owner across touch cancels. */
    void_input_key(ESCAPE,1);void_input_pump();
    void_input_touch_reset();void_input_pump();
    assert(count(KEYDOWN,ESCAPE)==2 && count(KEYUP,ESCAPE)==1);
    void_input_key(ESCAPE,0);void_input_pump();
    assert(count(KEYUP,ESCAPE)==2);
    void_input_touch_reset();void_input_pump();assert(count(KEYUP,ESCAPE)==2);

    /* New taunt key bit and the 28-bit limit. */
    assert(touch_submit(0,0,0,0,TAUNT_BIT,context()) && touch_submit(0,0,0,0,0,context()));
    void_input_pump();assert(count(KEYDOWN,'J')==1 && count(KEYUP,'J')==1);
    assert(!touch_submit(0,0,0,0,1u<<28,context()));

    /* Weapon picker: left shoulder, then a direction; release clears both at once. */
    assert(!touch_stick(0,1,context()) && touch_stick(0,0,context()));
    assert(touch_submit(0,0,0,0,TOUCH_PICKER,context()) && void_input_touch_pad(0,pad));
    memcpy(&buttons,pad+4,2);assert(buttons==0x0100);
    memcpy(&value,pad+12,2);assert(!value);
    assert(touch_stick(0,1,context()) && void_input_touch_pad(0,pad));
    memcpy(&value,pad+12,2);assert(!value);
    memcpy(&value,pad+14,2);assert(value==32767);
    assert(touch_stick(-.6f,-.8f,context()) && void_input_touch_pad(0,pad));
    memcpy(&value,pad+12,2);assert(value<-22000 && value>-23000);
    memcpy(&value,pad+14,2);assert(value<-27000 && value>-28000);
    uint32_t packet;memcpy(&packet,pad,4);
    assert(touch_submit(0,0,0,0,0,context()) && void_input_touch_pad(0,pad));
    uint32_t next;memcpy(&next,pad,4);assert(next!=packet);
    for (unsigned i=4;i<16;++i) assert(!pad[i]);
    assert(!touch_stick(1,0,context()));
    assert(touch_submit(0,0,0,0,TOUCH_PICKER,context()) && touch_stick(1,0,context()));
    assert(!touch_stick(NAN,0,context()));
    assert(void_input_touch_pad(0,pad));for (unsigned i=4;i<16;++i) assert(!pad[i]);
    assert(touch_submit(0,0,0,0,TOUCH_PICKER,context()) && touch_stick(1,0,context()));
    scene(2);memset(pad,0xA5,sizeof pad);assert(!void_input_touch_pad(0,pad));
    assert(!touch_stick(1,0,context()) && touch_stick(0,0,context()));
    scene(1);assert(void_input_touch_pad(0,pad));for (unsigned i=4;i<16;++i) assert(!pad[i]);
    /* Console look steering: the view steers, the stick's deflection is the throttle. */
    steer_ready=1;steer_values[0]=.5f;steer_values[1]=-.3f;steer_values[2]=1.1f;steer_bits=8|16;
    assert(touch_submit(0,1,0,0,0,context()) && void_input_touch_pad(0,pad));
    assert(axis_at(pad,8)==-19660 && axis_at(pad,10)==32767);          /* steer toward the view, full ahead */
    assert(touch_submit(.6f,0,0,0,0,context()) && void_input_touch_pad(0,pad));
    assert(axis_at(pad,8)==-19660 && axis_at(pad,10)==23854);          /* sideways deflection still drives */
    assert(touch_submit(0,-1,0,0,0,context()) && void_input_touch_pad(0,pad));
    assert(axis_at(pad,8)==-19660 && axis_at(pad,10)==-32767);         /* pulled back: reverse */
    steer_bits=8|16|32;assert(void_input_touch_pad(0,pad) && axis_at(pad,8)==19660); /* reversing flips steering */
    steer_bits=8;assert(touch_submit(.6f,.5f,0,0,0,context()) && void_input_touch_pad(0,pad));
    assert(axis_at(pad,8)==-19660 && axis_at(pad,10)==19660);          /* stick throttle, no deflection rule */
    assert(touch_submit(0,0,0,0,0,context()) && void_input_touch_pad(0,pad));
    assert(!axis_at(pad,8) && !axis_at(pad,10));                       /* a resting stick never drives */
    uint32_t before;memcpy(&before,pad,4);
    steer_values[0]=-.25f;assert(touch_submit(0,1,0,0,0,context()) && void_input_touch_pad(0,pad));
    memcpy(&packet,pad,4);assert(packet!=before && axis_at(pad,8)==13107);
    steer_ready=0;assert(void_input_touch_pad(0,pad) && !axis_at(pad,8) && axis_at(pad,10)==32767);
    assert(touch_submit(0,0,0,0,0,context()));
    /* Weapon bar swipe: previews coalesce, a switch is never replaced, a cancel follows a reset. */
    assert(!touch_weapon(0,1,context()) && !touch_weapon(1,10,context()) && !touch_weapon(1,-1,context()));
    assert(touch_weapon(1,4,context()));void_input_pump();assert(mailbox_count==1 && mailbox[0]==0x105);
    assert(touch_weapon(1,5,context()) && touch_weapon(1,6,context()) && touch_weapon(2,6,context()));
    assert(touch_weapon(1,2,context()));void_input_pump();assert(mailbox_count==2 && mailbox[1]==0x207);
    mailbox_busy=1;assert(touch_weapon(1,3,context()));void_input_pump();assert(mailbox_count==2);
    mailbox_busy=0;void_input_pump();assert(mailbox_count==3 && mailbox[2]==0x104);
    void_input_touch_reset();void_input_pump();assert(mailbox_count==4 && mailbox[3]==0x300);
    void_input_touch_reset();void_input_pump();assert(mailbox_count==4);
    scene(2);assert(!touch_weapon(1,3,context()));scene(1);void_input_pump();assert(mailbox_count==4);
    detail_slots=1|2|(1<<10)|(1<<14);detail_flags=1|4;void_input_pump();
    assert(touch_weapon_slots==detail_slots && touch_vehicle_flags==5);
    puts("PASS console look steering pad: view steering, deflection throttle, reverse region and flip, plain throttle, resting stick, packet change, publisher loss");
    puts("PASS weapon swipe mailbox: invalid phase/slot, preview coalescing, switch kept over later preview, busy retry, reset cancel once, UI rejection, details without reset");
    puts("PASS touch release ledger: pause Escape keeps its single DOWN/UP across scene and Java cancels; held DOWN released once; undelivered DOWN silent; physical Escape owned by Back; taunt bit; 28-bit mask");
    puts("PASS weapon picker pad: shoulder before direction, right stick only while held, release neutralizes buttons and stick in one packet, UI/stale/invalid neutral");
    return 0;
}
