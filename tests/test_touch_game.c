/* Production bounded reflection + input observation against synthetic UE3 metadata. */
#define _GNU_SOURCE
#define UT3_ANDROID_APP
#ifdef _WIN32
#include "touch_host_win.h"
/* This fixture requests one fixed, private, read/write anonymous mapping. */
#define mmap(address,length,protection,flags,fd,offset) VirtualAlloc((address),(length),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)
#else
#include <sys/mman.h>
#endif
#include <assert.h>
#include <stdio.h>
#include "../android/host/input.c"
#include "../android/host/touch_game.c"
#include "../android/host/touch_menu.c"

enum { TEST_SIZE=0x02000000, ENGINE=0x100000, VIEWPORT=0x110000,
       ENGINE_CLASS=0x120000, PORT_CLASS=0x121000, OBJECT_KIND=0x122000,
       INT_KIND=0x123000, VIEWPORT_PROPERTY=0x130000,
       MODE_PROPERTY=0x131000, EPOCH_PROPERTY=0x132000, VEHICLE_PROPERTY=0x133000,
       MENU_PROPERTY=0x134000, STRING_KIND=0x135000, STRUCT_KIND=0x136000,
       PATH_PROPERTY=0x137000, CONTROLLER_PROPERTY=0x138000,
       CONTROLLER=0x140000, CONTROLLER_CLASS=0x141000, SCENE=0x142000,
       SCENE_CLASS=0x143000, SCENE_PROPERTY=0x144000, MOUSE_PROPERTY=0x145000,
       PATH_DATA=0x146000, ACTIVE_PROPERTY=0x147000, OWNER_PROPERTY=0x148000,
       UI_OBJECT_CLASS=0x149000, LIST_CLASS=0x14A000, BUTTON_CLASS=0x14B000,
       SLIDER_CLASS=0x14C000, WIDGET=0x14D000, LIST=0x14E000,
       SCROLL_TARGET_PROPERTY=0x14F000, SCROLL_STEPS_PROPERTY=0x150000,
       OPTION_BUTTON_CLASS=0x160000, OPTION_BUTTON=0x161000, FLOAT_KIND_TEST=0x162000,
       DETAIL_PROPERTIES=0x163000 /* seven consecutive 0x100-byte property objects */ };
