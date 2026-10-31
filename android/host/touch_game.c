/* Read the port's script-published context only while this thread owns the VM. */
#include "host.h"

extern int void_machine_is_owned(void) __attribute__((weak));
extern int void_touch_menu_pending(void) __attribute__((weak));
extern void void_touch_menu_delivered(void) __attribute__((weak));
extern int void_ui_hud_scale_observe(float scale);

enum { CLASS_OFFSET=0x34, NAME_OFFSET=0x2C, SUPER_OFFSET=0x3C,
       NEXT_OFFSET=0x40, CHILDREN_OFFSET=0x4C, PROPERTY_OFFSET=0x64, OUTER_OFFSET=0x28 };
static struct {
    uint32_t engine_class, viewport_class, viewport, mode, epoch, vehicle, menu;
    uint32_t scene_path, controller, controller_class, scene, scene_class, mouse;
    uint32_t active, scroll_target, scroll_steps;
    uint32_t hud_viewport,hud_epoch,hud_store,hud_store_class,hud_settings,hud_settings_class,hud_scale;
    uint32_t list_string_class,list_owner_class;
    /* Optional publisher fields which never change the input context. */
    uint32_t weapon_slots,weapon_request,weapon_preview,vehicle_flags,steer,steer_reverse,steer_scale;
} touch_metadata;

static int touch_read32(uint32_t address, uint32_t *value)
{
    if (!address || address > UINT32_MAX-3 || !void_vm_readable(address, 4)) return 0;
    memcpy(value, GPTR(address), 4);
    return 1;
}

static int touch_member(uint32_t object, uint32_t offset, uint32_t *value)
{
    return object && offset <= UINT32_MAX-object && touch_read32(object+offset, value);
}

static unsigned touch_lower(unsigned value)
{
    return value >= 'A' && value <= 'Z' ? value + ('a'-'A') : value;
}

static int touch_name(uint32_t object, const char *expected)
{
    uint32_t index, table, entry;
    if (!touch_member(object, NAME_OFFSET, &index) || index >= 1048576 ||
        !touch_read32(0x01DD46E8u, &table) || !table ||
        index > (UINT32_MAX-table)/4 || !touch_read32(table+index*4, &entry) ||
        !entry || entry > UINT32_MAX-16-128) return 0;
    /* Original FName::ToString reads UTF-16 at entry+0x10 (00B7E475/47C). */
    for (unsigned i=0; i<64; ++i) {
        uint32_t address=entry+16+i*2;
        uint16_t value;
        if (!void_vm_readable(address, 2)) return 0;
        memcpy(&value, GPTR(address), 2);
        if (!expected[i]) return value == 0;
        if (touch_lower(value) != touch_lower((unsigned char)expected[i])) return 0;
    }
    return 0;
}

static int touch_class(uint32_t cls, const char *expected)
{
    for (unsigned depth=0; cls && depth<32; ++depth) {
        if (touch_name(cls, expected)) return 1;
        uint32_t next;
        if (!touch_member(cls, SUPER_OFFSET, &next) || next == cls) return 0;
        cls=next;
    }
    return 0;
}

static uint32_t touch_property(uint32_t cls, const char *name, const char *type)
{
    unsigned budget=2048;
    for (unsigned depth=0; cls && depth<32; ++depth) {
        uint32_t field;
        if (!touch_member(cls, CHILDREN_OFFSET, &field)) return 0;
        while (field && budget) {
            --budget;
            if (touch_name(field, name)) {
                uint32_t kind, offset;
                if (!touch_member(field, CLASS_OFFSET, &kind) || !touch_class(kind, type) ||
                    !touch_member(field, PROPERTY_OFFSET, &offset) || offset < 0x3C ||
                    offset > 65536-4 || (offset&3)) return 0;
                return offset;
            }
            uint32_t next;
            if (!touch_member(field, NEXT_OFFSET, &next) || next == field) return 0;
            field=next;
        }
        if (!budget) return 0;
        uint32_t next;
        if (!touch_member(cls, SUPER_OFFSET, &next) || next == cls) return 0;
        cls=next;
    }
    return 0;
}

/* -1: not a VM-owned call; 0: no valid publisher; 1: mode/epoch/profile/identity.
 * Cache only immutable class metadata, never World/PC/Pawn pointers. */
