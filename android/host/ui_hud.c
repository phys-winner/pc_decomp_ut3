/* Preserve the original HUD's 4:3 artwork proportions and uniformly scale its
 * status panels/text, independently of render resolution and touch controls. */
#include "host.h"
#include <errno.h>
#include <math.h>

extern int void_machine_is_owned(void);
enum { HUD_VIRTUAL=0x00BD4440u, HUD_FINAL=0x00BD4480u,
       HUD_PUSH=0x00EF11B0u, HUD_POP=0x00EF0840u,
       HUD_CLASS_GLOBAL=0x01D49F2Cu, WEAPON_CLASS_GLOBAL=0x01D4B260u,
       CROSSHAIR_TILE=0x00EF6EC0u, CROSSHAIR_POSITION=0x00EEF280u, HUD_STACK_BYTES=2048 };

static void_fn hud_original[2],hud_push,hud_pop;
static float hud_requested=1.0f,hud_scale=1.0f;
static int hud_ready,hud_bound;
static struct {
    uint32_t root,canvas,map,portrait,resolution[4],names[2][2];
} hud_metadata;
static void_fn crosshair_original_tile,crosshair_original_position;
static int crosshair_bound;
static struct {
    uint32_t root,function,caller,argument,argument_offset,instigator,name[2];
    uint32_t weapon_class,pawn_class,controller,pc_class,simple_offset,simple_mask;
    int allowed;
} crosshair_metadata;
static _Thread_local struct {
    int active,busy,positioned;
    uint64_t generation;
    uint32_t hud,hud_class,canvas,canvas_class,native,depth,bounds[4],virtual_origin;
    float delta;
    uint8_t entry[80];
} crosshair_scope;
static void crosshair_tile(void);
static void crosshair_position(void);
/* A touch weapon preview is the original bar's selected card only while the
 * original DrawGameHUD runs: native DisplayWeaponBar (0x006A6670) selects
 * PawnOwner.InvManager.PendingWeapon, else Pawn.Weapon, and animates and names
 * that card itself. The field is restored before any gameplay code can run. */
extern uint32_t void_touch_weapon_preview(void) __attribute__((weak));
static struct { uint32_t hud,weapon,owner,item_manager,pawn_class,pawn_manager,manager_class,pending; } preview_metadata;
static _Thread_local struct { int active; uint32_t manager,previous,preview; } preview_scope;

/* Public feature state only, once per change. No object addresses or text. */
static void hud_report(const char *state)
{
    static const char *previous_state;
    static float previous_request;
    static int previous_bound,previous_prepared;
    int prepared=hud_metadata.root!=0;
    if(previous_state==state&&previous_request==hud_requested&&
       previous_bound==hud_bound&&previous_prepared==prepared)return;
    previous_state=state;previous_request=hud_requested;
    previous_bound=hud_bound;previous_prepared=prepared;
    int saved_errno=errno;
    void_log("[ui] HUD scale requested=%.2f bound=%d state=%s prepared=%d",
             (double)hud_requested,hud_bound,state,prepared);
    errno=saved_errno;
}

