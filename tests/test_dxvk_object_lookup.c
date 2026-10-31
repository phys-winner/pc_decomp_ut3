/* Production COM identity lookup: collisions, references and destruction/reuse. */
#define main affinity_fixture_unused_main
#include "test_native_affinity.c"
#undef main
#include "../android/host/d3d9_dxvk.c"
typedef struct {void *native;uint32_t refs,addrefs,releases;} native_record;
static native_record records[2048];
static unsigned record_count;
static native_record *find_native(void *native)
{
    for(unsigned i=0;i<record_count;++i)if(records[i].native==native)return &records[i];
    assert(!"unregistered fixture native pointer");return NULL;
}
uint32_t ut3_dxvk_native_call(unsigned kind,void *native,unsigned slot,const uint32_t *args)
{(void)kind;(void)native;(void)slot;(void)args;assert(!"unexpected native API call");return 0;}
uint32_t ut3_dxvk_native_create9(uint32_t sdk){(void)sdk;return 0;}
uint32_t ut3_dxvk_native_addref(void *native)
{native_record *r=find_native(native);++r->addrefs;return ++r->refs;}
uint32_t ut3_dxvk_native_release(void *native)
{native_record *r=find_native(native);assert(r->refs);++r->releases;return --r->refs;}
static native_record *new_native(uintptr_t address)
{
    assert(record_count<sizeof records/sizeof *records);
    native_record *r=&records[record_count++];r->native=(void*)address;r->refs=1;return r;
}
static uint32_t method(uint32_t guest,unsigned slot)
{
    uint32_t before=g_esp,args[]={guest};
    uint32_t result=void_call_guest(MEM32(MEM32(guest)+slot*4),1,args,1);
    assert(g_esp==before);return result;
}
static unsigned live_objects(void)
{
    unsigned count=0;
    for(unsigned b=0;b<DXVK_OBJECT_BUCKETS;++b)
        for(dxvk_object *o=dxvk_objects[b];o;o=o->next){assert(dxvk_object_bucket(o->kind,o->native)==b);++count;}
    return count;
}
int main(void)
{
    alarm(10);assert(void_guest_reserve());void_threads_init();void_dispatch_init();void_machine_enter();
    assert(!ut3_dxvk_wrap_native(9,NULL));assert(!live_objects());
    native_record *identity=new_native(UINT64_C(0x123456780000));
    uint32_t surface=ut3_dxvk_wrap_native(9,identity->native);assert(surface);
    ++identity->refs;uint32_t same=ut3_dxvk_wrap_native(9,identity->native);assert(same==surface);
    assert(!identity->addrefs&&((dxvk_object*)void_com_host(surface))->com.refs==2);
    ++identity->refs;uint32_t unknown=ut3_dxvk_wrap_native(0,identity->native);assert(unknown!=surface&&live_objects()==2);
    assert(ut3_dxvk_unwrap_guest(surface)==identity->native&&ut3_dxvk_unwrap_guest(unknown)==identity->native);
    assert(method(surface,1)==3&&identity->addrefs==1&&identity->refs==4);
    assert(method(surface,2)==2);assert(method(surface,2)==1);assert(method(surface,2)==0);
    assert(live_objects()==1&&identity->refs==1);assert(method(unknown,2)==0&&identity->refs==0&&!live_objects());
    identity->refs=1;uint32_t reused=ut3_dxvk_wrap_native(9,identity->native);
    assert(((dxvk_object*)void_com_host(reused))->com.refs==1&&live_objects()==1);assert(!method(reused,2));assert(!live_objects());
    /* Force long same-kind collisions; every key remains independently live. */
    uint32_t collision_guests[64];native_record *collision_native[64];unsigned found=0,target=17;
    for(uintptr_t i=1;found<64;++i){uintptr_t address=UINT64_C(0x234560000000)+i*16;
        if(dxvk_object_bucket(9,(void*)address)!=target)continue;
        native_record *r=new_native(address);collision_native[found]=r;
        collision_guests[found]=ut3_dxvk_wrap_native(9,r->native);++found;
    }
    assert(live_objects()==64);
    for(unsigned i=0;i<64;++i){++collision_native[i]->refs;assert(ut3_dxvk_wrap_native(9,collision_native[i]->native)==collision_guests[i]);assert(method(collision_guests[i],2)==1);}
    const unsigned removal[]={63,0,31,62,1,32};
    for(unsigned i=0;i<sizeof removal/sizeof *removal;++i){unsigned index=removal[i];assert(!method(collision_guests[index],2));collision_guests[index]=0;}
    assert(live_objects()==58);
    for(unsigned i=0;i<64;++i)if(collision_guests[i])assert(!method(collision_guests[i],2));
    assert(!live_objects());
    /* A realistic scattered set exercises independent buckets and repeated identities. */
    uint32_t many[512];native_record *many_native[512];
    for(unsigned i=0;i<512;++i){many_native[i]=new_native(UINT64_C(0x345670000000)+i*48);many[i]=ut3_dxvk_wrap_native(9,many_native[i]->native);}
    for(unsigned i=0;i<10000;++i){unsigned index=(i*197u)%512u;++many_native[index]->refs;assert(ut3_dxvk_wrap_native(9,many_native[index]->native)==many[index]);assert(method(many[index],2)==1);}
    for(unsigned i=0;i<512;++i){assert(!method(many[i],2));assert(!many_native[i]->refs);}
    assert(!live_objects());
    puts("PASS production DXVK native/kind identity hash: stable aliases, incoming refs without extra AddRef, guest AddRef/Release stdcall, pointer reuse, 64 forced collisions, head/middle/tail destruction and 10000 scattered rewraps");
    return 0;
}
