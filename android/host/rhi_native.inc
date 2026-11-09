/* Native FD3D9DynamicRHI methods (UT3.exe), included by d3d9_device.c.
 *
 * UE3's D3D9 RHI sets state through thin virtual methods that make one D3D9 COM call per value: a
 * texture with its sampler state is SetTexture plus eight SetSamplerState calls, a blend state eleven
 * SetRenderState calls (research/d3d9-rhi-map-2026-11-08.md). Each COM call crosses a guest vtable,
 * an indirect-call site cache, a host thunk that decodes its arguments from the guest stack and the
 * device's COM lookup. These replacements read the RHI's state objects once and apply the same values,
 * in the same order, through the device API the COM methods use (api_set_*): the device state and what
 * the render thread receives are those of the COM path. The draw methods (015979B0, 01597840) add the
 * RHI's three statistics counters and SetIndices and call the device's draw directly.
 *
 * They act only on this file's GLES device. A DXVK device (another COM class), a state block being
 * recorded, an active trace, a null state object (the original faults reading it), an out-of-range
 * sampler or stream, a missing pixel shader (the original builds the default one) or a non-finite
 * depth bias run the lifted original, which calls through COM. The methods are reached only through
 * the RHI's vtable (01BA8A24..01BA8AAC; UT3.exe has no direct call), thiscall, and return the last COM
 * result in EAX. EBX/ESI/EDI/EBP, x87 and XMM are untouched; ECX/EDX/flags are scratch at a virtual
 * call site.
 *
 * rhi-native: absent/1 = native (the default), 0 = original, 2 = original, then the device state and the
 * draw are compared with what the native mapping does ([rhi] mismatch lines). Exact mode keeps the originals.
 * Measured 2026-11-08 (WAR-Torlan, 32 bots, three pairs, build against the COM path of the build before): -2.2%
 * game-thread cycles and -4.6% instructions per frame; a paused scene is pixel-identical between the paths. */
#include "native_mode.inc"

extern void sub_01596C80(void), sub_01596FD0(void), sub_01596EA0(void), sub_01596E40(void), sub_01596900(void);
extern void sub_01597200(void), sub_01597230(void), sub_01596890(void), sub_01597E70(void), sub_01596D70(void);
extern void sub_01596DA0(void), sub_01596DD0(void), sub_015979B0(void), sub_01597840(void), sub_01597B50(void), sub_01597CD0(void);

enum { RHI_SAMPLER, RHI_BLEND, RHI_STENCIL, RHI_DEPTH, RHI_RASTER, RHI_COLOR_ENABLE, RHI_COLOR_MASK, RHI_STREAM,
       RHI_SHADERS, RHI_VS_CONST, RHI_PS_CONST, RHI_VIEW_CONST, RHI_DRAW_INDEXED, RHI_DRAW, RHI_DRAW_UP, RHI_DRAW_INDEXED_UP,
       RHI_COUNT };
static const char *const rhi_names[RHI_COUNT] = {
    "SetSamplerState", "SetBlendState", "SetStencilState", "SetDepthState", "SetRasterizerState",
    "SetColorWriteEnable", "SetColorWriteMask", "SetStreamSource", "SetBoundShaderState",
    "SetVertexShaderParameter", "SetPixelShaderParameter", "SetViewParameters", "DrawIndexedPrimitive", "DrawPrimitive",
    "DrawPrimitiveUP", "DrawIndexedPrimitiveUP" };
static uint64_t rhi_calls[RHI_COUNT], rhi_native_calls[RHI_COUNT], rhi_mismatches[RHI_COUNT];

/* The RHI's device (FD3D9DynamicRHI::Direct3DDevice at +0xC) when it is this file's and not recording. */
static inline d3d_device *rhi_device(uint32_t rhi)
{
    d3d_device *const d = g_d3d_device;     /* the one device of this file; a DXVK device never matches it */
    return d && d->com.guest == MEM32(rhi + 0xC) && !d->recording ? d : NULL;
}

/* thiscall return: EAX, then the return address and the arguments popped. */
static inline void rhi_return(uint32_t sp, uint32_t argument_bytes, uint32_t result)
{
    g_eax = result;
    g_esp = sp + 4u + argument_bytes;
}

static __attribute__((noinline, cold)) void rhi_mismatch(int which, const char *what, uint32_t index, uint32_t have,
                                                         uint32_t want)
{
    if (rhi_mismatches[which]++ < 8)
        void_log("[rhi] mismatch %s: %s %u is %08X, the native mapping sets %08X", rhi_names[which], what, index,
                 have, want);
}

