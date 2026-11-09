/* UE3 static draw-list traversal (TStaticMeshDrawList::DrawVisible): 00F058D0 and its sibling instantiations
 * 00F05530, 00F05700, 00F05AA0, 00EFE060 and 00EFE230, which differ only in the offset of the element array, the element
 * size, the per-element draw and the register allocation (render_draw_list.inc). The sparse iterator constructor
 * and the draw policy retain their original ABI; traversal and visibility use native loops. */
#include "host.h"
#include "native_mode.inc"

extern void __real_sub_00F058D0(void);
extern void __real_sub_00F05530(void);
extern void __real_sub_00F05700(void);
extern void __real_sub_00F05AA0(void);
extern void __real_sub_00EFE060(void);
extern void __real_sub_00EFE230(void);
extern void sub_005DAF50(void);
extern void sub_00B55380(void);
extern void sub_00F043E0(void);
extern void sub_00F042C0(void);
extern void sub_00F04350(void);
extern void sub_00F04470(void);
extern void sub_00EFB030(void);
extern void sub_00EFB0C0(void);

static int render_draw_mode = -1;

static __attribute__((cold, noinline)) int render_draw_mode_load(void)
{
    char path[1100];
    snprintf(path, sizeof path, "%s/render-draw-native", void_data_dir());
    FILE *f = fopen(path, "rb");
    const int value = f ? fgetc(f) : '1';
    if (f) fclose(f);
    /* Draws have external side effects; diagnostic mode 2 deliberately keeps the original. */
    render_draw_mode = void_native_mode(value == '0' || value == '2' ? 0 : 1);
    void_log("[native-render] draw-list traversal: mode %d", render_draw_mode);
    return render_draw_mode;
}

static inline void draw_flags(uint32_t kind, uint32_t a, uint32_t b, uint32_t carry)
{
    g_flag_k = kind; g_flag_a = a; g_flag_b = b; g_flag_cf = carry;
}

static inline void draw_call(recomp_func_t fn)
{
    PUSH32(g_esp, RECOMP_RETADDR);
    fn();
}

#define DL_NAME draw_lists_00f058d0
#define DL_LIST 0x28u
#define DL_STRIDE 160u
#define DL_CALLEE sub_00F043E0
#define DL_THIS g_edi
#define DL_ELEM g_esi
#include "render_draw_list.inc"
#undef DL_NAME
#undef DL_CALLEE

#define DL_NAME draw_lists_00f05700
#define DL_CALLEE sub_00F04350
#include "render_draw_list.inc"
#undef DL_NAME
#undef DL_LIST
#undef DL_CALLEE

#define DL_NAME draw_lists_00f05aa0
#define DL_LIST 0x30u
#define DL_CALLEE sub_00F04470
#include "render_draw_list.inc"
#undef DL_NAME
#undef DL_LIST
#undef DL_STRIDE
#undef DL_CALLEE
#undef DL_THIS
#undef DL_ELEM

#define DL_NAME draw_lists_00efe060
#define DL_LIST 0x18u
#define DL_STRIDE 16u
#define DL_CALLEE sub_00EFB030
#define DL_THIS g_esi
#define DL_ELEM g_edi
#include "render_draw_list.inc"
#undef DL_NAME
#undef DL_LIST
#undef DL_CALLEE

#define DL_NAME draw_lists_00efe230
#define DL_LIST 0x1Cu
#define DL_CALLEE sub_00EFB0C0
#include "render_draw_list.inc"
#undef DL_NAME
#undef DL_LIST
#undef DL_CALLEE

#define DL_NAME draw_lists_00f05530
#define DL_LIST 0x28u
#define DL_CALLEE sub_00F042C0
#include "render_draw_list.inc"

#define DRAW_WRAPPER(address, native) \
    void __wrap_sub_##address(void) { \
        const int mode = __builtin_expect(render_draw_mode >= 0, 1) ? render_draw_mode : render_draw_mode_load(); \
        if (!mode) __real_sub_##address(); \
        else native(); \
    }
DRAW_WRAPPER(00F058D0, draw_lists_00f058d0)
DRAW_WRAPPER(00F05530, draw_lists_00f05530)
DRAW_WRAPPER(00F05700, draw_lists_00f05700)
DRAW_WRAPPER(00F05AA0, draw_lists_00f05aa0)
DRAW_WRAPPER(00EFE060, draw_lists_00efe060)
DRAW_WRAPPER(00EFE230, draw_lists_00efe230)

void void_render_draw_native_init(void)
{
    void_dispatch_add(0x00F058D0u, __wrap_sub_00F058D0);
    void_dispatch_add(0x00F05530u, __wrap_sub_00F05530);
    void_dispatch_add(0x00F05700u, __wrap_sub_00F05700);
    void_dispatch_add(0x00F05AA0u, __wrap_sub_00F05AA0);
    void_dispatch_add(0x00EFE060u, __wrap_sub_00EFE060);
    void_dispatch_add(0x00EFE230u, __wrap_sub_00EFE230);
}