static int owned=1, reads, logs;
static unsigned hud_observed;
static float hud_last;
static int hud_accept=1;
int void_ui_hud_scale_observe(float scale) { ++hud_observed;hud_last=scale;return hud_accept; }
static uint32_t denied, readonly, ticks, names=1, name_storage=0x10000;
int void_machine_is_owned(void) { return owned; }
int void_vm_readable(uint32_t address,uint32_t size)
{ ++reads;return address && size && address<=TEST_SIZE-size && (!denied || address+size<=denied || address>=denied+4); }
int void_vm_writable(uint32_t address,uint32_t size)
{ return void_vm_readable(address,size) && (!readonly || address+size<=readonly || address>=readonly+4); }
uint32_t void_tick_ms(void) { return ++ticks; }
uint64_t void_monotonic_ns(void) { return (uint64_t)++ticks*1000000; }
uint32_t void_window_main(void) { return 0; }
void void_post_message(uint32_t h,uint32_t m,uint32_t w,uint32_t l) { (void)h;(void)m;(void)w;(void)l; }
void void_log(const char *format,...) { (void)format;++logs; }
static void put(uint32_t address,uint32_t value) { memcpy(GPTR(address),&value,4); }
static void name(uint32_t object,const char *text)
{
    put(object+NAME_OFFSET,names);put(0x1000+names++*4,name_storage);
    for (unsigned i=0;;++i) { uint16_t c=(unsigned char)text[i];memcpy(GPTR(name_storage+16+2*i),&c,2);if(!c)break; }
    name_storage+=0x100;
}
static void property(uint32_t object,const char *text,uint32_t kind,uint32_t offset,uint32_t next)
{ name(object,text);put(object+CLASS_OFFSET,kind);put(object+PROPERTY_OFFSET,offset);put(object+NEXT_OFFSET,next); }
static void fixture(void)
{
    memset(GPTR(0),0,TEST_SIZE);memset(&touch_metadata,0,sizeof touch_metadata);
    names=1;name_storage=0x10000;denied=readonly=0;owned=1;
    put(0x1DD46E8,0x1000);put(0x1DEB464,ENGINE);
    name(ENGINE_CLASS,"Engine");name(PORT_CLASS,"AndroidPortViewportClient");
    name(OBJECT_KIND,"ObjectProperty");name(INT_KIND,"IntProperty");
    put(ENGINE+CLASS_OFFSET,ENGINE_CLASS);put(VIEWPORT+CLASS_OFFSET,PORT_CLASS);
    put(ENGINE_CLASS+CHILDREN_OFFSET,VIEWPORT_PROPERTY);
    put(PORT_CLASS+CHILDREN_OFFSET,MODE_PROPERTY);
    property(VIEWPORT_PROPERTY,"GameViewport",OBJECT_KIND,0x80,0);
    property(MODE_PROPERTY,"PortInputMode",INT_KIND,0x90,EPOCH_PROPERTY);
    property(EPOCH_PROPERTY,"PortInputEpoch",INT_KIND,0x94,VEHICLE_PROPERTY);
    property(VEHICLE_PROPERTY,"PortVehicleProfile",INT_KIND,0x98,0);
    put(ENGINE+0x80,VIEWPORT);put(VIEWPORT+0x90,1);put(VIEWPORT+0x94,1);
}
static void publish(uint32_t mode,uint32_t epoch,uint32_t profile)
{ put(VIEWPORT+0x90,mode);put(VIEWPORT+0x94,epoch);put(VIEWPORT+0x98,profile); }
static void menu_property(uint32_t kind,uint32_t offset)
{ property(MENU_PROPERTY,"PortMenuRequest",kind,offset,0);put(VEHICLE_PROPERTY+NEXT_OFFSET,MENU_PROPERTY); }
static void observe(void) { uint8_t keys[256];void_input_keyboard_snapshot(keys); }
static void ui_fixture(void)
{
    fixture();
    name(STRING_KIND,"StrProperty");name(STRUCT_KIND,"StructProperty");
    name(CONTROLLER_CLASS,"UIInteraction");name(SCENE_CLASS,"UISceneClient");
    put(VEHICLE_PROPERTY+NEXT_OFFSET,PATH_PROPERTY);
    property(PATH_PROPERTY,"PortInputScenePath",STRING_KIND,0xB0,CONTROLLER_PROPERTY);
    property(CONTROLLER_PROPERTY,"UIController",OBJECT_KIND,0xC0,0);
    put(VIEWPORT+0xC0,CONTROLLER);put(CONTROLLER+CLASS_OFFSET,CONTROLLER_CLASS);
    put(CONTROLLER_CLASS+CHILDREN_OFFSET,SCENE_PROPERTY);
    property(SCENE_PROPERTY,"SceneClient",OBJECT_KIND,0x90,0);
    put(CONTROLLER+0x90,SCENE);put(SCENE+CLASS_OFFSET,SCENE_CLASS);
    put(SCENE_CLASS+CHILDREN_OFFSET,MOUSE_PROPERTY);
    property(MOUSE_PROPERTY,"MousePosition",STRUCT_KIND,0xA0,0);
    put(SCENE+0xA0,123);put(SCENE+0xA4,456);
    uint32_t snapshot[4];assert(void_touch_game_read(snapshot)==1);
}
static void capturing_scene(void)
{
    put(VIEWPORT+0xB0,PATH_DATA);put(VIEWPORT+0xB4,2);put(VIEWPORT+0xB8,2);
    put(PATH_DATA,'x'); /* UTF16 x and terminating NUL; no field text is copied out. */
}
static void scroll_fixture(void)
{
    ui_fixture();capturing_scene();publish(2,1,0);
    put(CONTROLLER_PROPERTY+NEXT_OFFSET,SCROLL_TARGET_PROPERTY);
    property(SCROLL_TARGET_PROPERTY,"PortScrollTarget",OBJECT_KIND,0xD0,SCROLL_STEPS_PROPERTY);
    property(SCROLL_STEPS_PROPERTY,"PortScrollSteps",INT_KIND,0xD4,0);
    put(MOUSE_PROPERTY+NEXT_OFFSET,ACTIVE_PROPERTY);
    property(ACTIVE_PROPERTY,"ActiveControl",OBJECT_KIND,0xA8,0);
    name(UI_OBJECT_CLASS,"UIObject");name(LIST_CLASS,"UIList");name(BUTTON_CLASS,"UIButton");name(SLIDER_CLASS,"UISlider");
    put(LIST_CLASS+SUPER_OFFSET,UI_OBJECT_CLASS);put(BUTTON_CLASS+SUPER_OFFSET,UI_OBJECT_CLASS);put(SLIDER_CLASS+SUPER_OFFSET,UI_OBJECT_CLASS);
    put(UI_OBJECT_CLASS+CHILDREN_OFFSET,OWNER_PROPERTY);property(OWNER_PROPERTY,"Owner",OBJECT_KIND,0x80,0);
    put(WIDGET+CLASS_OFFSET,BUTTON_CLASS);put(WIDGET+0x80,LIST);put(LIST+CLASS_OFFSET,LIST_CLASS);
    put(SCENE+0xA8,WIDGET);touch_metadata.viewport_class=0;
    uint32_t snapshot[4];assert(void_touch_game_read(snapshot)==1);
}
/* Probe flags: 1 a tap clicks, 2 list rows follow the finger, 4 a drag adjusts, 8 a tap changes the option row. */
static void scroll_bounds(void)
{
    scroll_fixture();assert(void_touch_ui_control()==3);
    assert(void_touch_ui_scroll(3)==1 && MEM32(VIEWPORT+0xD0)==LIST && MEM32(VIEWPORT+0xD4)==3);
    assert(void_touch_ui_scroll(-1)==0 && MEM32(VIEWPORT+0xD4)==3);
    put(VIEWPORT+0xD4,0);assert(void_touch_ui_scroll(-8)==1 && (int32_t)MEM32(VIEWPORT+0xD4)==-8);
    scroll_fixture();owned=0;reads=0;assert(void_touch_ui_control()==-1 && void_touch_ui_scroll(1)==-1 && !reads && !MEM32(VIEWPORT+0xD0));
    owned=1;readonly=VIEWPORT+0xD4;assert(void_touch_ui_scroll(1)==-1 && !MEM32(VIEWPORT+0xD0) && !MEM32(VIEWPORT+0xD4));
    scroll_fixture();assert(void_touch_ui_scroll(0)==1 && void_touch_ui_scroll(9)==-1);
    /* A slider or scrollbar in a list drags sideways; vertical swipes still point at the list's rows. */
    put(WIDGET+CLASS_OFFSET,SLIDER_CLASS);assert(void_touch_ui_control()==7 && MEM32(VIEWPORT+0xD0)==LIST);
    name(SLIDER_CLASS,"UIScrollbar");assert(void_touch_ui_control()==7);
    name(SLIDER_CLASS,"UIEditBox");assert(void_touch_ui_control()==3);
    scroll_fixture();put(LIST+CLASS_OFFSET,SLIDER_CLASS);assert(void_touch_ui_control()==5 && !MEM32(VIEWPORT+0xD0));
    scroll_fixture();put(WIDGET+0x80,WIDGET);assert(void_touch_ui_control()==-1);
    scroll_fixture();put(LIST+0x80,WIDGET);assert(void_touch_ui_control()==-1);
    scroll_fixture();denied=WIDGET+0x80;assert(void_touch_ui_control()==-1 && void_touch_ui_scroll(1)==-1);
    scroll_fixture();put(SCENE+0xA8,0);assert(void_touch_ui_control()==1 && void_touch_ui_scroll(1)==-1);
    scroll_fixture();put(VIEWPORT+0x90,1);assert(void_touch_ui_scroll(1)==-1);
    scroll_fixture();assert(void_touch_ui_control()==3 && MEM32(VIEWPORT+0xD0)==LIST);
    put(WIDGET+CLASS_OFFSET,SLIDER_CLASS);assert(void_touch_ui_scroll(1)==1 && MEM32(VIEWPORT+0xD0)==LIST);
    put(VIEWPORT+0xD4,0);assert(void_touch_ui_scroll(0)==1 && MEM32(VIEWPORT+0xD0)==LIST);
    assert(void_touch_ui_scroll(INT32_MIN)==1 && !MEM32(VIEWPORT+0xD0) && !MEM32(VIEWPORT+0xD4));
    scroll_fixture();name(LIST_CLASS,"UTSimpleList");assert(void_touch_ui_control()==3);
    /* An option row's label or value: a tap changes it through the script, never a raw click. */
    scroll_fixture();name(LIST_CLASS,"UTUIOptionList");assert(void_touch_ui_control()==10);
    assert(void_touch_ui_scroll(65)==1 && MEM32(VIEWPORT+0xD4)==65);
    put(VIEWPORT+0xD4,0);assert(void_touch_ui_scroll(64)==1 && MEM32(VIEWPORT+0xD4)==64);
    put(VIEWPORT+0xD4,0);assert(void_touch_ui_scroll(66)==1 && MEM32(VIEWPORT+0xD4)==66);
    put(VIEWPORT+0xD4,0);assert(void_touch_ui_scroll(4096+3)==1 && MEM32(VIEWPORT+0xD4)==4099);
    put(VIEWPORT+0xD4,0);assert(void_touch_ui_scroll(67)==-1 && void_touch_ui_scroll(63)==-1 && void_touch_ui_scroll(6144)==-1 && !MEM32(VIEWPORT+0xD4));
    /* A checkbox or an option button's own arrow keeps its original click. */
    scroll_fixture();name(LIST_CLASS,"UTUIOptionList");name(BUTTON_CLASS,"UICheckbox");assert(void_touch_ui_control()==3);
    scroll_fixture();name(LIST_CLASS,"UTUIOptionList");name(OPTION_BUTTON_CLASS,"UTUIOptionButton");
    put(OPTION_BUTTON_CLASS+SUPER_OFFSET,UI_OBJECT_CLASS);put(OPTION_BUTTON+CLASS_OFFSET,OPTION_BUTTON_CLASS);
    put(OPTION_BUTTON+0x80,LIST);put(WIDGET+0x80,OPTION_BUTTON);assert(void_touch_ui_control()==3);
    put(SCENE+0xA8,OPTION_BUTTON);assert(void_touch_ui_control()==10);
    /* A combo box: its value text changes like an option, its own button opens the original list. */
    name(OPTION_BUTTON_CLASS,"UIComboBox");name(BUTTON_CLASS,"UIEditBox");put(SCENE+0xA8,WIDGET);
    assert(void_touch_ui_control()==10);
    name(BUTTON_CLASS,"UIButton");assert(void_touch_ui_control()==3);
    /* A plain text field in an option list keeps its click. */
    put(WIDGET+0x80,LIST);name(BUTTON_CLASS,"UIEditBox");assert(void_touch_ui_control()==3);
    /* Rows in a plain list are only highlighted; the option tap needs an option list. */
    scroll_fixture();assert(void_touch_ui_control()==3 && void_touch_ui_scroll(65)==-1 && void_touch_ui_scroll(64)==1);
    scroll_fixture();put(SCROLL_TARGET_PROPERTY+CLASS_OFFSET,INT_KIND);touch_metadata.viewport_class=0;
    uint32_t snapshot[4];assert(void_touch_game_read(snapshot)==1 && void_touch_ui_control()==1);
    const uint32_t aliases[]={0x90,0x94,0x98,0xB0,0xB4,0xB8,0xC0,0xD4};
    for(unsigned i=0;i<sizeof aliases/sizeof *aliases;++i){
        scroll_fixture();put(SCROLL_TARGET_PROPERTY+PROPERTY_OFFSET,aliases[i]);touch_metadata.viewport_class=0;
        assert(void_touch_game_read(snapshot)==1 && void_touch_ui_control()==1 && void_touch_ui_scroll(1)==-1);
    }
    puts("PASS UI pointer reflection: typed list/Owner ancestry, tap/rows/drag/option flags, checkbox and option-arrow clicks, slider drags, highlight and option-tap requests, cycle/ownership/type/alias/read/write bounds, no pointer retention, two-field checked mailbox busy/ACK");
}