static inline void rhi_check(int which, const char *what, uint32_t index, uint32_t have, uint32_t want)
{
    if (__builtin_expect(have != want, 0)) rhi_mismatch(which, what, index, have, want);
}

/* Render-state lists: the D3DRENDERSTATETYPE and the dword of the RHI state object (counted from the
 * pointer the RHI passes, which is 8 bytes into the object) in the original's call order. */
typedef struct { uint8_t state, field; } rhi_rs;
static const rhi_rs rhi_blend[11] = {   /* 01596FD0 */
    {27, 0}, {171, 1}, {19, 2}, {20, 3}, {206, 4}, {209, 5}, {207, 6}, {208, 7}, {15, 8}, {25, 9}, {24, 10} };
static const rhi_rs rhi_stencil[13] = { /* 01596EA0 */
    {52, 0}, {56, 2}, {53, 3}, {54, 4}, {55, 5}, {185, 1}, {189, 6}, {186, 7}, {187, 8}, {188, 9}, {58, 10},
    {59, 11}, {57, 12} };
static const rhi_rs rhi_depth[3] = {    /* 01596E40 */
    {7, 0}, {14, 1}, {23, 2} };

static void rhi_render_states(int which, void_fn original, const rhi_rs *list, unsigned count)
{
    const uint32_t sp = g_esp, rhi = g_ecx, object = MEM32(sp + 4);
    d3d_device *d;
    ++rhi_calls[which];
    if (rhi_mode != 1 || !object || !(d = rhi_device(rhi))) {
        original();
        if (rhi_mode == 2 && object && (d = rhi_device(rhi)))
            for (unsigned i = 0; i < count; ++i)
                rhi_check(which, "render state", list[i].state, d->st.rs[list[i].state], MEM32(object + 4u * list[i].field));
        return;
    }
    ++rhi_native_calls[which];
    for (unsigned i = 0; i < count; ++i) api_set_render_state(d, list[i].state, MEM32(object + 4u * list[i].field));
    rhi_return(sp, 4, D3D_OK);
}

static void rhi_set_blend_state(void) { rhi_render_states(RHI_BLEND, sub_01596FD0, rhi_blend, 11); }
static void rhi_set_stencil_state(void) { rhi_render_states(RHI_STENCIL, sub_01596EA0, rhi_stencil, 13); }
static void rhi_set_depth_state(void) { rhi_render_states(RHI_DEPTH, sub_01596E40, rhi_depth, 3); }

/* 01596900: FILLMODE, CULLMODE, DEPTHBIAS = DepthBias + the global bias at 01DF1368 (addss, in that
 * operand order), SLOPESCALEDEPTHBIAS. */
#define RHI_DEPTH_BIAS_OFFSET 0x01DF1368u
static int rhi_raster_bias(uint32_t object, uint32_t *bits)
{
    float bias, offset, sum;
    const uint32_t a = MEM32(object + 8), b = MEM32(RHI_DEPTH_BIAS_OFFSET);
    memcpy(&bias, &a, 4);
    memcpy(&offset, &b, 4);
    sum = bias + offset;
    if (!isfinite(bias) || !isfinite(offset) || !isfinite(sum)) return 0;   /* NaN payloads: the original */
    memcpy(bits, &sum, 4);
    return 1;
}

static void rhi_set_rasterizer_state(void)
{
    const uint32_t sp = g_esp, rhi = g_ecx, object = MEM32(sp + 4);
    uint32_t bias = 0;
    d3d_device *d;
    ++rhi_calls[RHI_RASTER];
    if (rhi_mode != 1 || !object || !rhi_raster_bias(object, &bias) || !(d = rhi_device(rhi))) {
        sub_01596900();
        if (rhi_mode == 2 && object && rhi_raster_bias(object, &bias) && (d = rhi_device(rhi))) {
            rhi_check(RHI_RASTER, "render state", 8, d->st.rs[8], MEM32(object));
            rhi_check(RHI_RASTER, "render state", 22, d->st.rs[22], MEM32(object + 4));
            rhi_check(RHI_RASTER, "render state", 195, d->st.rs[195], bias);
            rhi_check(RHI_RASTER, "render state", 175, d->st.rs[175], MEM32(object + 12));
        }
        return;
    }
    ++rhi_native_calls[RHI_RASTER];
    api_set_render_state(d, 8, MEM32(object));
    api_set_render_state(d, 22, MEM32(object + 4));
    api_set_render_state(d, 195, bias);
    api_set_render_state(d, 175, MEM32(object + 12));
    rhi_return(sp, 4, D3D_OK);
}

