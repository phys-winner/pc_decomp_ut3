/* Reuse the real production provider and its host fixture, not a copied affinity algorithm. */
#define main creation_affinity_fixture_main
#include "test_native_affinity.c"
#undef main
#include <stdatomic.h>
static cpu_set_t fixture_guest,fixture_original;
static atomic_uint fixture_entries,fixture_work;
extern void ut3_fixture_run_dxvk_workers(void (*work)(void));
void ut3_fixture_dxvk_worker_enter(void)
{
    cpu_set_t inherited=current();assert(CPU_EQUAL(&inherited,&fixture_guest));
    errno=EDOM;assert(void_native_worker_affinity_apply());assert(errno==EDOM);
    same(fixture_original);atomic_fetch_add(&fixture_entries,1);
}
static void child_work(void)
{
    same(fixture_original);atomic_fetch_add(&fixture_work,1);
}
int main(void)
{
    alarm(10);fixture_original=current();
    assert(void_guest_reserve());void_threads_init();void_dispatch_init();void_machine_enter();
    fixture_guest=current();print_mask("original",fixture_original);print_mask("guest",fixture_guest);
    ut3_fixture_run_dxvk_workers(child_work);same(fixture_guest);
    assert(atomic_load(&fixture_entries)==9&&atomic_load(&fixture_work)==9);
    puts("PASS production DXVK Android child-entry constructor: 9 lazy workers initially inherit guest7, apply actual host original0-7 before user function; moved/vector threads, parent7 and errno preserved");
    return 0;
}