/* The seven optional publisher fields, consecutive in the class's property chain. */
static void detail_fixture(void)
{
    static const char *fields[7]={"PortWeaponSlots","PortWeaponRequest","PortWeaponPreview","PortVehicleFlags",
        "PortSteer","PortSteerReverse","PortSteerScale"};
    fixture();name(FLOAT_KIND_TEST,"FloatProperty");
    for(unsigned i=0;i<7;++i) {
        uint32_t kind=i==2 ? OBJECT_KIND : i>=4 ? FLOAT_KIND_TEST : INT_KIND;
        property(DETAIL_PROPERTIES+i*0x100,fields[i],kind,0xE0+i*4,i<6 ? DETAIL_PROPERTIES+(i+1)*0x100 : 0);
    }
    put(VEHICLE_PROPERTY+NEXT_OFFSET,DETAIL_PROPERTIES);
    uint32_t snapshot[4];assert(void_touch_game_read(snapshot)==1);
}
static void putf(uint32_t address,float value) { memcpy(GPTR(address),&value,4); }
static void detail_cases(void)
{
    uint32_t out[3],snapshot[4],flags=0;float steer[3];
    /* An older package without the fields: everything stays off. */
    fixture();assert(void_touch_game_read(snapshot)==1);
    assert(void_touch_game_details(out)==0 && !out[0] && !out[1] && void_touch_weapon_request(0x102)==-1 &&
           !void_touch_weapon_preview() && !void_touch_steer(steer,&flags));
    detail_fixture();
    put(VIEWPORT+0xE0,1|2|16|(4<<10)|(1<<14));put(VIEWPORT+0xEC,1|4);
    assert(void_touch_game_details(out)==1 && out[0]==(1u|2|16|(4u<<10)|(1u<<14)) && out[1]==5);
    put(VIEWPORT+0xE0,1|(12<<10)|(1<<14));put(VIEWPORT+0xEC,64);
    assert(void_touch_game_details(out)==1 && !out[0] && !out[1]); /* Invalid selected slot, unknown flag. */
    put(VIEWPORT+0xE0,1u<<15);put(VIEWPORT+0xEC,0);assert(void_touch_game_details(out)==1 && out[0]==(1u<<15)); /* Minimap only. */
    put(VIEWPORT+0xE0,1u<<16);assert(void_touch_game_details(out)==1 && !out[0]);
    /* Weapon mailbox: a later preview replaces an unread preview, never an unread switch. */
    assert(void_touch_weapon_request(0x103)==1 && MEM32(VIEWPORT+0xE4)==0x103);
    assert(void_touch_weapon_request(0x105)==1 && MEM32(VIEWPORT+0xE4)==0x105);
    assert(void_touch_weapon_request(0x205)==1 && void_touch_weapon_request(0x106)==0 && MEM32(VIEWPORT+0xE4)==0x205);
    put(VIEWPORT+0xE4,0);assert(void_touch_weapon_request(0x300)==1);
    owned=0;assert(void_touch_weapon_request(0x101)==-1 && !void_touch_weapon_preview());owned=1;
    readonly=VIEWPORT+0xE4;put(VIEWPORT+0xE4,0);assert(void_touch_weapon_request(0x101)==-1 && !MEM32(VIEWPORT+0xE4));readonly=0;
    put(VIEWPORT+0xE8,0x777000);assert(void_touch_weapon_preview()==0x777000);
    /* Look steering only while the script flags it, with finite bounded values. */
    put(VIEWPORT+0xEC,1);putf(VIEWPORT+0xF0,.5f);putf(VIEWPORT+0xF4,-.3f);putf(VIEWPORT+0xF8,1.1f);
    assert(!void_touch_steer(steer,&flags));
    put(VIEWPORT+0xEC,8|16|32);assert(void_touch_steer(steer,&flags)==1 && flags==(8|16|32) &&
        steer[0]==.5f && steer[1]==-.3f && steer[2]==1.1f);
    putf(VIEWPORT+0xF0,1.5f);assert(!void_touch_steer(steer,&flags));
    putf(VIEWPORT+0xF0,.5f);putf(VIEWPORT+0xF8,0);assert(!void_touch_steer(steer,&flags));
    putf(VIEWPORT+0xF8,NAN);assert(!void_touch_steer(steer,&flags));
    /* A duplicated offset disables all seven fields. */
    detail_fixture();put(DETAIL_PROPERTIES+3*0x100+PROPERTY_OFFSET,0xE0);touch_metadata.viewport_class=0;
    assert(void_touch_game_read(snapshot)==1);
    assert(void_touch_game_details(out)==0 && void_touch_weapon_request(0x101)==-1);
    puts("PASS script details: optional seven-field set, slot/flag validation, weapon mailbox preview/switch/cancel ordering, ownership/read-only bounds, preview object, bounded look-steering values");
}
static void hud_settings_cases(void)
{
    enum { STORE_FIELD=0x154000,STORE_CLASS=0x155000,STORE=0x156000,
           SETTINGS_FIELD=0x157000,SETTINGS_CLASS=0x158000,SETTINGS=0x159000,
           SCALE_FIELD=0x15A000,FLOAT_KIND=0x15B000 };
    uint32_t out[4];float value=1.25f;fixture();hud_observed=0;hud_accept=1;
    name(STORE_CLASS,"AndroidPortDataStore");name(SETTINGS_CLASS,"AndroidPortSettings");name(FLOAT_KIND,"FloatProperty");
    property(STORE_FIELD,"PortStore",OBJECT_KIND,0xD0,0);put(VEHICLE_PROPERTY+NEXT_OFFSET,STORE_FIELD);
    put(STORE_CLASS+CHILDREN_OFFSET,SETTINGS_FIELD);property(SETTINGS_FIELD,"Settings",OBJECT_KIND,0x80,0);
    put(SETTINGS_CLASS+CHILDREN_OFFSET,SCALE_FIELD);property(SCALE_FIELD,"uiScale",FLOAT_KIND,0x80,0);
    put(STORE+CLASS_OFFSET,STORE_CLASS);put(SETTINGS+CLASS_OFFSET,SETTINGS_CLASS);put(STORE+0x80,SETTINGS);
    memcpy(GPTR(SETTINGS+0x80),&value,4);
    assert(void_touch_game_read(out)==1&&hud_observed==1&&hud_last==1); /* Store not ready; retry same epoch. */
    put(VIEWPORT+0xD0,STORE);assert(void_touch_game_read(out)==1&&hud_observed==2&&hud_last==1.25f);
    unsigned before=hud_observed;value=1.5f;memcpy(GPTR(SETTINGS+0x80),&value,4);
    assert(void_touch_game_read(out)==1&&hud_observed==before); /* No per-pump settings walk. */
    publish(2,2,0);assert(void_touch_game_read(out)==1&&hud_observed==before+1&&hud_last==1);
    publish(1,3,0);assert(void_touch_game_read(out)==1&&hud_observed==before+2&&hud_last==1.5f);
    hud_accept=0;publish(1,4,0);assert(void_touch_game_read(out)==1);before=hud_observed;
    assert(void_touch_game_read(out)==1&&hud_observed==before+1); /* Native metadata not yet ready. */
    hud_accept=1;assert(void_touch_game_read(out)==1);before=hud_observed;
    owned=0;reads=0;assert(void_touch_game_read(out)==-1&&!reads&&hud_observed==before);owned=1;
    touch_metadata.hud_settings_class=0;put(SCALE_FIELD+CLASS_OFFSET,INT_KIND);publish(1,5,0);
    assert(void_touch_game_read(out)==1&&hud_last==1); /* Wrong type never becomes a scale. */
    put(0x1DEB464,0);before=hud_observed;assert(!void_touch_game_read(out)&&hud_observed==before+1&&hud_last==1);
    fixture();hud_accept=1;assert(void_touch_game_read(out)==1&&hud_last==1);before=hud_observed;
    assert(void_touch_game_read(out)==1&&hud_observed==before); /* Older module without optional PortStore. */
    puts("PASS HUD settings observer: existing typed PortStore.Settings.uiScale, default/old modules, initialization/native readiness retries, epoch/UI-close updates, same-context skip, ownership/type/lost-publisher guards");
}