/* 01597200 COLORWRITEENABLE = bEnable ? 0xF : 0; 01597230 COLORWRITEENABLE = mask & 0xF. */
static void rhi_color_write(int which, void_fn original, uint32_t value)
{
    const uint32_t sp = g_esp, rhi = g_ecx;
    d3d_device *d;
    ++rhi_calls[which];
    if (rhi_mode != 1 || !(d = rhi_device(rhi))) {
        original();
        if (rhi_mode == 2 && (d = rhi_device(rhi))) rhi_check(which, "render state", 168, d->st.rs[168], value);
        return;
    }
    ++rhi_native_calls[which];
    api_set_render_state(d, 168, value);
    rhi_return(sp, 4, D3D_OK);
}
static void rhi_set_color_write_enable(void) { rhi_color_write(RHI_COLOR_ENABLE, sub_01597200, MEM32(g_esp + 4) ? 0xFu : 0u); }
static void rhi_set_color_write_mask(void) { rhi_color_write(RHI_COLOR_MASK, sub_01597230, MEM32(g_esp + 4) & 0xFu); }

/* 01596C80 (Frequency, TextureIndex, ?, SamplerState, Texture): SetTexture(TextureIndex, the texture's
 * IDirect3DBaseTexture9 at Texture - 4), then SRGBTEXTURE = Texture[0] & 1 and MAGFILTER, MINFILTER,
 * MIPFILTER, ADDRESSU, ADDRESSV, ADDRESSW, MIPMAPLODBIAS from SamplerState[0..6]. */
static const uint8_t rhi_sampler_types[7] = { 5, 6, 7, 1, 2, 3, 8 };

static void rhi_set_sampler_state(void)
{
    const uint32_t sp = g_esp, rhi = g_ecx, sampler = MEM32(sp + 8), state = MEM32(sp + 16), texture = MEM32(sp + 20);
    d3d_device *d;
    ++rhi_calls[RHI_SAMPLER];
    if (rhi_mode != 1 || !state || !texture || sampler_slot(sampler) < 0 || d3d_trace_active() || !(d = rhi_device(rhi))) {
        sub_01596C80();
        if (rhi_mode == 2 && state && texture && sampler_slot(sampler) >= 0 && (d = rhi_device(rhi))) {
            const int slot = sampler_slot(sampler);
            const uint32_t guest = MEM32(texture - 4);
            const void_com_object *bound = (const void_com_object *)d->st.textures[slot];
            rhi_check(RHI_SAMPLER, "texture of sampler", sampler, bound ? bound->guest : 0, guest);
            rhi_check(RHI_SAMPLER, "SRGBTEXTURE of sampler", sampler, d->st.ss[slot][11], MEM32(texture) & 1u);
            for (unsigned i = 0; i < 7; ++i)
                rhi_check(RHI_SAMPLER, "sampler state", rhi_sampler_types[i], d->st.ss[slot][rhi_sampler_types[i]],
                          MEM32(state + 4u * i));
        }
        return;
    }
    ++rhi_native_calls[RHI_SAMPLER];
    api_set_texture(d, sampler, MEM32(texture - 4));
    api_set_sampler_state(d, sampler, 11, MEM32(texture) & 1u);
    for (unsigned i = 0; i < 7; ++i) api_set_sampler_state(d, sampler, rhi_sampler_types[i], MEM32(state + 4u * i));
    rhi_return(sp, 0x14, D3D_OK);
}

/* 01596890 (StreamIndex, VertexBuffer, Stride, bUseInstanceIndex, NumVerticesPerInstance, NumInstances):
 * SetStreamSource(StreamIndex, VertexBuffer - 4 or 0, 0, Stride), then SetStreamSourceFreq(StreamIndex,
 * INSTANCEDATA|1 with bUseInstanceIndex, else INDEXEDDATA|NumInstances above one instance, else 1). */
static inline uint32_t rhi_stream_frequency(uint32_t instance_index, uint32_t instances)
{
    return instance_index ? 0x80000001u : instances > 1 ? (instances | 0x40000000u) : 1u;
}

