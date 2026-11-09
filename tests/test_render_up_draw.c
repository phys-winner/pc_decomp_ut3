/* DrawIndexedPrimitiveUP through the production GLES path (d3d9_gl.c, a real GLES context): the vertex
 * bytes uploaded for a draw whose largest index is not count - 1 are the bytes the draw reads,
 * (largest index + 1) * stride, as the game handed them; the same whether the game's thread sized
 * them with the record (d3d_gl_draw -> run_draw -> gl_draw) or the render thread measures them (a
 * draw made inside a record: d3d_rt_executing, gl_draw with 0). Run it twice, with
 * VOID_RENDER_THREAD=1 and =0: the same uploads with the render thread and inline.
 * Build: tools/build_host_test.py tests/test_render_up_draw.c --objects build/<task>/obj --exclude android_main */
#include "host.h"
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include "../android/host/d3d9_internal.h"
static unsigned array_buffer_binds;
static void count_bind_buffer(GLenum target, GLuint buffer)
{
    if (target == GL_ARRAY_BUFFER) ++array_buffer_binds;
    glBindBuffer(target, buffer);
}
#define glBindBuffer count_bind_buffer
#include "../android/host/d3d9_gl.c"
#undef glBindBuffer

/* A scratch -finstrument-functions build checks the skipped work without production hooks. */
#ifdef TEST_BOUNDS_INSTRUMENT
static unsigned bounds_calls;
__attribute__((no_instrument_function)) void __cyg_profile_func_enter(void *fn, void *caller)
{
    (void)caller;
    if (fn == (void *)draw_vertex_bounds) ++bounds_calls;
}
__attribute__((no_instrument_function)) void __cyg_profile_func_exit(void *fn, void *caller)
{ (void)fn; (void)caller; }
#define RESET_BOUNDS_CALLS() (bounds_calls = 0)
#define CHECK_BOUNDS_CALLS(n) assert(bounds_calls == (n))
#else
#define RESET_BOUNDS_CALLS() ((void)0)
#define CHECK_BOUNDS_CALLS(n) ((void)0)
#endif

void void_process_exit(uint32_t code, int detach) { (void)detach; _exit((int)code); }

typedef struct { uint32_t used, segment; int ring; GLuint vbo; } ring_position;
static void run_position(void *out)
{
    ring_position *p = (ring_position *)out;
    p->used = ring_used; p->segment = ring_segment; p->ring = ring_state > 0; p->vbo = up_vbo;
}
/* The ring is made by the first UP draw (ring_alloc -> ring_ready): make it before the checks, so the
 * position read before a draw is where that draw writes, and the path (ring or buffer) is settled. */
static void run_ring_ready(void *unused) { (void)unused; (void)ring_ready(); }

/* What GL holds: a GPU copy of the range into a buffer of our own, mapped for reading. */
typedef struct { GLuint buffer; uint32_t offset, bytes; GLint size; uint8_t data[256]; } readback;
static void run_readback(void *p)
{
    readback *r = (readback *)p;
    GLuint scratch;
    glGenBuffers(1, &scratch);
    glBindBuffer(GL_COPY_WRITE_BUFFER, scratch);
    glBufferData(GL_COPY_WRITE_BUFFER, r->bytes, NULL, GL_STREAM_READ);
    glBindBuffer(GL_COPY_READ_BUFFER, r->buffer);
    glGetBufferParameteriv(GL_COPY_READ_BUFFER, GL_BUFFER_SIZE, &r->size);
    glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, r->offset, 0, r->bytes);
    const void *m = glMapBufferRange(GL_COPY_WRITE_BUFFER, 0, r->bytes, GL_MAP_READ_BIT);
    assert(m);
    memcpy(r->data, m, r->bytes);
    glUnmapBuffer(GL_COPY_WRITE_BUFFER);
    glDeleteBuffers(1, &scratch);
}

typedef struct { uint32_t count, prims; const void *indices; int index32; } up_draw;
static uint8_t vertices[8 * 16];
static d3d_device dev_under_test;
static d3d_surface render_target;
static void run_target_ready(void *p)
{
    d3d_surface *s = (d3d_surface *)p;
    glGenTextures(1, &s->gl);
    glBindTexture(GL_TEXTURE_2D, s->gl);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, (GLsizei)s->width, (GLsizei)s->height);
    glBindTexture(GL_TEXTURE_2D, 0);
    d3d_device *d = d3d_rt_device();
    d->rt[0] = s;
    d->fbo_dirty = 1;
    d3d_gl_bind_targets(d);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    assert(glGetError() == GL_NO_ERROR);
}
/* A draw made from inside a record: the render thread measures the vertex bytes itself. */
static void run_direct(void *p)
{
    const up_draw *u = (const up_draw *)p;
    assert(d3d_rt_executing());
    d3d_gl_draw(d3d_rt_device(), 4, 0, u->count, u->prims, 1, 0, 0, 0, vertices, 16, u->indices, u->index32);
}

