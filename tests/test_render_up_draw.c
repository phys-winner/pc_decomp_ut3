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
#include "../android/host/d3d9_gl.c"

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
    d3d_rt_device_reset(d);
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
    d3d_rt_finish();
    printf("PASS indexed UP draws upload (largest index + 1) * stride bytes, %s\n",
           d3d_rt_threaded() ? "with the render thread" : "inline");
    return 0;
}