static void rhi_set_stream_source(void)
{
    const uint32_t sp = g_esp, rhi = g_ecx, stream = MEM32(sp + 4), buffer = MEM32(sp + 8), stride = MEM32(sp + 12);
    const uint32_t frequency = rhi_stream_frequency(MEM32(sp + 16), MEM32(sp + 24));
    const uint32_t guest = buffer ? buffer - 4u : 0;
    d3d_device *d;
    ++rhi_calls[RHI_STREAM];
    if (rhi_mode != 1 || stream >= 16 || !(d = rhi_device(rhi))) {
        sub_01596890();
        if (rhi_mode == 2 && stream < 16 && (d = rhi_device(rhi))) {
            const void_com_object *bound = (const void_com_object *)d->st.streams[stream];
            rhi_check(RHI_STREAM, "buffer of stream", stream, bound ? bound->guest : 0, guest);
            rhi_check(RHI_STREAM, "offset of stream", stream, d->st.stream_offset[stream], 0);
            rhi_check(RHI_STREAM, "stride of stream", stream, d->st.stream_stride[stream], stride);
            rhi_check(RHI_STREAM, "frequency of stream", stream, d->st.stream_freq[stream], frequency);
        }
        return;
    }
    ++rhi_native_calls[RHI_STREAM];
    api_set_stream_source(d, stream, guest, 0, stride);
    rhi_return(sp, 0x18, api_set_stream_source_freq(d, stream, frequency));
}

/* 01597E70 (BoundShaderState): SetVertexDeclaration, SetVertexShader, SetPixelShader from its
 * VertexDeclaration, VertexShader and PixelShader (the original asserts without a declaration and
 * builds the default pixel shader without one: both stay with it). */
static void rhi_set_bound_shader_state(void)
{
    const uint32_t sp = g_esp, rhi = g_ecx, object = MEM32(sp + 4);
    const uint32_t decl = object ? MEM32(object) : 0, vs = object ? MEM32(object + 4) : 0;
    const uint32_t ps = object ? MEM32(object + 8) : 0;
    d3d_device *d;
    ++rhi_calls[RHI_SHADERS];
    if (rhi_mode != 1 || !decl || !ps || !(d = rhi_device(rhi))) {
        sub_01597E70();
        if (rhi_mode == 2 && decl && ps && (d = rhi_device(rhi))) {
            const void_com_object *o;
            o = (const void_com_object *)d->st.decl; rhi_check(RHI_SHADERS, "declaration", 0, o ? o->guest : 0, decl);
            o = (const void_com_object *)d->st.vs; rhi_check(RHI_SHADERS, "vertex shader", 0, o ? o->guest : 0, vs);
            o = (const void_com_object *)d->st.ps; rhi_check(RHI_SHADERS, "pixel shader", 0, o ? o->guest : 0, ps);
        }
        return;
    }
    ++rhi_native_calls[RHI_SHADERS];
    api_set_vertex_declaration(d, decl);
    api_set_vertex_shader(d, vs);
    rhi_return(sp, 4, api_set_pixel_shader(d, ps));
}

/* 01596D70 / 01596DA0 (Shader, BufferIndex, BaseIndex, NumBytes, NewValue):
 * Set{Vertex,Pixel}ShaderConstantF(BaseIndex >> 4, NewValue, (NumBytes + 15) >> 4). */
static void rhi_shader_parameter(int which, void_fn original, int pixel)
{
    const uint32_t sp = g_esp, rhi = g_ecx;
    const uint32_t start = MEM32(sp + 12) >> 4, count = (MEM32(sp + 16) + 15u) >> 4, data = MEM32(sp + 20);
    d3d_device *d;
    ++rhi_calls[which];
    if (rhi_mode != 1 || (pixel && d3d_trace_active()) || !(d = rhi_device(rhi))) {
        original();
        if (rhi_mode == 2 && (d = rhi_device(rhi)) && start + count <= (pixel ? 224u : 256u)) {
            const uint8_t *have = (const uint8_t *)(pixel ? d->st.ps_f[start] : d->st.vs_f[start]);
            if (memcmp(have, GPTR(data), (size_t)count * 16))
                rhi_mismatch(which, "constants from register", start, 0, 0);
        }
        return;
    }
    ++rhi_native_calls[which];
    rhi_return(sp, 0x14, pixel ? api_set_ps_constants(d, start, data, count) : api_set_vs_constants(d, start, data, count));
}
static void rhi_set_vertex_shader_parameter(void) { rhi_shader_parameter(RHI_VS_CONST, sub_01596D70, 0); }
static void rhi_set_pixel_shader_parameter(void) { rhi_shader_parameter(RHI_PS_CONST, sub_01596DA0, 1); }