int main(void)
{
    assert(mmap((void *)(uintptr_t)VOID_GUEST_BASE,TEST_SIZE,PROT_READ|PROT_WRITE,
                MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)==(void *)(uintptr_t)VOID_GUEST_BASE);
    uint32_t out[4]={99,99,99,99};fixture();
    owned=0;reads=0;assert(void_touch_game_read(out)==-1 && !reads && out[0]==99);owned=1;
    assert(void_touch_game_read(out)==1 && out[0]==1 && out[1]==1 && out[2]==0 && out[3]==VIEWPORT);
    for(unsigned mode=0;mode<3;++mode)for(unsigned vehicle=0;vehicle<5;++vehicle) {
        publish(mode,INT32_MAX,vehicle);assert(void_touch_game_read(out)==1 && out[0]==mode && out[2]==vehicle);
    }
    const uint32_t invalid[][3]={{3,1,0},{1,0,0},{1,0x80000000u,0},{1,1,5}};
    for(unsigned i=0;i<sizeof invalid/sizeof *invalid;++i) { publish(invalid[i][0],invalid[i][1],invalid[i][2]);assert(!void_touch_game_read(out)); }
    fixture();denied=VIEWPORT+0x94;assert(!void_touch_game_read(out));
    fixture();put(ENGINE+CLASS_OFFSET,0xFFFFFFF0u);assert(!void_touch_game_read(out));
    fixture();put(PORT_CLASS+NAME_OFFSET,1048576);assert(!void_touch_game_read(out));
    fixture();put(MODE_PROPERTY+CLASS_OFFSET,OBJECT_KIND);assert(!void_touch_game_read(out));
    const uint32_t bad_offsets[]={0x38,0x91,65536,0xFFFFFFFCu};
    for(unsigned i=0;i<sizeof bad_offsets/sizeof *bad_offsets;++i) { fixture();put(MODE_PROPERTY+PROPERTY_OFFSET,bad_offsets[i]);assert(!void_touch_game_read(out)); }
    fixture();put(EPOCH_PROPERTY+PROPERTY_OFFSET,0x90);assert(!void_touch_game_read(out));
    fixture();name(MODE_PROPERTY,"Unrelated");put(MODE_PROPERTY+NEXT_OFFSET,MODE_PROPERTY);assert(!void_touch_game_read(out));
    fixture();name(PORT_CLASS,"Unrelated");put(PORT_CLASS+SUPER_OFFSET,PORT_CLASS);assert(!void_touch_game_read(out));
    fixture();name(MODE_PROPERTY,"Unrelated");name(EPOCH_PROPERTY,"Unrelated");name(VEHICLE_PROPERTY,"Unrelated");
    put(VEHICLE_PROPERTY+NEXT_OFFSET,MODE_PROPERTY);reads=0;assert(!void_touch_game_read(out) && reads<50000);

    fixture();publish(2,1,0);observe();uint64_t ui=touch_cursor_context;
    assert((ui&3)==3 && touch_game_active && touch_vehicle==0 && logs==1);
    void_input_cursor_visible(0);assert(touch_cursor_context==ui);
    assert(!touch_submit(1,0,0,0,0,ui));assert(!touch_submit(0,0,0,0,4,ui));
    for(unsigned i=0;i<110;++i){void_input_key('A',1);void_input_key('A',0);}
    assert(key_ring.count==220); /* Independent physical keyboard ring is unread. */
    assert(touch_submit(0,0,0,0,1,ui));assert(touch_buttons[0] && key_ring.count==220);
    assert(touch_submit(0,0,0,0,0,ui));assert(!touch_buttons[0] && key_ring.count==220);
    assert(!touch_submit(0,0,0,0,1,ui+4));assert(!touch_held && key_ring.count==220);
    while(pending_count<=464)void_input_mouse_move(0,0);
    assert(!touch_submit(0,0,0,0,1,ui));assert(!touch_held && !touch_buttons[0]);
    void_input_pump(); /* A full actual message batch still rejects neutrally. */
    void_input_event physical[256];assert(void_input_keyboard_events(physical,256,0)==220);
    assert(touch_submit(0,0,0,0,0,ui)); /* Rejected owner must become neutral before a fresh press. */
    uint8_t pad[16];memset(pad,0xA5,sizeof pad);
    assert(touch_submit(0,0,0,0,1,ui));assert(touch_held==1 && touch_buttons[0]);
    assert(!void_input_touch_pad(0,pad));for(unsigned i=0;i<sizeof pad;++i)assert(pad[i]==0xA5);
    assert(touch_submit(0,0,0,0,0,ui));assert(!touch_buttons[0] && !void_input_touch_pad(0,pad));
    assert(touch_submit(0,0,0,0,1,ui));
    publish(1,2,2);observe();uint64_t game=touch_cursor_context;
    assert((game&3)==2 && game!=ui && touch_vehicle==2 && !touch_held && logs==1);
    assert(!touch_submit(0,0,0,0,1,ui));assert(touch_submit(1,0,12,3,17,game));
    assert(void_input_touch_pad(0,pad));assert(touch_submit(0,0,0,0,0,game));
    assert(void_input_touch_pad(0,pad) && !touch_stick_x && !touch_stick_y);
    for(unsigned slot=1;slot<4;++slot)assert(!void_input_touch_pad(slot,pad));
    assert(touch_submit(1,0,0,0,17,game));
    publish(1,3,2);observe();assert(touch_cursor_context!=game && !touch_held && !touch_stick_x && !touch_mouse_dx);
    game=touch_cursor_context;owned=0;reads=0;publish(0,4,0);observe();assert(!reads && touch_cursor_context==game);
    owned=1;observe();assert((touch_cursor_context&3)==1 && touch_vehicle==0);
    assert(!touch_submit(0,0,0,0,1,touch_cursor_context));assert(!touch_submit(0,0,0,0,0,touch_cursor_context));
    put(ENGINE+0x80,0);observe();uint64_t blocked=touch_cursor_context;
    assert((blocked&3)==1 && !touch_game_active && touch_vehicle==-1);
    void_input_cursor_visible(0);assert(touch_cursor_context==blocked);
    put(ENGINE+0x80,VIEWPORT);publish(1,5,4);observe();
    assert((touch_cursor_context&3)==2 && touch_cursor_context!=blocked && touch_vehicle==4 && logs==1);
    assert(touch_submit(1,0,0,0,17,touch_cursor_context));assert(void_input_touch_pad(0,pad));
    publish(2,6,0);observe();assert((touch_cursor_context&3)==3 && !touch_held && !touch_buttons[0]);
    assert(!void_input_touch_pad(0,pad));assert(touch_submit(0,0,0,0,1,touch_cursor_context));
    assert(!void_input_touch_pad(0,pad));
    assert(!void_touch_menu_pending());
    for(int i=0;i<20;++i)Java_org_ut3_port_GameActivity_nativeSettings(NULL,NULL);
    assert(void_touch_menu_pending()==1);fixture();
    assert(void_touch_game_read(out)==1 && void_touch_menu_pending()==1); /* Old module remains valid. */
    fixture();menu_property(OBJECT_KIND,0x9C);assert(void_touch_game_read(out)==1 && void_touch_menu_pending()==1 && !MEM32(VIEWPORT+0x9C));
    const uint32_t bad_menu_offsets[]={0x90,0x94,0x98,0x9D,65536,0xFFFFFFFCu};
    for(unsigned i=0;i<sizeof bad_menu_offsets/sizeof *bad_menu_offsets;++i) {
        fixture();menu_property(INT_KIND,bad_menu_offsets[i]);
        assert(void_touch_game_read(out)==1 && void_touch_menu_pending()==1 && !MEM32(VIEWPORT+0x9C));
    }
    fixture();menu_property(INT_KIND,0x9C);put(VIEWPORT+0xA0,0xC0FFEEu);
    owned=0;reads=0;assert(void_touch_game_read(out)==-1 && !reads && !MEM32(VIEWPORT+0x9C) && void_touch_menu_pending()==1);
    owned=1;readonly=VIEWPORT+0x9C;
    assert(void_touch_game_read(out)==1 && !MEM32(VIEWPORT+0x9C) && void_touch_menu_pending()==1);
    readonly=0;publish(3,1,0);assert(!void_touch_game_read(out) && !MEM32(VIEWPORT+0x9C) && void_touch_menu_pending()==1);
    publish(1,1,0);assert(void_touch_game_read(out)==1 && MEM32(VIEWPORT+0x9C)==1 && !void_touch_menu_pending());
    assert(MEM32(VIEWPORT+0x90)==1 && MEM32(VIEWPORT+0x94)==1 && !MEM32(VIEWPORT+0x98) && MEM32(VIEWPORT+0xA0)==0xC0FFEEu);
    put(VIEWPORT+0x9C,0);assert(void_touch_game_read(out)==1 && !MEM32(VIEWPORT+0x9C) && !void_touch_menu_pending());
    Java_org_ut3_port_GameActivity_nativeSettings(NULL,NULL);
    assert(void_touch_game_read(out)==1 && MEM32(VIEWPORT+0x9C)==1 && !void_touch_menu_pending());
    uint32_t xy[2]={99,99};
    ui_fixture();assert(void_touch_ui_mouse(xy)==2 && xy[0]==99 && xy[1]==99);
    put(VIEWPORT+0xB0,PATH_DATA);put(VIEWPORT+0xB4,1);put(VIEWPORT+0xB8,1);
    assert(void_touch_ui_mouse(xy)==2); /* Explicit UTF16 empty console path. */
    capturing_scene();assert(void_touch_ui_mouse(xy)==1 && xy[0]==123 && xy[1]==456);
    owned=0;reads=0;assert(void_touch_ui_mouse(xy)==-1 && !reads);owned=1;
    denied=SCENE+0xA4;assert(!void_touch_ui_mouse(xy));denied=0;
    const uint32_t bad_lengths[]={1025,0xFFFFFFFFu};
    for(unsigned i=0;i<sizeof bad_lengths/sizeof *bad_lengths;++i) {
        ui_fixture();capturing_scene();put(VIEWPORT+0xB4,bad_lengths[i]);assert(!void_touch_ui_mouse(xy));
    }
    ui_fixture();capturing_scene();put(VIEWPORT+0xB8,1);assert(!void_touch_ui_mouse(xy));
    ui_fixture();capturing_scene();put(PATH_DATA+2,'x');assert(!void_touch_ui_mouse(xy));
    ui_fixture();capturing_scene();put(VIEWPORT+0xB0,0xFFFFFFFEu);assert(!void_touch_ui_mouse(xy));
    ui_fixture();capturing_scene();put(CONTROLLER+CLASS_OFFSET,OBJECT_KIND);assert(!void_touch_ui_mouse(xy));
    ui_fixture();capturing_scene();put(SCENE+CLASS_OFFSET,OBJECT_KIND);assert(!void_touch_ui_mouse(xy));
    ui_fixture();capturing_scene();put(MOUSE_PROPERTY+CLASS_OFFSET,INT_KIND);assert(!void_touch_ui_mouse(xy));
    ui_fixture();capturing_scene();put(MOUSE_PROPERTY+PROPERTY_OFFSET,65532);assert(!void_touch_ui_mouse(xy));
    ui_fixture();capturing_scene();put(SCENE_PROPERTY+CLASS_OFFSET,INT_KIND);assert(!void_touch_ui_mouse(xy));
    ui_fixture();capturing_scene();put(0x1DEB464,0);assert(!void_touch_ui_mouse(xy));
    scroll_bounds();
    detail_cases();
    hud_settings_cases();
    puts("PASS UI mouse acknowledgement: reflection-only controller/scene/IntPoint, empty console bypass, ownership and FString/type/offset/read bounds, no guest calls or writes");
    puts("PASS settings mailbox: JNI coalescing, missing/bad optional field preserves observer/pending request, bounds/alias/read-only/ownership checks, one checked write and acknowledgement per delivery");
    puts("PASS production context: bounded reflection, invalid metadata, ownership gate, UI/loading/game epochs, vehicle profiles, stale input rejection, loss/restoration and cursor priority");
    return 0;
}
