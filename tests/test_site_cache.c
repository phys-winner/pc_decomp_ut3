/* Per-site indirect-call caches (core.c void_resolve_site) against the dispatch table.
 *
 * Linked with a build's objects (tools/build_host_test.py); run with SITE_TEST_DIR naming a folder
 * whose site-cache file selects the mode (absent = 1, extensions; 0 = single entry; 2 = statistics).
 * Every call emulates RECOMP_SITE_RESOLVE's inline check and must return the target registered for
 * the VA at that moment: monomorphic, alternating, 5- and 6-target cycles, a megamorphic random
 * site (table promotion), re-registration of targets (flush), and more polymorphic sites than the
 * extension pool holds. The targets are distinct pointer values that are never called. */
#include "host.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

void void_site_cache_init(void);
const char *void_data_dir(void)
{
    const char *dir = getenv("SITE_TEST_DIR");
    return dir ? dir : "/nonexistent";
}

#define TARGETS 1024u
#define VA(i) (0xF0000000u + 16u * (uint32_t)(i))
static void_fn expected[TARGETS];
static unsigned long long calls, inline_hits;
static uint32_t rng = 12345u;

static uint32_t next_random(void)
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng;
}

static void_fn fake(uint32_t i, uint32_t generation) { return (void_fn)(uintptr_t)(0x10000000u + 64u * i + generation); }

static void call(recomp_icache_t *site, uint32_t i)
{
    const uint32_t va = VA(i);
    recomp_func_t fn;
    if (site->key == ~va) { fn = site->fn; ++inline_hits; }
    else fn = void_resolve_site(site, va);
    ++calls;
    if ((void_fn)fn != expected[i]) {
        fprintf(stderr, "FAIL site %p target %u: got %p, expected %p\n", (void *)site, i, (void *)fn, (void *)expected[i]);
        exit(1);
    }
}

static void reregister(uint32_t i, uint32_t generation)
{
    expected[i] = fake(i, generation);
    void_dispatch_add(VA(i), expected[i]);
}

#define MANY_SITES 70000u
static recomp_icache_t many[MANY_SITES];

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    void_site_cache_init();
    for (uint32_t i = 0; i < TARGETS; ++i) reregister(i, 0);
    static recomp_icache_t mono, two, five, six, mega, phases;
    for (int round = 0; round < 3; ++round) {
        for (int k = 0; k < 4000; ++k) call(&mono, 7);
        for (int k = 0; k < 4000; ++k) call(&two, 10 + (k & 1));
        for (int k = 0; k < 4000; ++k) call(&five, 20 + k % 5);
        for (int k = 0; k < 4000; ++k) call(&six, 30 + k % 6);
        for (int k = 0; k < 20000; ++k) call(&mega, 100 + next_random() % 300);
        for (int k = 0; k < 4000; ++k) call(&phases, 500 + k / 1000);
        /* A registered VA changing target must reach every cached copy at once. */
        reregister(7, round + 1);
        reregister(11, round + 1);
        reregister(22, round + 1);
        reregister(33, round + 1);
        reregister(150 + round, round + 1);
        reregister(501, round + 1);
        call(&mono, 7); call(&two, 11); call(&five, 22); call(&six, 33); call(&mega, 150 + round); call(&phases, 501);
    }
    for (uint32_t s = 0; s < MANY_SITES; ++s) { call(&many[s], 600 + s % 7); call(&many[s], 610 + s % 5); }
    for (int pass = 0; pass < 3; ++pass)
        for (uint32_t s = 0; s < MANY_SITES; ++s) call(&many[s], (s + pass) & 1 ? 600 + s % 7 : 610 + s % 5);
    reregister(600, 9);
    reregister(610, 9);
    for (uint32_t s = 0; s < MANY_SITES; ++s) { call(&many[s], 600 + s % 7); call(&many[s], 610 + s % 5); }
    void_thunk_stats(100);    /* site-cache 2: the statistics lines */
    printf("PASS site caches: %llu calls, %llu inline hits, %llu through void_resolve_site\n",
           calls, inline_hits, calls - inline_hits);
    return 0;
}