static int touch_game_read(uint32_t out[4],int deliver_menu)
{
    if (!void_machine_is_owned || !void_machine_is_owned()) return -1;
    uint32_t engine, engine_class, viewport, viewport_class;
    if (!touch_read32(0x01DEB464u, &engine) ||
        !touch_member(engine, CLASS_OFFSET, &engine_class)) return 0;
    if (engine_class != touch_metadata.engine_class || !touch_metadata.viewport) {
        memset(&touch_metadata, 0, sizeof touch_metadata);
        if (!touch_class(engine_class, "Engine")) return 0;
        touch_metadata.viewport=touch_property(engine_class, "GameViewport", "ObjectProperty");
        if (!touch_metadata.viewport) return 0;
        touch_metadata.engine_class=engine_class;
    }
    if (!touch_member(engine, touch_metadata.viewport, &viewport) ||
        !touch_member(viewport, CLASS_OFFSET, &viewport_class)) return 0;
    if (viewport_class != touch_metadata.viewport_class || !touch_metadata.mode ||
        !touch_metadata.epoch || !touch_metadata.vehicle) {
        touch_metadata.viewport_class=0;
        if (!touch_class(viewport_class, "AndroidPortViewportClient")) return 0;
        uint32_t mode=touch_property(viewport_class, "PortInputMode", "IntProperty");
        uint32_t epoch=touch_property(viewport_class, "PortInputEpoch", "IntProperty");
        uint32_t vehicle=touch_property(viewport_class, "PortVehicleProfile", "IntProperty");
        if (!mode || !epoch || !vehicle || mode==epoch || mode==vehicle || epoch==vehicle) return 0;
        touch_metadata.mode=mode; touch_metadata.epoch=epoch; touch_metadata.vehicle=vehicle;
        uint32_t menu=touch_property(viewport_class,"PortMenuRequest","IntProperty");
        touch_metadata.menu=menu!=mode && menu!=epoch && menu!=vehicle ? menu : 0;
        touch_metadata.scene_path=touch_property(viewport_class,"PortInputScenePath","StrProperty");
        touch_metadata.controller=touch_property(viewport_class,"UIController","ObjectProperty");
        uint32_t target=touch_property(viewport_class,"PortScrollTarget","ObjectProperty");
        uint32_t steps=touch_property(viewport_class,"PortScrollSteps","IntProperty");
        /* Optional fields cannot alias context, settings or pointer metadata. */
        uint32_t offsets[]={mode,epoch,vehicle,touch_metadata.menu,touch_metadata.controller};
        int distinct=target && steps && target!=steps;
        for (unsigned i=0;i<sizeof offsets/sizeof *offsets;++i)
            if (target==offsets[i] || steps==offsets[i])distinct=0;
        if (touch_metadata.scene_path &&
            ((target>=touch_metadata.scene_path && target<touch_metadata.scene_path+12) ||
             (steps>=touch_metadata.scene_path && steps<touch_metadata.scene_path+12)))distinct=0;
        touch_metadata.scroll_target=distinct ? target : 0;
        touch_metadata.scroll_steps=distinct ? steps : 0;
        uint32_t details[7]={
            touch_property(viewport_class,"PortWeaponSlots","IntProperty"),
            touch_property(viewport_class,"PortWeaponRequest","IntProperty"),
            touch_property(viewport_class,"PortWeaponPreview","ObjectProperty"),
            touch_property(viewport_class,"PortVehicleFlags","IntProperty"),
            touch_property(viewport_class,"PortSteer","FloatProperty"),
            touch_property(viewport_class,"PortSteerReverse","FloatProperty"),
            touch_property(viewport_class,"PortSteerScale","FloatProperty")};
        /* All seven or none: an older package simply lacks these features. */
        int present=1;
        for (unsigned i=0;i<7;++i) {
            if (!details[i])present=0;
            for (unsigned j=0;j<i;++j)if(details[i]==details[j])present=0;
            for (unsigned j=0;j<sizeof offsets/sizeof *offsets;++j)if(details[i]==offsets[j])present=0;
            if (details[i]==target || details[i]==steps)present=0;
        }
        touch_metadata.weapon_slots=present ? details[0] : 0;
        touch_metadata.weapon_request=present ? details[1] : 0;
        touch_metadata.weapon_preview=present ? details[2] : 0;
        touch_metadata.vehicle_flags=present ? details[3] : 0;
        touch_metadata.steer=present ? details[4] : 0;
        touch_metadata.steer_reverse=present ? details[5] : 0;
        touch_metadata.steer_scale=present ? details[6] : 0;
        touch_metadata.controller_class=touch_metadata.scene_class=0;
        touch_metadata.viewport_class=viewport_class;
    }
    uint32_t values[4];
    if (!touch_member(viewport, touch_metadata.mode, &values[0]) ||
        !touch_member(viewport, touch_metadata.epoch, &values[1]) ||
        !touch_member(viewport, touch_metadata.vehicle, &values[2]) ||
        values[0]>2 || !values[1] || values[1]>INT32_MAX || values[2]>4) return 0;
    if (deliver_menu && void_touch_menu_pending && void_touch_menu_delivered && touch_metadata.menu &&
        void_touch_menu_pending() && touch_metadata.menu<=UINT32_MAX-viewport) {
        uint32_t address=viewport+touch_metadata.menu;
        if (void_vm_writable(address,4)) {
            uint32_t request=1;memcpy(GPTR(address),&request,4);
            void_touch_menu_delivered();
        }
    }
    values[3]=viewport;
    memcpy(out, values, sizeof values);
    return 1;
}