static int hud_read(uint32_t address,void *out,uint32_t bytes)
{
    if (!address || address>UINT32_MAX-bytes || !void_vm_readable(address,bytes))return 0;
    memcpy(out,GPTR(address),bytes);return 1;
}
static int hud_word(uint32_t address,uint32_t *out) { return hud_read(address,out,4); }
static int hud_member(uint32_t object,uint32_t offset,uint32_t *out)
{ return object && offset<=UINT32_MAX-object && hud_word(object+offset,out); }
static int hud_write(uint32_t address,const void *value,uint32_t bytes)
{
    if (!address || address>UINT32_MAX-bytes || !void_vm_writable(address,bytes))return 0;
    memcpy(GPTR(address),value,bytes);return 1;
}
static unsigned hud_lower(unsigned c) { return c>='A'&&c<='Z' ? c+32 : c; }
static int hud_name(uint32_t object,const char *name)
{
    uint32_t index,table,entry;
    if (!hud_member(object,0x2C,&index) || index>=1048576 ||
        !hud_word(0x01DD46E8u,&table) || !table || index>(UINT32_MAX-table)/4 ||
        !hud_word(table+index*4,&entry) || !entry || entry>UINT32_MAX-144)return 0;
    for(unsigned i=0;i<64;++i) {
        uint16_t c;
        if(!hud_read(entry+16+2*i,&c,2))return 0;
        if(!name[i])return !c;
        if(hud_lower(c)!=hud_lower((unsigned char)name[i]))return 0;
    }
    return 0;
}
static int hud_class(uint32_t cls,const char *name)
{
    for(unsigned depth=0;cls&&depth<32;++depth) {
        uint32_t next;
        if(hud_name(cls,name))return 1;
        if(!hud_member(cls,0x3C,&next)||next==cls)return 0;
        cls=next;
    }
    return 0;
}
static uint32_t hud_field(uint32_t cls,const char *name,const char *kind)
{
    unsigned budget=2048;
    for(unsigned depth=0;cls&&depth<32;++depth) {
        uint32_t field,next;
        if(!hud_member(cls,0x4C,&field))return 0;
        while(field&&budget--) {
            uint32_t type;
            if(hud_name(field,name))return hud_member(field,0x34,&type)&&hud_class(type,kind) ? field : 0;
            if(!hud_member(field,0x40,&next)||next==field)return 0;
            field=next;
        }
        if(field||!hud_member(cls,0x3C,&next)||next==cls)return 0;
        cls=next;
    }
    return 0;
}
static uint32_t hud_property(uint32_t cls,const char *name,const char *kind,uint32_t bytes)
{
    uint32_t field=hud_field(cls,name,kind),offset;
    return field&&hud_member(field,0x64,&offset)&&offset>=0x3C&&offset<=65536-bytes&&!(offset&3) ? offset : 0;
}
static void crosshair_prepare(void)
{
    uint32_t root,kind;
    if(!crosshair_original_tile||!crosshair_original_position||!hud_word(WEAPON_CLASS_GLOBAL,&root)||!hud_name(root,"UTWeapon")||
       !hud_member(root,0x34,&kind)||!hud_class(kind,"Class")) {
        crosshair_metadata.root=0;return;
    }
    if(crosshair_metadata.root==root)return;
    memset(&crosshair_metadata,0,sizeof crosshair_metadata);
    uint32_t function=hud_field(root,"DrawWeaponCrosshair","Function"),caller=hud_field(root,"ActiveRenderOverlays","Function"),owner;
    uint32_t argument=hud_field(caller,"H","ObjectProperty"),offset;
    if(!function||!caller||!argument||!hud_member(function,0x28,&owner)||owner!=root||
       !hud_member(caller,0x28,&owner)||owner!=root||!hud_member(argument,0x28,&owner)||owner!=caller||
       !hud_member(argument,0x64,&offset)||offset>65532||(offset&3)||
       !hud_read(function+0x2C,crosshair_metadata.name,8))return;
    uint32_t instigator=hud_property(root,"Instigator","ObjectProperty",4);
    if(!instigator)return;
    crosshair_metadata.function=function;crosshair_metadata.caller=caller;
    crosshair_metadata.argument=argument;crosshair_metadata.argument_offset=offset;
    crosshair_metadata.instigator=instigator;crosshair_metadata.root=root;
    if(!crosshair_bound) {
        void_dispatch_add(CROSSHAIR_TILE,crosshair_tile);void_dispatch_add(CROSSHAIR_POSITION,crosshair_position);
        crosshair_bound=1;
    }
}
static int hud_prepare(void)
{
    uint32_t root,kind;
    if(!hud_word(HUD_CLASS_GLOBAL,&root)||!hud_name(root,"UTHUD")||
       !hud_member(root,0x34,&kind)||!hud_class(kind,"Class"))return 0;
    if(hud_metadata.root==root) { crosshair_prepare();return 1; }
    memset(&hud_metadata,0,sizeof hud_metadata);
    uint32_t first=hud_field(root,"DrawGameHUD","Function"),second=hud_field(root,"DrawPostGameHud","Function");
    if(!first||!second||!hud_read(first+0x2C,hud_metadata.names[0],8)||
       !hud_read(second+0x2C,hud_metadata.names[1],8))return 0;
    hud_metadata.canvas=hud_property(root,"Canvas","ObjectProperty",4);
    hud_metadata.map=hud_property(root,"MapDefaultSize","FloatProperty",4);
    hud_metadata.portrait=hud_property(root,"CharPortraitSize","StructProperty",8);
    const char *names[]={"ResolutionScale","ResolutionScaleX","HUDScaleX","HUDScaleY"};
    for(unsigned i=0;i<4;++i) {
        hud_metadata.resolution[i]=hud_property(root,names[i],"FloatProperty",4);
        if(!hud_metadata.resolution[i])return 0;
    }
    if(!hud_metadata.canvas||!hud_metadata.map||!hud_metadata.portrait)return 0;
    uint32_t offsets[]={hud_metadata.canvas,hud_metadata.map,hud_metadata.portrait,
        hud_metadata.resolution[0],hud_metadata.resolution[1],hud_metadata.resolution[2],hud_metadata.resolution[3]};
    for(unsigned i=0;i<7;++i)for(unsigned j=0;j<i;++j)
        if(offsets[i]<offsets[j]+(j==2?8u:4u)&&offsets[j]<offsets[i]+(i==2?8u:4u))return 0;
    hud_metadata.root=root;crosshair_prepare();return 1;
}