/* 01596DD0 (View, ViewProjectionMatrix, ViewOrigin): vertex c0..c3 = the matrix, c4 = the origin,
 * pixel c2 = View + 480, c1 = View + 496. */
static void rhi_set_view_parameters(void)
{
    const uint32_t sp = g_esp, rhi = g_ecx, view = MEM32(sp + 4), matrix = MEM32(sp + 8), origin = MEM32(sp + 12);
    d3d_device *d;
    ++rhi_calls[RHI_VIEW_CONST];
    if (rhi_mode != 1 || d3d_trace_active() || !(d = rhi_device(rhi))) {
        sub_01596DD0();
        if (rhi_mode == 2 && (d = rhi_device(rhi))) {
            if (memcmp(d->st.vs_f[0], GPTR(matrix), 64) || memcmp(d->st.vs_f[4], GPTR(origin), 16) ||
                memcmp(d->st.ps_f[2], GPTR(view + 480), 16) || memcmp(d->st.ps_f[1], GPTR(view + 496), 16))
                rhi_mismatch(RHI_VIEW_CONST, "view constants", 0, 0, 0);
        }
        return;
    }
    ++rhi_native_calls[RHI_VIEW_CONST];
    api_set_vs_constants(d, 0, matrix, 4);
    api_set_vs_constants(d, 4, origin, 1);
    api_set_ps_constants(d, 2, view + 480, 1);
    rhi_return(sp, 0xC, api_set_ps_constants(d, 1, view + 496, 1));
}

/* ---- draws. 015979B0 RHIDrawIndexedPrimitive(IndexBuffer, PrimitiveType, BaseVertexIndex, MinIndex, NumVertices,
 * StartIndex, NumPrimitives) and 01597840 RHIDrawPrimitive(PrimitiveType, BaseVertexIndex, NumPrimitives): the
 * STAT counters DrawPrimitiveCalls 0x579, RHITriangles 0x57A, RHILines 0x57B (a TMap<int> find as 00B65BD0 and
 * InterlockedExchangeAdd at the value + 12, the host's being a plain add), a device-lost test if
 * GParanoidDeviceLostChecking is set (the GLES device is never lost: its TestCooperativeLevel answers D3D_OK),
 * then SetIndices (buffer - 4 or 0, as the stream sources) and the draw with the primitive type mapped:
 * 0 triangles (4), 1 strip (5), 2 lines (2). The original asserts on a missing counter or an unknown type:
 * those take the original. */
#define RHI_STATS_MAP 0x01DCE1F4u
#define RHI_DEVICE_LOST_CHECKING 0x01D1341Cu

/* The pair found last for each counter: it stays valid while the map's pair array is the one it was found in and
 * its key is still there (a rehash moves the array or the pairs; the original walk is the fallback). */
typedef struct { uint32_t pairs, pair; } rhi_stat_slot;

static inline uint32_t rhi_stat_find(uint32_t key, rhi_stat_slot *slot)
{
    const uint32_t pairs = MEM32(RHI_STATS_MAP);
    if (pairs == slot->pairs && pairs && MEM32(slot->pair + 4) == key) return MEM32(slot->pair + 8);
    const uint32_t hash = MEM32(RHI_STATS_MAP + 0xC);
    if (!hash || (int32_t)MEM32(RHI_STATS_MAP + 4) <= 0) return 0;
    uint32_t id = MEM32(hash + ((MEM32(RHI_STATS_MAP + 0x10) - 1u) & key) * 4u);
    while (id != UINT32_MAX) {
        const uint32_t pair = pairs + id * 12u;
        if (MEM32(pair + 4) == key) {
            slot->pairs = pairs;
            slot->pair = pair;
            return MEM32(pair + 8);
        }
        id = MEM32(pair);
    }
    return 0;
}

static inline int rhi_draw_stats(uint32_t type, uint32_t primitives)
{
    static rhi_stat_slot slots[3];
    const uint32_t calls = rhi_stat_find(0x579u, &slots[0]), triangles = rhi_stat_find(0x57Au, &slots[1]), lines = rhi_stat_find(0x57Bu, &slots[2]);
    if (!calls || !triangles || !lines) return 0;
    MEM32(calls + 12) += 1u;
    MEM32(triangles + 12) += type != 2 ? primitives : 0;
    MEM32(lines + 12) += type == 2 ? primitives : 0;
    return 1;
}