/* The original settings store already exists independently of the options
 * scene. Read its typed scale on context transitions, retrying while the first
 * Tick is still creating the store. No JNI argument or script ABI is added. */
/* -1: older publisher; 0: not ready; 1: live typed value. No object pointers
 * or values are retained, including when the slider changes within one epoch. */
static int touch_settings_scale(uint32_t viewport,float *scale)
{
    uint32_t store,cls,settings,bits;
    if(!touch_metadata.hud_store)touch_metadata.hud_store=touch_property(touch_metadata.viewport_class,"PortStore","ObjectProperty");
    if(!touch_metadata.hud_store)return -1;
    if(!touch_member(viewport,touch_metadata.hud_store,&store)||!store||
       !touch_member(store,CLASS_OFFSET,&cls))return 0;
    if(cls!=touch_metadata.hud_store_class) {
        if(!touch_class(cls,"AndroidPortDataStore"))return 0;
        touch_metadata.hud_settings=touch_property(cls,"Settings","ObjectProperty");
        touch_metadata.hud_store_class=cls;touch_metadata.hud_settings_class=0;
    }
    if(!touch_metadata.hud_settings||!touch_member(store,touch_metadata.hud_settings,&settings)||!settings||
       !touch_member(settings,CLASS_OFFSET,&cls))return 0;
    if(cls!=touch_metadata.hud_settings_class) {
        if(!touch_class(cls,"AndroidPortSettings"))return 0;
        touch_metadata.hud_scale=touch_property(cls,"uiScale","FloatProperty");
        touch_metadata.hud_settings_class=cls;
    }
    if(!touch_metadata.hud_scale||!touch_member(settings,touch_metadata.hud_scale,&bits))return 0;
    memcpy(scale,&bits,4);return 1;
}

static void touch_hud_scale(const uint32_t context[4])
{
    uint32_t viewport=context[3],epoch=context[1];
    if(touch_metadata.hud_viewport==viewport&&touch_metadata.hud_epoch==epoch)return;
    float scale=1.0f;
    int result=context[0]==1 ? touch_settings_scale(viewport,&scale) : -1;
    if(!result) { void_ui_hud_scale_observe(1.0f);return; }
    if(void_ui_hud_scale_observe(scale)) {
        touch_metadata.hud_viewport=viewport;touch_metadata.hud_epoch=epoch;
    }
}

int void_touch_game_read(uint32_t out[4])
{
    int result=touch_game_read(out,1);
    if(result==1)touch_hud_scale(out);
    else if(result==0) {
        touch_metadata.hud_viewport=touch_metadata.hud_epoch=0;
        void_ui_hud_scale_observe(1.0f);
    }
    return result;
}

