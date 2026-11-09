/* Cached production shader binds without a GL context: key changes, collision traversal and invalidation.
 * Build with tools/build_host_test.py, excluding android_main; glUseProgram is observed here. */
#include "../android/host/d3d9_internal.h"
#include <assert.h>
#include <unistd.h>

static unsigned use_calls;
static GLuint used_program;
static void cache_use_program(GLuint program) { ++use_calls; used_program = program; }
#define glUseProgram cache_use_program
#include "../android/host/d3d9_shader.c"
#undef glUseProgram

void void_process_exit(uint32_t code, int detach) { (void)detach; _exit((int)code); }

static void aligned_hashes(void)
{
    const uint32_t alignments[] = { 16, 64, 128, 4096 };
    for (unsigned test = 0; test < sizeof alignments / sizeof *alignments; ++test) {
        unsigned counts[1024] = {0}, occupied = 0, longest = 0;
        const d3d_shader *vs = (const d3d_shader *)(uintptr_t)0x7410000000ull;
        for (unsigned i = 0; i < 4096; ++i) {
            const d3d_shader *ps = (const d3d_shader *)(uintptr_t)(0x7460000000ull + (uint64_t)i * alignments[test]);
            uint32_t slot = program_hash(vs, ps, 0, 0, 9);
            assert(slot < 1024);
            if (!counts[slot]++) ++occupied;
            if (counts[slot] > longest) longest = counts[slot];
        }
        /* A fixed variant must use the table even when every shader has the same allocation alignment. */
        assert(occupied >= 950 && longest <= 16);
        printf("alignment %u: %u buckets, longest chain %u\n", alignments[test], occupied, longest);
    }
}

static void exact_keys(void)
{
    d3d_shader vs[2] = {0}, ps[2] = {0};
    program_t p = { .vs = vs, .ps = ps, .bgra_mask = 3, .ff_id = 7, .alpha_mode = 5 };
    assert(program_matches(&p, vs, ps, 3, 7, 5));
    assert(!program_matches(NULL, vs, ps, 3, 7, 5));
    assert(!program_matches(&p, vs + 1, ps, 3, 7, 5));
    assert(!program_matches(&p, vs, ps + 1, 3, 7, 5));
    assert(!program_matches(&p, vs, ps, 2, 7, 5));
    assert(!program_matches(&p, vs, ps, 3, 6, 5));
    assert(!program_matches(&p, vs, ps, 3, 7, 4));
    p.vs = NULL;
    assert(program_matches(&p, NULL, ps, 3, 7, 5));
    p.ps = NULL;
    assert(program_matches(&p, NULL, NULL, 3, 7, 5));
}

static int bgra;
static int bgra_of(d3d_device *dev, int usage, int index)
{ (void)dev; assert(usage == 0 && index == 0); return bgra; }

static void bind_cached(d3d_device *dev, d3d_shader *vs, d3d_shader *ps, program_t *expected)
{
    int count;
    const int *usage, *index, *ps_types, *vs_types;
    uint32_t ps_mask, vs_mask;
    assert(d3d_shader_bind2(dev, vs, ps, bgra_of, &count, &usage, &index, &ps_mask, &vs_mask, &ps_types, &vs_types));
    assert(current_program == expected && count == expected->attr_count && usage == expected->attr_usage &&
        index == expected->attr_index && ps_mask == expected->ps_sampler_mask && vs_mask == expected->vs_sampler_mask &&
        ps_types == expected->ps_sampler_type && vs_types == expected->vs_sampler_type);
}

static void cached_binds(void)
{
    MOJOSHADER_attribute attr = { .usage = 0, .index = 0 };
    MOJOSHADER_parseData parse = { .attribute_count = 1, .attributes = &attr };
    d3d_shader vs = { .parse = &parse }, ps = { .pixel = 1, .parse = &parse };
    d3d_device dev = {0};
    static program_t current, decoy;
    current = (program_t){ .vs = &vs, .ps = &ps, .gl = 11, .attr_count = 1, .ps_sampler_mask = 5 };
    memset(programs, 0, sizeof programs);
    current_program = &current;
    use_calls = 0;
    bgra = 0;
    /* The exact current key must bypass the empty hash table and leave GL bound. */
    bind_cached(&dev, &vs, &ps, &current);
    assert(use_calls == 0);
    /* A changed variant finds a pre-existing entry beyond a deliberate collision. */
    program_t *other = calloc(1, sizeof *other); assert(other);
    *other = (program_t){ .vs = &vs, .ps = &ps, .bgra_mask = 1, .gl = 12, .attr_count = 2, .vs_sampler_mask = 9 };
    decoy = current;
    decoy.ff_id = 99;
    decoy.next = other;
    programs[program_hash(&vs, &ps, 1, 0, 0)] = &decoy;
    bgra = 1;
    bind_cached(&dev, &vs, &ps, other);
    assert(use_calls == 1 && used_program == 12);
    bind_cached(&dev, &vs, &ps, other);
    assert(use_calls == 1);
    /* The alpha variant is part of the key too. */
    dev.st.rs[15] = 1; dev.st.rs[25] = 5; dev.st.rs[24] = 127;
    current.bgra_mask = 1; current.alpha_mode = 5;
    programs[program_hash(&vs, &ps, 1, 0, 5)] = &current;
    bind_cached(&dev, &vs, &ps, &current);
    assert(use_calls == 2 && used_program == 11);
    /* Removal must invalidate the current pointer before its allocation is freed. */
    memset(programs, 0, sizeof programs);
    other->gl = 0;
    programs[program_hash(&vs, &ps, 1, 0, 0)] = other;
    current_program = other;
    forget_shader(&vs);
    assert(current_program == NULL && used_program == 0 && use_calls == 3);
    for (unsigned i = 0; i < 1024; ++i) assert(programs[i] == NULL);
}

int main(void)
{
    aligned_hashes();
    exact_keys();
    cached_binds();
    puts("PASS shader program cache: aligned keys, exact variants, current reuse, collisions and invalidation");
    return 0;
}