static void check(const char *name, const up_draw *u, uint32_t largest, int direct)
{
    uint32_t vertex_bytes = (largest + 1) * 16, index_bytes = u->count * (u->index32 ? 4u : 2u);
    ring_position before, after;
    d3d_rt_sync(run_position, &before);
    if (direct) d3d_rt_sync(run_direct, (void *)u);
    else assert(d3d_gl_draw(&dev_under_test, 4, 0, u->count, u->prims, 1, 0, 0, 0, vertices, 16, u->indices, u->index32));
    d3d_rt_sync(run_position, &after);
    assert(after.ring == before.ring);
    readback r = { 0, 0, vertex_bytes, 0, {0} };
    if (before.ring) {
        /* Vertices, then indices, each 16-aligned in this frame's segment. */
        uint32_t at = (before.used + 15u) & ~15u;
        assert(after.segment == before.segment);
        assert(after.used == ((at + vertex_bytes + 15u) & ~15u) + index_bytes);
        r.buffer = ring_buffer;
        r.offset = before.segment * RING_SEGMENT_BYTES + at;
    } else {
        r.buffer = after.vbo;   /* glBufferData of exactly the bytes uploaded */
        assert(r.buffer);
    }
    d3d_rt_sync(run_readback, &r);
    if (!before.ring) assert((uint32_t)r.size == vertex_bytes);
    assert(!memcmp(r.data, vertices, vertex_bytes));
    printf("PASS %s (%s, %s): %u vertex bytes for largest index %u of %u indices\n", name,
           direct ? "measured by the render thread" : "sized by the game's thread", before.ring ? "ring" : "buffer",
           vertex_bytes, largest, u->count);
}

static void run_attribute_binds(void *p)
{
    d3d_device *d = d3d_rt_device();
    d3d_decl *saved_decl = d->st.decl, decl = { 0 };
    void *saved_stream = d->st.streams[1];
    uint32_t saved_stride = d->st.stream_stride[1];
    decl.count = 3;
    decl.elements[0] = (d3d_vertex_element){ 0, 0, 2, 0, 0, 0 };
    decl.elements[1] = (d3d_vertex_element){ 0, 0, 2, 0, 3, 0 };
    decl.elements[2] = (d3d_vertex_element){ 0, 12, 4, 0, 10, 0 };
    d3d_buffer_rt other = { 0 };
    other.b.size = sizeof vertices;
    other.b.shadow = other.rt_shadow = *(uint32_t *)p;
    other.b.dirty_hi = sizeof vertices;
    glGenBuffers(1, &other.b.gl);
    glBindBuffer(GL_COPY_WRITE_BUFFER, other.b.gl);
    glBufferData(GL_COPY_WRITE_BUFFER, sizeof vertices, NULL, GL_STATIC_DRAW);
    d->st.decl = &decl;
    d->st.streams[1] = &other.b;
    d->st.stream_stride[1] = 16;
    for (unsigned mixed = 0; mixed < 2; ++mixed) {
        decl.elements[1].stream = (uint16_t)mixed;
        attr_cache_ready = 0;
        array_buffer_binds = 0;
        RESET_BOUNDS_CALLS();
        assert(d3d_gl_draw(d, 4, 0, 3, 1, 0, 0, 0, 0, vertices, 16, NULL, 0));
        CHECK_BOUNDS_CALLS(mixed);
        assert(array_buffer_binds == (mixed ? 3u : 1u) + (ring_state > 0 ? 0u : 1u));
        for (GLuint i = 0; i < 3; ++i) {
            GLint captured = 0;
            glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &captured);
            GLuint expected = mixed && i == 1 ? other.b.gl : ring_state > 0 ? ring_buffer : up_vbo;
            assert((GLuint)captured == expected);
        }
    }
    readback r = { other.b.gl, 0, sizeof vertices, 0, { 0 } };
    run_readback(&r);
    assert(!memcmp(r.data, GPTR(other.rt_shadow), sizeof vertices));
    d3d_gl_forget_buffer(other.b.gl);
    glDeleteBuffers(1, &other.b.gl);
    d->st.decl = saved_decl;
    d->st.streams[1] = saved_stream;
    d->st.stream_stride[1] = saved_stride;
    puts("PASS attribute binds: one bind for a shared buffer, A/B/A switches captured correctly across a COPY_WRITE upload");
}