static inline uint32_t rhi_primitive_type(uint32_t type) { return type == 0 ? 4u : type == 1 ? 5u : type == 2 ? 2u : 0; }

/* Mode 2: after the original ran, what its COM draw did against what the native path would draw. */
static void rhi_check_draw(int which, uint32_t hits_before, uint32_t primitive, uint32_t base, uint32_t min_index,
                           uint32_t start, uint32_t count, uint32_t indexed)
{
    const rhi_draw_seen *s = &rhi_seen_draw;
    if (s->hits != hits_before + 1u) { rhi_mismatch(which, "COM draw calls", 0, s->hits - hits_before, 1); return; }
    rhi_check(which, "primitive", 0, s->primitive, primitive);
    rhi_check(which, "base vertex", 0, s->base, base);
    rhi_check(which, "min index", 0, s->min_index, min_index);
    rhi_check(which, "start", 0, s->start, start);
    rhi_check(which, "count", 0, s->count, count);
    rhi_check(which, "indexed", 0, s->indexed, indexed);
}

static void rhi_draw_indexed_primitive(void)
{
    const uint32_t sp = g_esp, rhi = g_ecx, buffer = MEM32(sp + 4), type = MEM32(sp + 8);
    const uint32_t base = MEM32(sp + 12), min_index = MEM32(sp + 16), start = MEM32(sp + 24), count = MEM32(sp + 28);
    const uint32_t primitive = rhi_primitive_type(type), guest = buffer ? buffer - 4u : 0;
    d3d_device *d;
    ++rhi_calls[RHI_DRAW_INDEXED];
    if (rhi_mode != 1 || !primitive || !(d = rhi_device(rhi)) || !rhi_draw_stats(type, count)) {
        const uint32_t hits = rhi_seen_draw.hits;
        sub_015979B0();
        if (rhi_mode == 2 && primitive && (d = rhi_device(rhi))) {
            const void_com_object *bound = (const void_com_object *)d->st.indices;
            rhi_check(RHI_DRAW_INDEXED, "index buffer", 0, bound ? bound->guest : 0, guest);
            rhi_check_draw(RHI_DRAW_INDEXED, hits, primitive, base, min_index, start, count, 1);
        }
        return;
    }
    ++rhi_native_calls[RHI_DRAW_INDEXED];
    api_set_indices(d, guest);
    if (count) d3d_gl_draw(d, primitive, 0, vertex_count_for(primitive, count), count, 1, base, start, min_index, NULL, 0, NULL, 0);
    rhi_return(sp, 0x1C, D3D_OK);
}

static void rhi_draw_primitive(void)
{
    const uint32_t sp = g_esp, rhi = g_ecx, type = MEM32(sp + 4), base = MEM32(sp + 8), count = MEM32(sp + 12);
    const uint32_t primitive = rhi_primitive_type(type);
    d3d_device *d;
    ++rhi_calls[RHI_DRAW];
    if (rhi_mode != 1 || !primitive || !(d = rhi_device(rhi)) || !rhi_draw_stats(type, count)) {
        const uint32_t hits = rhi_seen_draw.hits;
        sub_01597840();
        if (rhi_mode == 2 && primitive && (d = rhi_device(rhi))) rhi_check_draw(RHI_DRAW, hits, primitive, 0, 0, base, count, 0);
        return;
    }
    ++rhi_native_calls[RHI_DRAW];
    if (count) d3d_gl_draw(d, primitive, base, vertex_count_for(primitive, count), count, 0, 0, 0, 0, NULL, 0, NULL, 0);
    rhi_return(sp, 0xC, D3D_OK);
}

/* 01597B50 DrawPrimitiveUP(PrimitiveType, NumPrimitives, VertexData, VertexDataStride) and 01597CD0
 * DrawIndexedPrimitiveUP(PrimitiveType, MinVertexIndex, NumVertices, NumPrimitives, IndexData, IndexDataStride,
 * VertexData, VertexDataStride): the statistics as above, SetStreamSourceFreq(0, 1), then the D3D9 UP draw (the
 * device's UP draw with the post-effects hook and the stream unbinding); the index format is INDEX16 for a stride
 * of two bytes, else INDEX32. */