/* The normal thread snapshot keeps the logical eight x87 registers. Calls
 * inserted by this feature must also preserve the whole sliding window/sp. */
typedef struct {
    void_regs regs;
    uint16_t segments[6];
    uint32_t gs,es;
    ptrdiff_t memory_base;
#ifdef RECOMP_X87_WINDOW
    double window[RECOMP_X87_SLOTS];
    int64_t integers[RECOMP_X87_SLOTS];
    int sp;
#endif
    uint8_t stack[HUD_STACK_BYTES];
    uint32_t stack_begin;
    int error;
} hud_call_state;
static int hud_save(hud_call_state *s)
{
    if(g_esp<HUD_STACK_BYTES+128)return 0;
    s->stack_begin=g_esp-HUD_STACK_BYTES;
    if(!void_vm_writable(s->stack_begin,HUD_STACK_BYTES)||
       !hud_read(s->stack_begin,s->stack,HUD_STACK_BYTES))return 0;
    void_regs_save(&s->regs);
    s->segments[0]=g_seg_cs;s->segments[1]=g_seg_ds;s->segments[2]=g_seg_es;
    s->segments[3]=g_seg_fs;s->segments[4]=g_seg_gs;s->segments[5]=g_seg_ss;
    s->gs=g_gs_base;s->es=g_es_base;s->memory_base=g_mem_base;s->error=errno;
#ifdef RECOMP_X87_WINDOW
    s->sp=g_fp_sp;memcpy(s->window,g_st_win,sizeof s->window);memcpy(s->integers,g_st_i64_win,sizeof s->integers);
#endif
    return 1;
}
static void hud_restore(const hud_call_state *s)
{
    hud_write(s->stack_begin,s->stack,HUD_STACK_BYTES);
#ifdef RECOMP_X87_WINDOW
    g_fp_sp=s->sp;memcpy(g_st_win,s->window,sizeof s->window);memcpy(g_st_i64_win,s->integers,sizeof s->integers);
#endif
    void_regs_load(&s->regs);
    g_seg_cs=s->segments[0];g_seg_ds=s->segments[1];g_seg_es=s->segments[2];
    g_seg_fs=s->segments[3];g_seg_gs=s->segments[4];g_seg_ss=s->segments[5];
    g_gs_base=s->gs;g_es_base=s->es;g_mem_base=s->memory_base;errno=s->error;
}
static _Thread_local struct {
    int active,busy,entry_valid,scaled;
    uint32_t object,cls,canvas,canvas_class,native,depth;
    uint32_t bounds[6],map,portrait[2],resolution[4];
    uint8_t entry[80];
} hud_scope;

