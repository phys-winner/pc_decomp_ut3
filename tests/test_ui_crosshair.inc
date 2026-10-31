/* Production crosshair state/dispatch fixture; real native geometry is covered
 * separately by the original-PE Canvas VM/DrawTile oracle. */
enum { WEAPON_ROOT=0x160000,PAWN_CLASS=0x161000,PC_CLASS=0x162000,BOOL_KIND=0x163000,
       CROSS_FUNCTION=0x164000,CROSS_CALLER=0x164100,CROSS_ARGUMENT=0x164200,
       CROSS_INSTIGATOR=0x164300,CROSS_CONTROLLER=0x164400,CROSS_SIMPLE=0x164500,
       WEAPON_OBJECT=0x170000,PAWN_OBJECT=0x171000,PC_OBJECT=0x172000,LOCALS=0x173000,
       WEAPON_SUBCLASS=0x174000,OVERRIDE_FUNCTION=0x174100 };
static int cross_expect,cross_bypass,cross_none,cross_abort,cross_nested,cross_defer,cross_tiles,cross_positions;
static int cross_retire,cross_retired,cross_new_abort;
static jmp_buf cross_nested_escape;
static float cross_width,cross_height,cross_position_x,cross_position_y,cross_requested_width;
static uint32_t cross_flags;
static hud_call_state cross_position_state,cross_tile_state;
static void cross_native_position(void)
{
    uint32_t sp=g_esp;assert(g_ecx==CANVAS_OBJECT);++cross_positions;
    putf(CANVAS_OBJECT+0x50,cross_position_x);putf(CANVAS_OBJECT+0x54,cross_position_y);
    if(cross_retire==3) { cross_retire=0;cross_retired=1;void_ui_hud_frame_begin(); }
    seed(40);g_esp=sp+12;state(&cross_position_state);
}
static void cross_native_tile(void)
{
    uint32_t sp=g_esp;assert(g_ecx==CANVAS_OBJECT);++cross_tiles;
    assert(getf(CANVAS_OBJECT+0x40)==120&&getf(CANVAS_OBJECT+0x48)==physical_width);
    if(cross_expect)assert(crosshair_scope.active&&crosshair_scope.busy);
    if(cross_new_abort) { cross_new_abort=0;longjmp(cross_nested_escape,1); }
    if(cross_retire==1||cross_retire==2) {
        int replacement=cross_retire==2;cross_retire=0;cross_retired=1;void_ui_hud_frame_begin();
        if(replacement) {
            assert(crosshair_enter(WEAPON_OBJECT,FRAME,CODE+8));
            g_ecx=CANVAS_OBJECT;g_esp=sp;void_resolve(CROSSHAIR_POSITION)();
            cross_new_abort=1;
            if(!setjmp(cross_nested_escape)) { g_ecx=CANVAS_OBJECT;g_esp=sp;void_resolve(CROSSHAIR_TILE)();assert(0); }
            assert(crosshair_scope.active&&crosshair_scope.busy);
        }
    }
    if(cross_defer) { cross_defer=0;assert(!void_ui_hud_scale_observe(1.5f)); }
    if(cross_abort==3) { cross_abort=0;longjmp(escaped,1); }
    if(cross_nested) {
        cross_nested=0;g_ecx=CANVAS_OBJECT;void_resolve(CROSSHAIR_TILE)();
        assert(crosshair_scope.active&&crosshair_scope.busy);
    }
    putf(CANVAS_OBJECT+0x50,getf(CANVAS_OBJECT+0x50)+cross_requested_width);
    putf(CANVAS_OBJECT+0x58,cross_height);
    seed(41);g_esp=sp+12;state(&cross_tile_state);
}
static void cross_body(int final)
{
    uint32_t sp=g_esp,frame=MEM32(sp+4);++calls;
    put(frame+0x0C,MEM32(frame+0x0C)+(final?4:8)+6);
    if(cross_bypass) {
        assert(!crosshair_scope.active&&getf(CANVAS_OBJECT+0x40)==120&&getf(CANVAS_OBJECT+0x48)==physical_width);
    } else {
        assert(crosshair_scope.active==cross_expect);
        if(cross_expect)assert(getf(CANVAS_OBJECT+0x48)==1024&&!pushes&&!pops);
        if(cross_abort==1) { cross_abort=0;longjmp(escaped,1); }
        if(!cross_none) {
            float clip=getf(CANVAS_OBJECT+0x48);
            cross_requested_width=cross_width*((cross_flags&0x20)?clip/1024:1);
            float x=clip*.5f-cross_requested_width*.5f,y=1080*.5f-cross_height*.5f;
            for(unsigned shadow=1;shadow<2;--shadow) {
                uint32_t native_sp=sp-0x400;
                cross_position_x=x+(float)shadow;cross_position_y=y+(float)shadow;
                put(native_sp,RECOMP_RETADDR);put(native_sp+4,NEST_FRAME);put(native_sp+8,0);
                g_esp=native_sp;g_ecx=CANVAS_OBJECT;void_resolve(CROSSHAIR_POSITION)();
                hud_call_state after;state(&after);assert(!memcmp(&after,&cross_position_state,sizeof after));
                if(cross_retired)break;
                if(cross_abort==2) { cross_abort=0;longjmp(escaped,1); }
                g_esp=native_sp;g_ecx=CANVAS_OBJECT;void_resolve(CROSSHAIR_TILE)();
                state(&after);assert(!memcmp(&after,&cross_tile_state,sizeof after));
                if(cross_retired)break;
                if(cross_abort==4) { cross_abort=0;longjmp(escaped,1); }
            }
        }
    }
    seed(33);g_esp=sp+12;
}
static void cross_virtual_body(void) { cross_body(0); }
static void cross_final_body(void) { cross_body(1); }
static void cross_fixture(int final,int simple)
{
    fixture();hud_ready=0;
    cross_expect=cross_bypass=cross_none=cross_abort=cross_nested=cross_defer=cross_tiles=cross_positions=0;
    cross_retire=cross_retired=cross_new_abort=0;
    cross_width=cross_height=45;cross_flags=0x80000000u|(simple?0x20u:0);
    object_name(WEAPON_ROOT,"UTWeapon");object_name(PAWN_CLASS,"Pawn");object_name(PC_CLASS,"UTPlayerController");
    object_name(BOOL_KIND,"BoolProperty");put(WEAPON_ROOT+0x34,CLASS_KIND);
    put(WEAPON_CLASS_GLOBAL,WEAPON_ROOT);put(WEAPON_ROOT+0x4C,CROSS_FUNCTION);
    field(CROSS_FUNCTION,"DrawWeaponCrosshair",FUNCTION_KIND,0,CROSS_CALLER);put(CROSS_FUNCTION+0x28,WEAPON_ROOT);
    field(CROSS_CALLER,"ActiveRenderOverlays",FUNCTION_KIND,0,CROSS_INSTIGATOR);put(CROSS_CALLER+0x28,WEAPON_ROOT);
    put(CROSS_CALLER+0x4C,CROSS_ARGUMENT);
    field(CROSS_ARGUMENT,"H",OBJECT_KIND,0,0);put(CROSS_ARGUMENT+0x28,CROSS_CALLER);
    field(CROSS_INSTIGATOR,"Instigator",OBJECT_KIND,0x600,0);
    put(PAWN_CLASS+0x4C,CROSS_CONTROLLER);field(CROSS_CONTROLLER,"Controller",OBJECT_KIND,0x500,0);
    put(PC_CLASS+0x4C,CROSS_SIMPLE);field(CROSS_SIMPLE,"bSimpleCrosshair",BOOL_KIND,0x300,0);
    put(CROSS_SIMPLE+0x84,0x20);put(CROSS_SIMPLE+0x70,0x40000000); /* Wrong guessed offset is a decoy. */
    put(WEAPON_OBJECT+0x34,WEAPON_ROOT);put(WEAPON_OBJECT+0x600,PAWN_OBJECT);
    put(PAWN_OBJECT+0x34,PAWN_CLASS);put(PAWN_OBJECT+0x500,PC_OBJECT);put(PC_OBJECT+0x34,PC_CLASS);
    put(PC_OBJECT+0x300,cross_flags);put(LOCALS,HUD_OBJECT);
    put(FRAME+4,CROSS_CALLER);put(FRAME+8,WEAPON_OBJECT);put(FRAME+0x10,LOCALS);put(FRAME+0x0C,CODE);
    uint32_t at=CODE+(final?4:8);
    if(final)put(CODE,CROSS_FUNCTION);else memcpy(GPTR(CODE),GPTR(CROSS_FUNCTION+0x2C),8);
    *(uint8_t*)GPTR(at)=0;put(at+1,CROSS_ARGUMENT);*(uint8_t*)GPTR(at+5)=0x16;
    memcpy(GPTR(CROSSHAIR_TILE),(uint8_t[]){0x83,0xEC,0x28,0x56,0x8B,0x74,0x24,0x30},8);
    memcpy(GPTR(CROSSHAIR_POSITION),(uint8_t[]){0x51,0x0F,0x57,0xC0,0x56,0x8B,0x74,0x24,0x0C},9);
    void_dispatch_add(HUD_VIRTUAL,cross_virtual_body);void_dispatch_add(HUD_FINAL,cross_final_body);
    void_dispatch_add(CROSSHAIR_TILE,cross_native_tile);void_dispatch_add(CROSSHAIR_POSITION,cross_native_position);
    void_ui_hud_init();assert(void_ui_hud_scale_observe(1));
    assert(crosshair_metadata.root==WEAPON_ROOT&&crosshair_bound);
    seed(1);g_esp=GUEST_STACK;g_ecx=WEAPON_OBJECT;
}
static void cross_returned(void)
{
    assert(!crosshair_scope.active&&!crosshair_scope.busy&&!pushes&&!pops);
    assert(getf(CANVAS_OBJECT+0x40)==120&&getf(CANVAS_OBJECT+0x48)==physical_width);
    assert(MEM32(PC_OBJECT+0x300)==cross_flags);
}
static void run_crosshair_tests(void)
{
    hud_call_state reference,current;
    uint8_t canvas[128];
    cross_fixture(0,0);void_resolve(HUD_VIRTUAL)();state(&reference);memcpy(canvas,GPTR(CANVAS_OBJECT),sizeof canvas);
    assert(cross_tiles==2&&cross_positions==2);cross_returned();
    cross_fixture(0,1);cross_expect=1;void_resolve(HUD_VIRTUAL)();state(&current);cross_returned();
    assert(!memcmp(&reference,&current,sizeof reference)&&!memcmp(canvas,GPTR(CANVAS_OBJECT),sizeof canvas));
    assert(cross_tiles==2&&cross_positions==2);
    for(unsigned final=0;final<2;++final)for(unsigned width=0;width<4;++width)for(unsigned huge=0;huge<2;++huge) {
        cross_fixture((int)final,1);cross_expect=1;physical_width=(float[]){800,1440,1920,2400}[width];
        putf(CANVAS_OBJECT+0x48,physical_width);cross_width=huge?4000:45;
        void_resolve(final?HUD_FINAL:HUD_VIRTUAL)();cross_returned();assert(cross_tiles==2&&cross_positions==2);
        assert(fabsf(getf(CANVAS_OBJECT+0x50)-(physical_width*.5f+cross_width*.5f))<.001f);
    }
    cross_fixture(0,1);cross_expect=1;cross_none=1;
    putf(CANVAS_OBJECT+0x50,333);void_resolve(HUD_VIRTUAL)();cross_returned();
    assert(!cross_tiles&&!cross_positions&&getf(CANVAS_OBJECT+0x50)==333);
    cross_fixture(0,1);cross_expect=1;cross_nested=1;void_resolve(HUD_VIRTUAL)();cross_returned();assert(cross_tiles==3);
    cross_fixture(0,1);cross_expect=1;cross_defer=1;void_resolve(HUD_VIRTUAL)();cross_returned();
    assert(hud_requested==1.5f&&hud_scale==1);void_ui_hud_frame_begin();assert(hud_scale==1.5f);
    cross_fixture(0,1);cross_expect=1;assert(void_ui_hud_scale_observe(1.5f));
    void_resolve(HUD_VIRTUAL)();cross_returned();assert(getf(CANVAS_OBJECT+0x50)==physical_width*.5f+cross_width*.5f);
    for(unsigned phase=1;phase<=4;++phase) {
        cross_fixture(0,1);cross_expect=1;cross_abort=(int)phase;
        if(!setjmp(escaped)) { void_resolve(HUD_VIRTUAL)();assert(0); }
        assert(crosshair_scope.active);
        seed(55);g_esp=GUEST_STACK+0x800;state(&reference);void_ui_hud_frame_begin();state(&current);
        assert(!memcmp(&reference,&current,sizeof reference));cross_returned();
        float expected=phase==1?0:physical_width*.5f-cross_width*.5f+1+(phase==4?cross_width:0);
        assert(getf(CANVAS_OBJECT+0x50)==expected);
    }
    for(unsigned reason=0;reason<13;++reason) {
        cross_fixture(0,1);cross_bypass=1;
        if(reason==0) *(uint8_t*)GPTR(CODE+8)=1;
        else if(reason==1) *(uint8_t*)GPTR(CODE+13)=0;
        else if(reason==2)put(CODE+9,CROSS_ARGUMENT+4);
        else if(reason==3)put(FRAME+4,CROSS_FUNCTION);
        else if(reason==4)put(FRAME+8,HUD_OBJECT);
        else if(reason==5)put(LOCALS,CANVAS_OBJECT);
        else if(reason==6)readonly=CANVAS_OBJECT+0x50;
        else if(reason==7)owned=0;
        else if(reason==8) {
            object_name(WEAPON_SUBCLASS,"CustomWeapon");put(WEAPON_SUBCLASS+0x3C,WEAPON_ROOT);
            put(WEAPON_SUBCLASS+0x4C,OVERRIDE_FUNCTION);field(OVERRIDE_FUNCTION,"DrawWeaponCrosshair",FUNCTION_KIND,0,0);
            put(WEAPON_OBJECT+0x34,WEAPON_SUBCLASS);
        } else if(reason==9)put(CROSS_SIMPLE+0x84,0x30);
        else if(reason==10)put(CROSS_SIMPLE+0x34,FLOAT_KIND);
        else if(reason==11)put(CROSS_SIMPLE+0x64,65536);
        else { put(CROSS_ARGUMENT+0x28,CROSS_FUNCTION);crosshair_metadata.root=0;crosshair_prepare(); }
        uint8_t before[128];memcpy(before,GPTR(CANVAS_OBJECT),sizeof before);
        void_resolve(HUD_VIRTUAL)();assert(!memcmp(before,GPTR(CANVAS_OBJECT),sizeof before));
        assert(!crosshair_scope.active&&!cross_tiles&&!cross_positions&&calls==1);
    }
    for(unsigned reason=0;reason<3;++reason) {
        cross_fixture(0,1);cross_expect=1;cross_abort=2;putf(ENTRIES,1);
        if(!setjmp(escaped)) { void_resolve(HUD_VIRTUAL)();assert(0); }
        if(reason==0)putf(ENTRIES,2);
        else if(reason==1)put(HUD_OBJECT+0x500,CANVAS_OBJECT+0x200);
        else put(CANVAS_OBJECT+0x34,ROOT_CLASS);
        uint8_t before[128];memcpy(before,GPTR(CANVAS_OBJECT),sizeof before);
        seed(55);g_esp=GUEST_STACK+0x800;void_ui_hud_frame_begin();
        assert(!crosshair_scope.active&&!crosshair_scope.busy&&!memcmp(before,GPTR(CANVAS_OBJECT),sizeof before));
    }
    for(unsigned replacement=0;replacement<3;++replacement) {
        cross_fixture(0,1);cross_expect=1;cross_retire=(int)replacement+1;
        void_resolve(HUD_VIRTUAL)();
        assert(getf(CANVAS_OBJECT+0x40)==120&&getf(CANVAS_OBJECT+0x48)==physical_width);
        if(replacement==1) {
            assert(crosshair_scope.active&&crosshair_scope.busy&&crosshair_scope.generation==2);
            void_ui_hud_frame_begin();cross_returned();
        } else assert(!crosshair_scope.active&&!crosshair_scope.busy&&crosshair_scope.positioned==(replacement==0));
    }
    puts("PASS production simple-crosshair: exact base/caller/local-H and typed BoolProperty+84 guard; normalreticle complete Canvas/gueststate byteidentical; 16viewport/oversize cases; original physicalclip/UV boundary, selectedstyle/100percent/userHUD independence");
    puts("PASS production simple-crosshair: originalSetPos/DrawTile exactlyonce, nestedtile, noimage, deferredscale, four nonlocal cleanup phases, 13malformed/owner/override bypasses and three foreignidentity no-write guards");
    puts("PASS production simple-crosshair: trustedcleanup then nativeSetPos/DrawTile return cannot resurrect virtualbounds or overwrite a replacement TLS generation");
}