static void run_mixed_bounds(void *p)
{
    d3d_device *d = d3d_rt_device();
    d3d_state saved = d->st;
    d3d_decl decl = { 0 };
    decl.count = 2;
    decl.elements[0] = (d3d_vertex_element){ 0, 0, 2, 0, 0, 0 };
    decl.elements[1] = (d3d_vertex_element){ 1, 4, 2, 0, 3, 0 };
    d3d_buffer_rt other = { 0 };
    uint8_t pages[3] = { 1, 1, 1 };
    uint8_t zero[256] = { 0 };
    other.b.size = 3 * 4096;
    other.b.shadow = other.rt_shadow = *(uint32_t *)p;
    other.b.dirty_pages = pages;
    other.b.page_count = 3;
    glGenBuffers(1, &other.b.gl);
    glBindBuffer(GL_COPY_WRITE_BUFFER, other.b.gl);
    glBufferData(GL_COPY_WRITE_BUFFER, other.b.size, NULL, GL_STATIC_DRAW);
    d->st.decl = &decl;
    d->st.streams[1] = &other.b;
    d->st.stream_stride[1] = 1024;
    d->st.stream_offset[1] = 20;
    const uint16_t i16[] = { 1, 5, 3 };
    const uint32_t i32[] = { 1, 5, 3 };
    for (unsigned mode = 0; mode < 3; ++mode) {
        /* Defined initial GPU bytes distinguish untouched dirty pages from uploaded pages. */
        glBindBuffer(GL_COPY_WRITE_BUFFER, other.b.gl);
        for (unsigned at = 0; at < other.b.size; at += sizeof zero)
            glBufferSubData(GL_COPY_WRITE_BUFFER, at, sizeof zero, zero);
        memset(pages, 1, sizeof pages);
        other.b.dirty_page_count = 3;
        d->st.stream_freq[0] = mode == 2 ? 0x40000002u : 1;
        d->st.stream_freq[1] = mode == 2 ? 0x80000001u : 1;
        attr_cache_ready = 0;
        RESET_BOUNDS_CALLS();
        assert(d3d_gl_draw(d, 4, 0, 3, 1, mode != 2, 0, 0, 0, vertices, 16,
                           mode == 0 ? (const void *)i16 : mode == 1 ? (const void *)i32 : NULL, mode == 1));
        CHECK_BOUNDS_CALLS(1);
        assert(!pages[0] && pages[1] == (mode == 2) && pages[2]);
        assert(other.b.dirty_page_count == (mode == 2 ? 2u : 1u));
        GLint captured, stride, divisor;
        void *offset = NULL;
        glGetVertexAttribiv(1, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &captured);
        glGetVertexAttribiv(1, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &stride);
        glGetVertexAttribiv(1, GL_VERTEX_ATTRIB_ARRAY_DIVISOR, &divisor);
        glGetVertexAttribPointerv(1, GL_VERTEX_ATTRIB_ARRAY_POINTER, &offset);
        assert((GLuint)captured == other.b.gl && stride == 1024 && (uintptr_t)offset == 24);
        assert(divisor == (mode == 2));
        for (unsigned page = 0; page < 3; ++page) {
            readback r = { other.b.gl, page * 4096 + 24, sizeof zero, 0, { 0 } };
            run_readback(&r);
            const void *expected = pages[page] ? (const void *)zero : (const uint8_t *)GPTR(other.rt_shadow) + r.offset;
            assert(!memcmp(r.data, expected, r.bytes));
        }
        GLenum error = glGetError();
        if (error) fprintf(stderr, "mixed draw mode %u GLES error %#x\n", mode, error);
        assert(error == GL_NO_ERROR);
    }
    /* A bound but unused regular stream must not bring the pure-UP index scan back. */
    decl.elements[1].stream = 0;
    d->st.stream_freq[0] = d->st.stream_freq[1] = 1;
    RESET_BOUNDS_CALLS();
    assert(d3d_gl_draw(d, 4, 0, 3, 1, 1, 0, 0, 0, vertices, 16, i16, 0));
    CHECK_BOUNDS_CALLS(0);
    d3d_gl_forget_buffer(other.b.gl);
    glDeleteBuffers(1, &other.b.gl);
    d->st = saved;
    puts("PASS mixed UP streams: 16/32-bit bounds preserve exact dirty pages, offsets, strides and instance divisors");
}