/* Formatter-only read: no menu delivery, HUD observer, JNI or guest call. */
int void_touch_ui_list_scale(uint32_t string,float *scale)
{
    uint32_t context[4],cls,owner,owner_class;
    float value;
    if(touch_game_read(context,0)!=1 || context[0]!=2 ||
       touch_settings_scale(context[3],&value)!=1 || !isfinite(value) || value<=1.0f || value>1.5f ||
       !touch_member(string,CLASS_OFFSET,&cls))return 0;
    if(cls!=touch_metadata.list_string_class) {
        if(!touch_class(cls,"UIListString"))return 0;
        touch_metadata.list_string_class=cls;
    }
    if(!touch_member(string,OUTER_OFFSET,&owner) || !touch_member(owner,CLASS_OFFSET,&owner_class))return 0;
    if(owner_class!=touch_metadata.list_owner_class) {
        if(!touch_class(owner_class,"UIList"))return 0;
        touch_metadata.list_owner_class=owner_class;
    }
    *scale=value;return 1;
}

/* UI pointer acknowledgement, read on the window's VM-owned input pump only.
 * 1: capturing scene's cached IntPoint; 2: console-only UI needs no hover;
 * 0: unavailable/invalid metadata; -1: caller does not own the machine.
 * MousePosition is updated by original SceneClient.Tick before hover hit-test.
 * There are no guest calls, object writes, or private field text reads here. */
static int touch_ui_scene(uint32_t out[2],uint32_t *scene_out)
{
    if (!void_machine_is_owned || !void_machine_is_owned()) return -1;
    uint32_t engine, cls, viewport, path, length, capacity;
    if (!touch_read32(0x01DEB464u,&engine) ||
        !touch_member(engine,CLASS_OFFSET,&cls) || cls!=touch_metadata.engine_class ||
        !touch_member(engine,touch_metadata.viewport,&viewport) ||
        !touch_member(viewport,CLASS_OFFSET,&cls) || cls!=touch_metadata.viewport_class ||
        !touch_metadata.scene_path || touch_metadata.scene_path>65536-12 ||
        !touch_member(viewport,touch_metadata.scene_path,&path) ||
        !touch_member(viewport,touch_metadata.scene_path+4,&length) ||
        !touch_member(viewport,touch_metadata.scene_path+8,&capacity) ||
        length>1024 || capacity<length || capacity>65536) return 0;
    if (length) {
        uint16_t terminator;
        if (!path || (path&1) || path>UINT32_MAX-length*2 ||
            !void_vm_readable(path,length*2)) return 0;
        memcpy(&terminator,GPTR(path+(length-1)*2),2);
        if (terminator) return 0;
    }
    if (length<=1) return 2;
    uint32_t controller, scene;
    if (!touch_metadata.controller ||
        !touch_member(viewport,touch_metadata.controller,&controller) ||
        !touch_member(controller,CLASS_OFFSET,&cls)) return 0;
    if (cls!=touch_metadata.controller_class) {
        touch_metadata.controller_class=0;
        if (!touch_class(cls,"UIInteraction")) return 0;
        touch_metadata.scene=touch_property(cls,"SceneClient","ObjectProperty");
        if (!touch_metadata.scene) return 0;
        touch_metadata.controller_class=cls;
    }
    if (!touch_member(controller,touch_metadata.scene,&scene) ||
        !touch_member(scene,CLASS_OFFSET,&cls)) return 0;
    if (cls!=touch_metadata.scene_class) {
        touch_metadata.scene_class=0;
        if (!touch_class(cls,"UISceneClient")) return 0;
        touch_metadata.mouse=touch_property(cls,"MousePosition","StructProperty");
        if (!touch_metadata.mouse || touch_metadata.mouse>65536-8) return 0;
        touch_metadata.scene_class=cls;
        touch_metadata.active=touch_property(cls,"ActiveControl","ObjectProperty");
    }
    uint32_t xy[2];
    if (!touch_member(scene,touch_metadata.mouse,&xy[0]) ||
        !touch_member(scene,touch_metadata.mouse+4,&xy[1])) return 0;
    memcpy(out,xy,sizeof xy);
    if (scene_out)*scene_out=scene;
    return 1;
}

int void_touch_ui_mouse(uint32_t out[2])
{ return touch_ui_scene(out,NULL); }

/* What a finger does to the control under the cursor, as probe flags:
 * 1 a tap clicks it, 2 the rows of the nearest list follow the finger,
 * 4 a sideways drag adjusts it (slider, scrollbar), 8 a tap changes the
 * option row. Text fields, checkboxes, an option button's own arrows and a
 * combo box's own button keep their original click; a combo's value text
 * is changed like an option. Never retain widget pointers. */