static int hud_depth(uint32_t canvas,uint32_t *depth)
{
    uint32_t data,count,capacity;
    if(canvas>UINT32_MAX-0x30||!hud_word(canvas+0x24,&data)||!hud_word(canvas+0x28,&count)||
       !hud_word(canvas+0x2C,&capacity)||!data||!count||count>256||capacity<count||capacity>4096||
       count>(UINT32_MAX-data)/80||!void_vm_readable(data,count*80))return 0;
    *depth=count;return 1;
}
static int crosshair_simple(uint32_t weapon)
{
    uint32_t cls,pawn,pc,bits;
    if(!hud_member(weapon,0x34,&cls))return 0;
    if(crosshair_metadata.weapon_class!=cls) {
        crosshair_metadata.allowed=hud_class(cls,"UTWeapon")&&
            hud_field(cls,"DrawWeaponCrosshair","Function")==crosshair_metadata.function;
        crosshair_metadata.weapon_class=cls;
    }
    if(!crosshair_metadata.allowed||!hud_member(weapon,crosshair_metadata.instigator,&pawn)||
       !hud_member(pawn,0x34,&cls)||!hud_class(cls,"Pawn"))return 0;
    if(crosshair_metadata.pawn_class!=cls) {
        crosshair_metadata.controller=hud_property(cls,"Controller","ObjectProperty",4);
        crosshair_metadata.pawn_class=cls;
    }
    if(!crosshair_metadata.controller||!hud_member(pawn,crosshair_metadata.controller,&pc)||
       !hud_member(pc,0x34,&cls)||!hud_class(cls,"UTPlayerController"))return 0;
    if(crosshair_metadata.pc_class!=cls) {
        uint32_t field=hud_field(cls,"bSimpleCrosshair","BoolProperty"),offset=0,mask=0;
        crosshair_metadata.simple_offset=crosshair_metadata.simple_mask=0;
        if(field&&hud_member(field,0x64,&offset)&&offset>=0x3C&&offset<=65532&&!(offset&3)&&
           hud_member(field,0x84,&mask)&&mask&&!(mask&(mask-1))) {
            crosshair_metadata.simple_offset=offset;crosshair_metadata.simple_mask=mask;
        }
        crosshair_metadata.pc_class=cls;
    }
    return crosshair_metadata.simple_offset&&hud_member(pc,crosshair_metadata.simple_offset,&bits)&&
        (bits&crosshair_metadata.simple_mask)!=0;
}
static int crosshair_same(int physical)
{
    uint32_t cls,canvas,native,depth,data,origin,clip;
    uint8_t entry[80];
    return hud_member(crosshair_scope.hud,0x34,&cls)&&cls==crosshair_scope.hud_class&&
        hud_member(crosshair_scope.hud,hud_metadata.canvas,&canvas)&&canvas==crosshair_scope.canvas&&
        hud_member(canvas,0x34,&cls)&&cls==crosshair_scope.canvas_class&&
        hud_member(canvas,0x6C,&native)&&native==crosshair_scope.native&&
        hud_depth(native,&depth)&&depth==crosshair_scope.depth&&hud_word(native+0x24,&data)&&
        hud_read(data+(depth-1)*80,entry,sizeof entry)&&!memcmp(entry,crosshair_scope.entry,sizeof entry)&&
        hud_word(canvas+0x40,&origin)&&origin==(physical?crosshair_scope.bounds[0]:crosshair_scope.virtual_origin)&&
        hud_word(canvas+0x48,&clip)&&clip==(physical?crosshair_scope.bounds[2]:0x44800000u);
}
static int crosshair_enter(uint32_t weapon,uint32_t frame,uint32_t arguments)
{
    uint32_t node,object,property,locals,hud,cls,canvas,kind,native,depth,data;
    uint8_t expression[6];
    if(crosshair_scope.active||crosshair_scope.busy||!crosshair_metadata.root||!crosshair_simple(weapon)||
       !hud_member(frame,4,&node)||node!=crosshair_metadata.caller||!hud_member(frame,8,&object)||object!=weapon||
       !hud_read(arguments,expression,sizeof expression)||expression[0]!=0||expression[5]!=0x16)return 0;
    memcpy(&property,expression+1,4);
    if(property!=crosshair_metadata.argument||!hud_member(frame,0x10,&locals)||
       !hud_member(locals,crosshair_metadata.argument_offset,&hud)||!hud_member(hud,0x34,&cls)||!hud_class(cls,"UTHUD")||
       !hud_member(hud,hud_metadata.canvas,&canvas)||!hud_member(canvas,0x34,&kind)||!hud_class(kind,"Canvas")||
       !hud_member(canvas,0x6C,&native)||!hud_depth(native,&depth)||!hud_word(native+0x24,&data))return 0;
    uint32_t bounds[4];float values[4];
    if(!hud_read(canvas+0x40,bounds,sizeof bounds)||!void_vm_writable(canvas+0x40,4)||
       !void_vm_writable(canvas+0x48,4)||!void_vm_writable(canvas+0x50,4))return 0;
    memcpy(values,bounds,sizeof values);
    for(unsigned i=0;i<4;++i)if(!isfinite(values[i])||fabsf(values[i])>65536)return 0;
    if(values[2]<1||values[3]<1||values[2]==1024||
       !hud_read(data+(depth-1)*80,crosshair_scope.entry,sizeof crosshair_scope.entry))return 0;
    float delta=(values[2]-1024)*.5f,origin=values[0]+delta;
    if(!isfinite(origin))return 0;
    crosshair_scope.hud=hud;crosshair_scope.hud_class=cls;crosshair_scope.canvas=canvas;crosshair_scope.canvas_class=kind;
    crosshair_scope.native=native;crosshair_scope.depth=depth;crosshair_scope.delta=delta;
    memcpy(crosshair_scope.bounds,bounds,sizeof bounds);memcpy(&crosshair_scope.virtual_origin,&origin,4);
    uint32_t clip=0x44800000u; /* The original simple-crosshair width reference is1024. */
    hud_write(canvas+0x40,&origin,4);hud_write(canvas+0x48,&clip,4);
    if(!++crosshair_scope.generation)++crosshair_scope.generation;
    crosshair_scope.positioned=0;crosshair_scope.active=1;return 1;
}
static void crosshair_leave(void)
{
    if(!crosshair_scope.active)return;
    int saved_errno=errno;
    if(crosshair_same(crosshair_scope.busy)) {
        float x;
        if(!crosshair_scope.busy&&crosshair_scope.positioned&&hud_read(crosshair_scope.canvas+0x50,&x,4)&&isfinite(x)) {
            x+=crosshair_scope.delta;hud_write(crosshair_scope.canvas+0x50,&x,4);
        }
        hud_write(crosshair_scope.canvas+0x40,crosshair_scope.bounds,4);
        hud_write(crosshair_scope.canvas+0x48,crosshair_scope.bounds+2,4);
    }
    crosshair_scope.active=crosshair_scope.busy=0;errno=saved_errno;
}
static void crosshair_tile(void)
{
    if(!crosshair_scope.active||crosshair_scope.busy||g_ecx!=crosshair_scope.canvas) { crosshair_original_tile();return; }
    int saved_errno=errno,entered=0;
    uint64_t generation=crosshair_scope.generation;
    if(crosshair_scope.active&&!crosshair_scope.busy&&g_ecx==crosshair_scope.canvas&&
       void_machine_is_owned()&&crosshair_same(0)) {
        float x;
        if(hud_read(crosshair_scope.canvas+0x50,&x,4)) {
            if(isfinite(x))x+=crosshair_scope.delta;
            hud_write(crosshair_scope.canvas+0x40,crosshair_scope.bounds,4);
            hud_write(crosshair_scope.canvas+0x48,crosshair_scope.bounds+2,4);
            hud_write(crosshair_scope.canvas+0x50,&x,4);
            crosshair_scope.positioned=1;crosshair_scope.busy=1;entered=1;
        }
    }
    errno=saved_errno;crosshair_original_tile();
    if(entered&&crosshair_scope.active&&crosshair_scope.busy&&crosshair_scope.generation==generation) {
        saved_errno=errno;
        if(crosshair_same(1)) {
            float x;
            if(hud_read(crosshair_scope.canvas+0x50,&x,4)&&isfinite(x)) {
                x-=crosshair_scope.delta;hud_write(crosshair_scope.canvas+0x50,&x,4);
            }
            uint32_t clip=0x44800000u;
            hud_write(crosshair_scope.canvas+0x40,&crosshair_scope.virtual_origin,4);
            hud_write(crosshair_scope.canvas+0x48,&clip,4);
        } else crosshair_scope.active=0;
        crosshair_scope.busy=0;errno=saved_errno;
    }
}
static void crosshair_position(void)
{
    if(!crosshair_scope.active||crosshair_scope.busy||g_ecx!=crosshair_scope.canvas) { crosshair_original_position();return; }
    int saved_errno=errno;
    uint64_t generation=crosshair_scope.generation;
    int ours=crosshair_scope.active&&!crosshair_scope.busy&&g_ecx==crosshair_scope.canvas&&
        void_machine_is_owned()&&crosshair_same(0);
    errno=saved_errno;crosshair_original_position();
    saved_errno=errno;
    if(ours&&crosshair_scope.active&&crosshair_scope.generation==generation&&crosshair_same(0))crosshair_scope.positioned=1;
    errno=saved_errno;
}
static int hud_push_matrix(uint32_t native,float scale)
{
    hud_call_state saved;
    if(!hud_save(&saved))return 0;
    uint32_t matrix=(g_esp-96)&~15u,stack=matrix-16;
    float values[16]={scale,0,0,0,0,scale,0,0,0,0,1,0,0,0,0,1};
    uint32_t args[2]={RECOMP_RETADDR,matrix};
    hud_write(matrix,values,sizeof values);hud_write(stack,args,sizeof args);
    g_ecx=native;g_esp=stack;++hud_scope.busy;hud_push();--hud_scope.busy;
    hud_restore(&saved);return 1;
}
static void hud_leave(void)
{
    if(!hud_scope.active)return;
    hud_call_state saved;
    /* A frame can have been invalidated by a map/Surface change. Never write
     * through an old object identity, and never guess another canvas's depth. */
    uint32_t cls,canvas,native,depth,data;
    int same=hud_member(hud_scope.object,0x34,&cls)&&cls==hud_scope.cls&&
        hud_member(hud_scope.object,hud_metadata.canvas,&canvas)&&canvas==hud_scope.canvas&&
        hud_member(canvas,0x34,&cls)&&cls==hud_scope.canvas_class&&
        hud_member(canvas,0x6C,&native)&&native==hud_scope.native&&hud_depth(native,&depth)&&
        (hud_scope.scaled ? depth>hud_scope.depth&&depth-hud_scope.depth<=32 : depth==hud_scope.depth)&&
        hud_word(native+0x24,&data);
    if(same&&hud_scope.entry_valid) {
        uint8_t entry[80];
        uint32_t at=hud_scope.scaled ? hud_scope.depth : hud_scope.depth-1;
        same=hud_read(data+at*80,entry,sizeof entry)&&!memcmp(entry,hud_scope.entry,sizeof entry);
    }
    hud_scope.active=0;
    if(same) {
        if(hud_scope.scaled) {
            hud_write(canvas+0x40,hud_scope.bounds,16);hud_write(canvas+0x64,hud_scope.bounds+4,8);
            hud_write(hud_scope.object+hud_metadata.map,&hud_scope.map,4);
            hud_write(hud_scope.object+hud_metadata.portrait,hud_scope.portrait,8);
        }
        uint32_t current;
        /* A later original PostRender may already have recomputed the scale. */
        if(hud_member(hud_scope.object,hud_metadata.resolution[1],&current)&&current==hud_scope.resolution[0])
            hud_write(hud_scope.object+hud_metadata.resolution[1],hud_scope.resolution+1,4);
    }
    if(!same||!hud_scope.scaled||!hud_save(&saved))return;
    ++hud_scope.busy;
    for(unsigned budget=32;depth>hud_scope.depth&&budget--;) {
        uint32_t output=(g_esp-112)&~15u,stack=output-16,args[2]={RECOMP_RETADDR,output};
        hud_write(stack,args,sizeof args);g_ecx=native+0x24;g_esp=stack;hud_pop();
        if(!hud_depth(native,&depth))break;
        /* Every inserted call uses the same bounded scratch area. */
        g_esp=saved.regs.esp;
    }
    --hud_scope.busy;hud_restore(&saved);
}
static int hud_enter(uint32_t object)
{
    uint32_t cls,canvas,kind,native,depth;
    if(hud_scope.active||hud_scope.busy||object>UINT32_MAX-65536||!hud_member(object,0x34,&cls)||!hud_class(cls,"UTHUD")||
       !hud_member(object,hud_metadata.canvas,&canvas)||!hud_member(canvas,0x34,&kind)||!hud_class(kind,"Canvas")||
       !hud_member(canvas,0x6C,&native)||!hud_depth(native,&depth))return 0;
    uint32_t resolution[4];
    for(unsigned i=0;i<4;++i) {
        float value;
        if(!hud_member(object,hud_metadata.resolution[i],resolution+i))return 0;
        memcpy(&value,resolution+i,4);
        if(!isfinite(value)||value<=0||value>65536)return 0;
    }
    if(!void_vm_writable(object+hud_metadata.resolution[1],4))return 0;
    if(hud_scale==1.0f) {
        uint32_t data;
        if(resolution[0]==resolution[1]||!hud_word(native+0x24,&data)||
           !hud_read(data+(depth-1)*80,hud_scope.entry,sizeof hud_scope.entry))return 0;
        hud_scope.object=object;hud_scope.cls=cls;hud_scope.canvas=canvas;hud_scope.canvas_class=kind;
        hud_scope.native=native;hud_scope.depth=depth;memcpy(hud_scope.resolution,resolution,sizeof resolution);
        hud_scope.scaled=0;hud_scope.entry_valid=1;
        if(!hud_write(object+hud_metadata.resolution[1],resolution,4))return 0;
        hud_scope.active=1;return 1;
    }
    if(depth>=256||
       !void_vm_writable(canvas+0x40,16)||!void_vm_writable(canvas+0x64,8)||
       !void_vm_writable(object+hud_metadata.map,4)||!void_vm_writable(object+hud_metadata.portrait,8))return 0;
    uint32_t bounds[6],map,portrait[2];
    if(!hud_read(canvas+0x40,bounds,16)||!hud_read(canvas+0x64,bounds+4,8)||
       !hud_read(object+hud_metadata.map,&map,4)||!hud_read(object+hud_metadata.portrait,portrait,8))return 0;
    float values[4],map_value,portrait_values[2];
    memcpy(values,bounds,16);memcpy(&map_value,&map,4);memcpy(portrait_values,portrait,8);
    for(unsigned i=0;i<4;++i)if(!isfinite(values[i])||fabsf(values[i])>65536)return 0;
    if(values[2]<1||values[3]<1||!bounds[4]||!bounds[5]||bounds[4]>65536||bounds[5]>65536||
       !isfinite(map_value)||map_value<0||map_value>65536)return 0;
    for(unsigned i=0;i<2;++i)if(!isfinite(portrait_values[i])||portrait_values[i]<0||portrait_values[i]>65536)return 0;
    hud_scope.object=object;hud_scope.cls=cls;hud_scope.canvas=canvas;hud_scope.canvas_class=kind;
    hud_scope.native=native;hud_scope.depth=depth;memcpy(hud_scope.bounds,bounds,sizeof bounds);
    hud_scope.map=map;memcpy(hud_scope.portrait,portrait,sizeof portrait);memcpy(hud_scope.resolution,resolution,sizeof resolution);
    hud_scope.entry_valid=0;hud_scope.scaled=1;
    if(!hud_push_matrix(native,hud_scale))return 0;
    hud_scope.active=1;
    uint32_t pushed;
    if(!hud_depth(native,&pushed)||pushed!=depth+1) { hud_leave();return 0; }
    uint32_t data;
    if(!hud_word(native+0x24,&data)||!hud_read(data+depth*80,hud_scope.entry,sizeof hud_scope.entry)) { hud_leave();return 0; }
    hud_scope.entry_valid=1;
    for(unsigned i=0;i<4;++i)values[i]/=hud_scale;
    uint32_t sizes[2]={(uint32_t)((float)bounds[4]/hud_scale),(uint32_t)((float)bounds[5]/hud_scale)};
    if(!sizes[0])sizes[0]=1;if(!sizes[1])sizes[1]=1;
    map_value*=hud_scale;portrait_values[0]*=hud_scale;portrait_values[1]*=hud_scale;
    hud_write(canvas+0x40,values,16);hud_write(canvas+0x64,sizes,8);
    hud_write(object+hud_metadata.map,&map_value,4);hud_write(object+hud_metadata.portrait,portrait_values,8);
    hud_write(object+hud_metadata.resolution[1],resolution,4);
    return 1;
}
static int preview_enter(uint32_t hud)
{
    uint32_t preview,root,weapon_class,pawn,pawn_class,manager,manager_class,owner,previous;
    if(preview_scope.active||!void_touch_weapon_preview||!hud_metadata.root||
       !(preview=void_touch_weapon_preview()))return 0;
    if(!hud_word(WEAPON_CLASS_GLOBAL,&root)||!hud_name(root,"UTWeapon"))return 0;
    if(preview_metadata.hud!=hud_metadata.root||preview_metadata.weapon!=root) {
        memset(&preview_metadata,0,sizeof preview_metadata);
        preview_metadata.owner=hud_property(hud_metadata.root,"PawnOwner","ObjectProperty",4);
        preview_metadata.item_manager=hud_property(root,"InvManager","ObjectProperty",4);
        if(!preview_metadata.owner||!preview_metadata.item_manager)return 0;
        preview_metadata.hud=hud_metadata.root;preview_metadata.weapon=root;
    }
    if(!hud_member(preview,0x34,&weapon_class)||!hud_class(weapon_class,"UTWeapon")||
       !hud_member(hud,preview_metadata.owner,&pawn)||!pawn||!hud_member(pawn,0x34,&pawn_class))return 0;
    if(pawn_class!=preview_metadata.pawn_class) {
        uint32_t offset=hud_class(pawn_class,"Pawn") ? hud_property(pawn_class,"InvManager","ObjectProperty",4) : 0;
        if(!offset)return 0;
        preview_metadata.pawn_class=pawn_class;preview_metadata.pawn_manager=offset;
    }
    if(!hud_member(pawn,preview_metadata.pawn_manager,&manager)||!manager||
       !hud_member(preview,preview_metadata.item_manager,&owner)||owner!=manager||
       !hud_member(manager,0x34,&manager_class))return 0;
    if(manager_class!=preview_metadata.manager_class) {
        uint32_t offset=hud_class(manager_class,"InventoryManager") ?
            hud_property(manager_class,"PendingWeapon","ObjectProperty",4) : 0;
        if(!offset)return 0;
        preview_metadata.manager_class=manager_class;preview_metadata.pending=offset;
    }
    if(!hud_member(manager,preview_metadata.pending,&previous)||previous==preview||
       !hud_write(manager+preview_metadata.pending,&preview,4))return 0;
    preview_scope.manager=manager;preview_scope.previous=previous;preview_scope.preview=preview;
    preview_scope.active=1;
    return 1;
}
static void preview_leave(void)
{
    uint32_t current;
    if(!preview_scope.active)return;
    preview_scope.active=0;
    /* Only undo our own value; nothing else may have chosen a weapon meanwhile. */
    if(hud_member(preview_scope.manager,preview_metadata.pending,&current)&&current==preview_scope.preview)
        hud_write(preview_scope.manager+preview_metadata.pending,&preview_scope.previous,4);
}
static void hud_opcode(unsigned final)
{
    uint32_t frame,code,name[2],function,kind;
    int target=0,crosshair=0,game=0;
    if(!hud_scope.active&&!hud_scope.busy&&!crosshair_scope.active&&g_esp<=UINT32_MAX-8&&void_machine_is_owned()&&
       hud_word(g_esp+4,&frame)&&hud_member(frame,0x0C,&code)) {
        int named=final ? hud_word(code,&function)&&function<=UINT32_MAX-0x34&&hud_read(function+0x2C,name,8) : hud_read(code,name,8);
        game=named&&!memcmp(name,hud_metadata.names[0],8);
        target=game||(named&&!memcmp(name,hud_metadata.names[1],8));
        crosshair=named&&crosshair_metadata.root&&!memcmp(name,crosshair_metadata.name,8)&&
            (!final||function==crosshair_metadata.function)&&code<=UINT32_MAX-8;
        /* The common unmatched final call pays no class/name traversal. */
        if(target&&final)target=hud_member(function,0x34,&kind)&&hud_class(kind,"Function");
    }
    int entered=target&&hud_enter(g_ecx);
    int previewed=game&&target&&preview_enter(g_ecx);
    int cross_entered=crosshair&&crosshair_enter(g_ecx,frame,code+(final?4:8));
    uint64_t cross_generation=crosshair_scope.generation;
    hud_original[final]();
    if(cross_entered&&crosshair_scope.generation==cross_generation)crosshair_leave();
    if(previewed)preview_leave();
    if(entered)hud_leave();
}
static void hud_virtual(void) { hud_opcode(0); }
static void hud_final(void) { hud_opcode(1); }