static void run_invalid_elements(void *unused)
{
    (void)unused;
    d3d_device *d = d3d_rt_device();
    d3d_decl *saved = d->st.decl, decl = { 0 };
    d->st.decl = &decl;
    decl.count = 4;
    decl.elements[0] = (d3d_vertex_element){ 0, 0, 17, 0, 0, 0 };
    decl.elements[1] = (d3d_vertex_element){ 16, 0, 2, 0, 0, 0 };
    decl.elements[2] = (d3d_vertex_element){ 65535, 0, 2, 0, 0, 0 };
    decl.elements[3] = (d3d_vertex_element){ 0, 0, 2, 0, 0, 0 };
    assert(find_element(d, 0, 0) == &decl.elements[3]);
    decl.count = 3;
    assert(!find_element(d, 0, 0));
    for (unsigned i = 0; i < 4; ++i) decl.elements[i].usage = 9;
    assert(!find_element(d, 0, 0));
    decl.count = 4;
    assert(find_element(d, 0, 0) == &decl.elements[3]);
    assert(!find_element(d, 3, 0) && !bgra_of(d, 10, 0));
    attr_cache_ready = 0;
    RESET_BOUNDS_CALLS();
    assert(d3d_gl_draw(d, 4, 0, 3, 1, 0, 0, 0, 0, vertices, 16, NULL, 0));
    CHECK_BOUNDS_CALLS(0);
    for (GLuint i = 1; i < 4; ++i) {
        GLint enabled = -1;
        GLfloat value[4];
        glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &enabled);
        glGetVertexAttribfv(i, GL_CURRENT_VERTEX_ATTRIB, value);
        assert(!enabled && value[0] == (i >= 2) && value[1] == (i >= 2) && value[2] == (i >= 2) && value[3] == 1);
    }
    assert(glGetError() == GL_NO_ERROR);
    d->st.decl = saved;
    puts("PASS element resolution: invalid types/streams skipped before valid duplicates and POSITIONT fallback; missing inputs use GL defaults");
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    assert(void_guest_reserve());
    void_threads_init(); void_dispatch_init(); void_machine_enter();
    const char *mode = getenv("VOID_RENDER_THREAD");
    if (!mode || !*mode) { setenv("VOID_RENDER_THREAD", "1", 1); mode = "1"; }
    assert(d3d_gl_init());
    assert(d3d_rt_threaded() == (*mode != '0'));
    /* The fixed-function path with one FLOAT3 position: what the draw reads of the D3D9 defaults. */
    static d3d_decl decl;
    decl.count = 1;
    decl.elements[0] = (d3d_vertex_element){ 0, 0, 2, 0, 0, 0 };
    d3d_device *d = &dev_under_test;
    d->st.decl = &decl;
    d->st.viewport[2] = d->st.viewport[3] = 32;
    d->st.viewport_z[1] = 1.0f;
    d->st.rs[8] = 3; d->st.rs[14] = 1; d->st.rs[19] = 2; d->st.rs[20] = 1; d->st.rs[22] = 1; d->st.rs[23] = 4;
    d->st.rs[25] = 8; d->st.rs[168] = 0xF;
    for (int i = 0; i < 8; ++i) { d->st.tss[i][1] = 1; d->st.tss[i][4] = 1; d->st.tss[i][11] = (uint32_t)i; }
    for (int t = 0; t < 512; ++t) for (int k = 0; k < 4; ++k) d->st.transforms[t][k * 5] = 1.0f;
    for (int i = 0; i < 16; ++i) d->st.stream_freq[i] = 1;
    render_target.width = render_target.height = 32;
    render_target.format = 21;
    render_target.fmt = d3d_format_find(21);
    d->rt[0] = &render_target;
    d3d_rt_device_reset(d);
    d3d_rt_sync(run_target_ready, &render_target);
    d3d_rt_sync(run_ring_ready, NULL);
    for (uint32_t i = 0; i < sizeof vertices; ++i) vertices[i] = (uint8_t)(i * 7u + 3u);

    static const uint16_t short_indices[] = { 0, 5, 2 };            /* largest 5, count - 1 = 2 */
    static const uint32_t long_indices[] = { 7, 1, 3 };             /* largest 7 */
    static const uint16_t repeated[] = { 1, 1, 0, 0, 1, 0 };        /* largest 1 < count - 1 = 5 */
    const up_draw cases[] = { { 3, 1, short_indices, 0 }, { 3, 1, long_indices, 1 }, { 6, 2, repeated, 0 } };
    const uint32_t largest[] = { 5, 7, 1 };
    const char *names[] = { "16-bit indices", "32-bit indices", "repeated indices" };
    for (int direct = 0; direct < 2; ++direct)
        for (int i = 0; i < 3; ++i) check(names[i], &cases[i], largest[i], direct);
    uint32_t shadow = void_galloc(sizeof vertices);
    memcpy(GPTR(shadow), vertices, sizeof vertices);
    d3d_rt_sync(run_attribute_binds, &shadow);
    void_gfree(shadow);
    shadow = void_galloc(3 * 4096);
    for (uint32_t i = 0; i < 3 * 4096; ++i) MEM8(shadow + i) = (uint8_t)(i * 13u + 5u);
    d3d_rt_sync(run_mixed_bounds, &shadow);
    void_gfree(shadow);
    d3d_rt_sync(run_invalid_elements, NULL);
    d3d_rt_finish();
    printf("PASS indexed UP draws upload (largest index + 1) * stride bytes, %s\n",
           d3d_rt_threaded() ? "with the render thread" : "inline");
    return 0;
}