static int touch_ui_target(uint32_t *target)
{
    uint32_t xy[2],scene=0,object=0,list=0,first=0;
    int drag=0,keep=0,edit=0,combo=0,option=0;
    int result=touch_ui_scene(xy,&scene);
    if (result<0)return -1;
    if (result!=1 || !touch_metadata.scroll_target || !touch_metadata.scroll_steps)return 1;
    if (!touch_metadata.active || !touch_member(scene,touch_metadata.active,&object))return -1;
    for (unsigned depth=0;object && depth<32;++depth) {
        uint32_t cls,owner,next;
        if (!touch_member(object,CLASS_OFFSET,&cls) || !touch_class(cls,"UIObject"))return -1;
        if (!depth)first=cls;
        if (touch_class(cls,"UISlider") || touch_class(cls,"UIScrollbar"))drag=1;
        if (touch_class(cls,"UICheckbox"))keep=1;
        if (!list && touch_class(cls,"UIEditBox"))edit=1;
        if (!list && touch_class(cls,"UIComboBox")) {
            combo=1;
            if (depth==1 && touch_class(first,"UIButton"))keep=1;
        }
        if (depth==1 && touch_class(cls,"UTUIOptionButton") && touch_class(first,"UIButton"))keep=1;
        if (!list && (touch_class(cls,"UIList") || touch_class(cls,"UTSimpleList") || touch_class(cls,"UTUIOptionList"))) {
            list=object;option=touch_class(cls,"UTUIOptionList");
        }
        owner=touch_property(cls,"Owner","ObjectProperty");
        if (!owner || !touch_member(object,owner,&next))return -1;
        if (next==object)return -1;
        object=next;
    }
    if (object)return -1; /* Cycles and unbounded ancestry never become scroll targets. */
    if (!list)return drag ? 4|1 : 1;
    *target=list;
    if (drag)return 2|4|1;
    if (edit && !combo)keep=1;
    return 2|(option && !keep ? 8 : 1);
}

static int touch_ui_mailbox(uint32_t *target_address,uint32_t *step_address,int require_ui)
{
    uint32_t engine,viewport,cls,mode;
    if (!void_machine_is_owned || !void_machine_is_owned() ||
        !touch_metadata.scroll_target || !touch_metadata.scroll_steps ||
        !touch_read32(0x01DEB464u,&engine) ||
        !touch_member(engine,CLASS_OFFSET,&cls) || cls!=touch_metadata.engine_class ||
        !touch_member(engine,touch_metadata.viewport,&viewport) ||
        !touch_member(viewport,CLASS_OFFSET,&cls) || cls!=touch_metadata.viewport_class ||
        !touch_member(viewport,touch_metadata.mode,&mode) || (require_ui && mode!=2) ||
        touch_metadata.scroll_target>UINT32_MAX-viewport || touch_metadata.scroll_steps>UINT32_MAX-viewport)return 0;
    *target_address=viewport+touch_metadata.scroll_target;*step_address=viewport+touch_metadata.scroll_steps;
    return void_vm_writable(*target_address,4) && void_vm_writable(*step_address,4);
}

int void_touch_ui_control(void)
{
    uint32_t target,target_address,step_address,pending_steps;
    int result=touch_ui_target(&target);
    if (result<=0 || !(result&2))return result;
    if (!touch_ui_mailbox(&target_address,&step_address,1) || !touch_read32(step_address,&pending_steps))return -1;
    if (pending_steps)return 0;
    /* This own reflected strong reference lives only until queued END/cancel
     * or publisher epoch change. Reflow cannot retarget a claimed gesture. */
    memcpy(GPTR(target_address),&target,4);
    return result;
}

/* 1 delivered/ACK, 0 mailbox occupied, -1 invalid; zero steps queries the ACK.
 * Called under VM ownership and
 * host input serialization. Only our reflected Object/Int properties change.
 * INT32_MIN retires the gesture. Script retains our target across row reflow,
 * ACKs steps before API calls and clears both on context/session change. */