int void_ui_hud_scale_observe(float scale)
{
    if(!void_machine_is_owned()||!hud_ready)return 0;
    hud_requested=isfinite(scale)&&scale>=1.0f&&scale<=1.5f ? scale : 1.0f;
    if(hud_scope.active||hud_scope.busy||crosshair_scope.active||crosshair_scope.busy) { hud_report("deferred");return 0; }
    if(!hud_prepare()) {
        if(hud_bound) {
            void_dispatch_add(HUD_VIRTUAL,hud_original[0]);void_dispatch_add(HUD_FINAL,hud_original[1]);hud_bound=0;
        }
        hud_scale=1.0f;hud_report("metadata-unavailable");return 0;
    }
    if(!hud_bound) {
        void_dispatch_add(HUD_VIRTUAL,hud_virtual);
        void_dispatch_add(HUD_FINAL,hud_final);
        hud_bound=1;
    }
    hud_scale=hud_requested;hud_report(hud_scale>1.0f ? "enabled" : "aspect");return 1;
}
/* The verified producer entry is a trusted boundary after any guest nonlocal
 * unwind. No ESP ordering assumption or modification of guest EH is needed. */
void void_ui_hud_frame_begin(void)
{
    if(!hud_ready||(!hud_scope.active&&!crosshair_scope.active&&!preview_scope.active&&hud_requested==hud_scale))return;
    if(!void_machine_is_owned())return;
    crosshair_leave();
    preview_leave();
    hud_leave();
    if(hud_requested!=hud_scale)void_ui_hud_scale_observe(hud_requested);
}
void void_ui_hud_init(void)
{
    static const uint8_t virtual_bytes[]={0x56,0x57,0x8B,0x7C,0x24,0x0C,0x8B,0x47,0x0C};
    static const uint8_t final_bytes[]={0x8B,0x44,0x24,0x04,0x8B,0x50,0x0C,0x56,0x8B,0x32};
    static const uint8_t push_bytes[]={0x55,0x8B,0xEC,0x83,0xE4,0xF0,0x81,0xEC,0x94,0,0,0};
    static const uint8_t pop_bytes[]={0x55,0x8B,0xEC,0x83,0xE4,0xF0,0x51,0x53,0x8B,0xD9};
    const uint32_t addresses[]={HUD_VIRTUAL,HUD_FINAL,HUD_PUSH,HUD_POP};
    const uint8_t *bytes[]={virtual_bytes,final_bytes,push_bytes,pop_bytes};
    const unsigned sizes[]={sizeof virtual_bytes,sizeof final_bytes,sizeof push_bytes,sizeof pop_bytes};
    void_fn functions[4];
    if(hud_ready)return;
    for(unsigned i=0;i<4;++i)if(!(functions[i]=void_dispatch_find(addresses[i]))||
       !void_vm_readable(addresses[i],sizes[i])||memcmp(GPTR(addresses[i]),bytes[i],sizes[i])) {
        void_log("[ui] HUD scale unavailable: original dispatch/code mismatch");return;
    }
    hud_original[0]=functions[0];hud_original[1]=functions[1];hud_push=functions[2];hud_pop=functions[3];
    static const uint8_t tile_bytes[]={0x83,0xEC,0x28,0x56,0x8B,0x74,0x24,0x30};
    if(void_vm_readable(CROSSHAIR_TILE,sizeof tile_bytes)&&!memcmp(GPTR(CROSSHAIR_TILE),tile_bytes,sizeof tile_bytes))
        crosshair_original_tile=void_dispatch_find(CROSSHAIR_TILE);
    static const uint8_t position_bytes[]={0x51,0x0F,0x57,0xC0,0x56,0x8B,0x74,0x24,0x0C};
    if(void_vm_readable(CROSSHAIR_POSITION,sizeof position_bytes)&&!memcmp(GPTR(CROSSHAIR_POSITION),position_bytes,sizeof position_bytes))
        crosshair_original_position=void_dispatch_find(CROSSHAIR_POSITION);
    hud_ready=1; /* Metadata enables aspect correction at the default scale too. */
    hud_report("ready");
}
