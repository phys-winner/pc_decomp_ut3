/* Production clip/cursor/map/capture state and raw DirectInput separation. */
#include "../android/host/user32.c"
#include <assert.h>
#include <unistd.h>
void void_process_exit(uint32_t code, int detach) { (void)detach; _exit((int)code); }
static uint32_t capture_changes, changed_to;
static void test_wndproc(void)
{
    if (ARG(1) == 0x0215) { ++capture_changes; changed_to = ARG(3); }
    g_eax = 1; g_esp += 20;
}
static uint32_t api(const char *name, unsigned count, const uint32_t *args)
{
    const void_shim *shim = void_shim_find("user32.dll", name, 0); assert(shim);
    uint32_t before = g_esp, result = void_call_guest(void_thunk(shim->fn, name), count, args, 1);
    assert(g_esp == before); return result;
}
static void set_rect(uint32_t out, int l, int t, int r, int b)
{ MEM32(out)=l; MEM32(out+4)=t; MEM32(out+8)=r; MEM32(out+12)=b; }
static void check_rect(uint32_t out, int l, int t, int r, int b)
{ assert((int32_t)MEM32(out)==l && (int32_t)MEM32(out+4)==t && (int32_t)MEM32(out+8)==r && (int32_t)MEM32(out+12)==b); }
static void position(uint32_t out, int x, int y)
{ assert(api("GetCursorPos",1,&out)); assert((int32_t)MEM32(out)==x && (int32_t)MEM32(out+4)==y); }
static void cursor_info(uint32_t out, uint32_t flags, uint32_t cursor)
{
    MEM32(out)=20; assert(api("GetCursorInfo",1,&out));
    assert(!MEM32(out) && MEM32(out+4)==flags && MEM32(out+8)==cursor);
}
int main(void)
{
    assert(void_guest_reserve()); void_threads_init(); void_dispatch_init(); void_machine_enter();
    void_input_set_screen_size(800,600);
    uint32_t rect=void_galloc(16), guarded=void_galloc(28), out=guarded+4, zero=0;
    memset(GPTR(guarded),0xA5,28);
    assert(api("GetClipCursor",1,&out)); check_rect(out,0,0,800,600);
    set_rect(rect,10,20,110,120); assert(api("ClipCursor",1,&rect)); position(out,109,119);
    uint32_t xy[2]={(uint32_t)-10,500}; assert(api("SetCursorPos",2,xy)); position(out,10,119);
    void_input_mouse_warp(999,-5); position(out,109,20);
    int32_t dx,dy,dz; uint8_t buttons[8]; void_input_mouse_snapshot(&dx,&dy,&dz,buttons);
    void_input_mouse_move(500,-500); position(out,109,20);
    void_input_mouse_snapshot(&dx,&dy,&dz,buttons); assert(dx==500 && dy==-500 && !dz);
    void_input_mouse_move(-1000,1000); position(out,10,119);
    void_input_mouse_snapshot(&dx,&dy,&dz,buttons); assert(dx==-1000 && dy==1000);
    void_input_event events[8]; int events_count=void_input_mouse_events(events,8,0);
    assert(events_count==4 && events[0].data==500 && (int32_t)events[1].data==-500 && (int32_t)events[2].data==-1000 && events[3].data==1000);
    set_rect(rect,-20,-30,25,30); assert(api("ClipCursor",1,&rect));
    assert(api("GetClipCursor",1,&out)); check_rect(out,0,0,25,30);
    xy[0]=xy[1]=100; api("SetCursorPos",2,xy); position(out,24,29);
    set_rect(rect,50,50,50,50); assert(api("ClipCursor",1,&rect));
    assert(api("GetClipCursor",1,&out)); check_rect(out,50,50,50,50);
    xy[0]=xy[1]=49; api("SetCursorPos",2,xy); position(out,50,50);
    xy[0]=xy[1]=50; api("SetCursorPos",2,xy); position(out,49,49);
    set_rect(rect,80,80,70,70); assert(!api("ClipCursor",1,&rect) && void_get_last_error()==5);
    assert(api("GetClipCursor",1,&out)); check_rect(out,50,50,50,50);
    set_rect(rect,100,100,700,500); assert(api("ClipCursor",1,&rect));
    xy[0]=650;xy[1]=450;api("SetCursorPos",2,xy);
    void_input_set_screen_size(300,200); position(out,299,199);
    assert(api("GetClipCursor",1,&out));check_rect(out,100,100,300,200);
    void_input_set_screen_size(800,600);
    assert(api("GetClipCursor",1,&out));check_rect(out,100,100,700,500);
    assert(api("ClipCursor",1,&zero)); assert(api("GetClipCursor",1,&out));check_rect(out,0,0,800,600);
    xy[0]=INT32_MIN;xy[1]=INT32_MAX;api("SetCursorPos",2,xy);position(out,0,599);
    assert(!api("GetClipCursor",1,&zero) && void_get_last_error()==998);
    uint32_t invalid=0x1000;assert(!api("ClipCursor",1,&invalid) && void_get_last_error()==998);
    cursor_info(out,1,0x00020001);
    uint32_t hide=0;assert(api("ShowCursor",1,&hide)==0xFFFFFFFF);cursor_info(out,0,0x00020001);
    uint32_t show=1;assert(!api("ShowCursor",1,&show));
    uint32_t old=api("SetCursor",1,&zero);assert(old==0x00020001);cursor_info(out,0,0);
    api("SetCursor",1,&old);cursor_info(out,1,old);
    MEM32(out)=19;uint8_t unchanged[20];memcpy(unchanged,GPTR(out),20);
    assert(!api("GetCursorInfo",1,&out) && void_get_last_error()==87 && !memcmp(unchanged,GPTR(out),20));
    assert(MEM32(guarded)==0xA5A5A5A5 && MEM32(guarded+24)==0xA5A5A5A5);
    void_input_pump(); /* Drain earlier host events before creating test windows. */
    void_dispatch_add(0x7FFE9000,test_wndproc);
    uint32_t wc=void_galloc(40),name=void_gstrdup("UT3CursorTest");MEM32(wc+4)=0x7FFE9000;MEM32(wc+36)=name;
    assert(api("RegisterClassA",1,&wc));
    uint32_t create[12]={0,name,name,0,20,30,100,80,0,0,0,0};uint32_t first=api("CreateWindowExA",12,create);assert(first);
    create[4]=200;create[5]=300;uint32_t second=api("CreateWindowExA",12,create);assert(second);
    set_rect(rect,0,0,100,80);uint32_t map[4]={first,0,rect,2};
    void_set_last_error(0x1234);assert(api("MapWindowPoints",4,map)==((30u<<16)|20));assert(void_get_last_error()==0x1234);check_rect(rect,20,30,120,110);
    map[0]=0;map[1]=second;assert(api("MapWindowPoints",4,map)==((uint32_t)(uint16_t)-200|((uint32_t)(uint16_t)-300<<16)));check_rect(rect,-180,-270,-80,-190);
    map[0]=first;map[1]=first;map[2]=0;map[3]=0;void_set_last_error(0x1234);assert(!api("MapWindowPoints",4,map) && void_get_last_error()==0x1234);
    assert(!api("GetCapture",0,NULL));assert(!api("SetCapture",1,&first));assert(api("GetCapture",0,NULL)==first);
    assert(api("SetCapture",1,&second)==first && capture_changes==1 && changed_to==second);
    void_input_mouse_warp(210,310);
    uint32_t message=void_galloc(28),peek[5]={message,second,0x0200,0x0200,1};assert(api("PeekMessageW",5,peek));
    assert(MEM32(message)==second && MEM32(message+4)==0x0200 && MEM32(message+12)==((10u<<16)|10));
    assert(api("ReleaseCapture",0,NULL) && !api("GetCapture",0,NULL) && capture_changes==2 && !changed_to);
    assert(api("ReleaseCapture",0,NULL));
    assert(!api("SetCapture",1,&first));assert(api("SetCapture",1,&zero)==first && !api("GetCapture",0,NULL));
    assert(!api("SetCapture",1,&invalid) && void_get_last_error()==1400);
    assert(!api("SetCapture",1,&second));assert(api("DestroyWindow",1,&second));assert(!api("GetCapture",0,NULL));
    assert(api("DestroyWindow",1,&first));
    void_gfree(message);void_gfree(wc);void_gfree(name);void_gfree(guarded);void_gfree(rect);
    printf("PASS cursor clip/get/release/zero/inverted/resize and physical clamps; raw DirectInput preserved; CursorInfo20, MapWindowPoints RECT2, capture routing/lifetime, guards and stdcall\n");
    return 0;
}