int void_touch_ui_scroll(int steps)
{
    if (!void_machine_is_owned || !void_machine_is_owned())return -1;
    /* 64 highlights the row under the cursor, 65 taps the option row there. */
    if (steps!=INT32_MIN && (steps< -8 || steps>8) && steps!=64 && steps!=65)return -1;
    uint32_t target,target_address,step_address,pending_steps,cls;
    if (!touch_ui_mailbox(&target_address,&step_address,steps!=INT32_MIN))return -1;
    if (steps==INT32_MIN) {
        uint32_t zero=0;memcpy(GPTR(step_address),&zero,4);memcpy(GPTR(target_address),&zero,4);return 1;
    }
    if (!touch_read32(step_address,&pending_steps))return -1;
    if (pending_steps)return 0;
    if (!steps)return 1;
    if (!touch_read32(target_address,&target) || !touch_member(target,CLASS_OFFSET,&cls) ||
        !(touch_class(cls,"UIList") || touch_class(cls,"UTSimpleList") || touch_class(cls,"UTUIOptionList")) ||
        (steps==65 && !touch_class(cls,"UTUIOptionList")))return -1;
    int32_t delta=steps;memcpy(GPTR(step_address),&delta,4);
    return 1;
}

/* The live publisher, under VM ownership, with the class metadata verified. */
static int touch_viewport(uint32_t *viewport)
{
    uint32_t engine,cls;
    return void_machine_is_owned && void_machine_is_owned() &&
        touch_read32(0x01DEB464u,&engine) && touch_member(engine,CLASS_OFFSET,&cls) &&
        cls==touch_metadata.engine_class && touch_metadata.viewport &&
        touch_member(engine,touch_metadata.viewport,viewport) &&
        touch_member(*viewport,CLASS_OFFSET,&cls) && cls==touch_metadata.viewport_class;
}

/* Weapon bar slots and vehicle flags, which change without a new input
 * context. 1 read, 0 unavailable (older package or no publisher). */
int void_touch_game_details(uint32_t out[2])
{
    uint32_t viewport;
    out[0]=out[1]=0;
    if (!touch_metadata.weapon_slots || !touch_viewport(&viewport) ||
        !touch_member(viewport,touch_metadata.weapon_slots,&out[0]) ||
        !touch_member(viewport,touch_metadata.vehicle_flags,&out[1])) { out[0]=out[1]=0;return 0; }
    /* Slots 0-9, selected slot 10-13 (0-9), bar shown 14, minimap 15; six vehicle flags. */
    if ((out[0]>>16) || ((out[0]>>10)&15)>9) out[0]=0;
    if (out[1]>>6) out[1]=0;
    return 1;
}

/* Weapon mailbox (phase << 8 | slot + 1), consumed by the next viewport Tick.
 * A later request replaces an unread preview, never an unread switch.
 * 1 written, 0 occupied by a switch, -1 unavailable. */
int void_touch_weapon_request(uint32_t request)
{
    uint32_t viewport,current,address;
    if (!touch_metadata.weapon_request || !touch_viewport(&viewport) ||
        touch_metadata.weapon_request>UINT32_MAX-viewport) return -1;
    address=viewport+touch_metadata.weapon_request;
    if (!touch_read32(address,&current) || !void_vm_writable(address,4)) return -1;
    if ((current>>8)==2 && current!=request) return 0;
    memcpy(GPTR(address),&request,4);
    return 1;
}

/* The weapon the script asks the original bar to show as selected, or 0. */
uint32_t void_touch_weapon_preview(void)
{
    uint32_t viewport,weapon;
    if (!touch_metadata.weapon_preview || !touch_viewport(&viewport) ||
        !touch_member(viewport,touch_metadata.weapon_preview,&weapon)) return 0;
    return weapon;
}

/* Console look steering for the driver's stick: steering toward the view,
 * the vehicle's DeflectionReverseThresh and ConsoleSteerScale. 1 when the
 * script publishes it for the current vehicle. */
int void_touch_steer(float out[3],uint32_t *flags)
{
    uint32_t viewport,bits,value[3];
    if (!touch_metadata.steer || !touch_viewport(&viewport) ||
        !touch_member(viewport,touch_metadata.vehicle_flags,&bits) || (bits>>6) || !(bits&8) ||
        !touch_member(viewport,touch_metadata.steer,&value[0]) ||
        !touch_member(viewport,touch_metadata.steer_reverse,&value[1]) ||
        !touch_member(viewport,touch_metadata.steer_scale,&value[2])) return 0;
    memcpy(out,value,sizeof value);
    if (!isfinite(out[0]) || fabsf(out[0])>1.0f || !isfinite(out[1]) || fabsf(out[1])>1.0f ||
        !isfinite(out[2]) || out[2]<=0.0f || out[2]>8.0f) return 0;
    *flags=bits;
    return 1;
}