static void rhi_draw_primitive_up(void)
{
    const uint32_t sp = g_esp, rhi = g_ecx, type = MEM32(sp + 4), count = MEM32(sp + 8), data = MEM32(sp + 12), stride = MEM32(sp + 16);
    const uint32_t primitive = rhi_primitive_type(type);
    d3d_device *d;
    ++rhi_calls[RHI_DRAW_UP];
    if (rhi_mode != 1 || !primitive || !(d = rhi_device(rhi)) || !rhi_draw_stats(type, count)) {
        const uint32_t hits = rhi_seen_draw.hits;
        sub_01597B50();
        if (rhi_mode == 2 && primitive && (d = rhi_device(rhi))) {
            rhi_check_draw(RHI_DRAW_UP, hits, primitive, 0, 0, 0, count, 0);
            rhi_check(RHI_DRAW_UP, "frequency of stream", 0, d->st.stream_freq[0], 1);
        }
        return;
    }
    ++rhi_native_calls[RHI_DRAW_UP];
    api_set_stream_source_freq(d, 0, 1);
    rhi_return(sp, 0x10, api_draw_primitive_up(d, d->com.guest, primitive, count, data, stride));
}

static void rhi_draw_indexed_primitive_up(void)
{
    const uint32_t sp = g_esp, rhi = g_ecx, type = MEM32(sp + 4), min_index = MEM32(sp + 8), vertices = MEM32(sp + 12);
    const uint32_t count = MEM32(sp + 16), indices = MEM32(sp + 20), format = MEM32(sp + 24) != 2 ? 102u : 101u;
    const uint32_t data = MEM32(sp + 28), stride = MEM32(sp + 32), primitive = rhi_primitive_type(type);
    d3d_device *d;
    ++rhi_calls[RHI_DRAW_INDEXED_UP];
    if (rhi_mode != 1 || !primitive || !(d = rhi_device(rhi)) || !rhi_draw_stats(type, count)) {
        const uint32_t hits = rhi_seen_draw.hits;
        sub_01597CD0();
        if (rhi_mode == 2 && primitive && (d = rhi_device(rhi))) {
            rhi_check_draw(RHI_DRAW_INDEXED_UP, hits, primitive, 0, min_index, 0, count, 1);
            rhi_check(RHI_DRAW_INDEXED_UP, "frequency of stream", 0, d->st.stream_freq[0], 1);
        }
        return;
    }
    ++rhi_native_calls[RHI_DRAW_INDEXED_UP];
    api_set_stream_source_freq(d, 0, 1);
    rhi_return(sp, 0x20, api_draw_indexed_primitive_up(d, d->com.guest, primitive, min_index, vertices, count, indices, format, data, stride));
}

/* Pixel comparison of the two paths on one running scene (QA only): with userdata/rhi-flip present when the
 * game starts, the file is read once per 30 frames: 0 = the COM path, anything else = the native path. */
static int rhi_flip;

void d3d_rhi_present(void);
void d3d_rhi_present(void)
{
    static uint32_t divider;
    if (!rhi_flip || ++divider < 30) return;
    divider = 0;
    char path[1100];
    snprintf(path, sizeof path, "%s/rhi-flip", void_data_dir());
    FILE *file = fopen(path, "rb");
    const int value = file ? fgetc(file) : '1';
    if (file) fclose(file);
    const int mode = value == '0' ? 3 : 1;   /* 3: the handlers stay registered and take the original */
    if (mode != rhi_mode) { void_log("[native-rhi] flip: mode %d", mode); rhi_mode = mode; }
}

/* Per frame-statistics interval: calls of each method and the share the native path took. */
void d3d_rhi_log(uint32_t frames);
void d3d_rhi_log(uint32_t frames)
{
    static uint64_t last[RHI_COUNT], last_native[RHI_COUNT];
    if (!frames) return;
    char line[1024];
    size_t used = (size_t)snprintf(line, sizeof line, "[rhi] calls per frame (native):");
    for (int i = 0; i < RHI_COUNT && used < sizeof line - 64; ++i) {
        used += (size_t)snprintf(line + used, sizeof line - used, " %s %.0f (%.0f)", rhi_names[i],
                                 (double)(rhi_calls[i] - last[i]) / frames,
                                 (double)(rhi_native_calls[i] - last_native[i]) / frames);
        last[i] = rhi_calls[i];
        last_native[i] = rhi_native_calls[i];
    }
    void_log("%s", line);
}
