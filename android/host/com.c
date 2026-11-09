/* Guest COM object plumbing. */
#include "com.h"

#define MAX_OBJECTS 65536
static void *objects[MAX_OBJECTS];
static uint32_t object_hint = 1;
static pthread_mutex_t com_lock = PTHREAD_MUTEX_INITIALIZER;

uint32_t void_com_vtable(void_com_class *cls)
{
    if (cls->vtable) return cls->vtable;
    uint32_t vt = void_galloc(4u * (uint32_t)cls->count);
    for (int i = 0; i < cls->count; ++i) {
        char *name = (char *)malloc(96);
        snprintf(name, 96, "%s#%d", cls->name, i);
        MEM32(vt + 4u * (uint32_t)i) = void_thunk(cls->methods[i], name);
    }
    cls->vtable = vt;
    return vt;
}

/* The guest half is [vtable][table index][host object pointer]: the method
 * call path reads the pointer from the line the caller's vtable load already
 * touched instead of the 512 KiB object table (two dependent cache misses per
 * D3D9 call), and the back-pointer check still rejects stale objects. */
#define GUEST_HOST_POINTER 8u
#define GUEST_HEADER 16u

uint32_t void_com_wrap(void_com_object *object, void_com_class *cls, uint32_t extra_guest)
{
    pthread_mutex_lock(&com_lock);
    uint32_t index = 0;
    for (uint32_t n = 0; n < MAX_OBJECTS; ++n) {
        uint32_t i = (object_hint + n) % MAX_OBJECTS;
        if (i && !objects[i]) { index = i; break; }
    }
    if (!index) void_fatal("COM object table full");
    objects[index] = object;
    object_hint = index + 1;
    pthread_mutex_unlock(&com_lock);
    uint32_t guest = void_galloc(GUEST_HEADER + extra_guest);
    if (!guest) void_fatal("COM object allocation failed");
    object->cls = cls;
    object->guest = guest;
    object->refs = 1;
    MEM32(guest) = void_com_vtable(cls);
    MEM32(guest + 4) = index;
    memcpy(GPTR(guest + GUEST_HOST_POINTER), &object, sizeof object);
    return guest;
}

static __attribute__((noinline, cold)) void *com_host_slow(uint32_t guest)
{
    if (!guest || !void_vm_readable(guest, GUEST_HEADER)) void_lift_trap(g_cur_func, "COM call on an invalid object");
    uint32_t index = MEM32(guest + 4);
    void *object = index && index < MAX_OBJECTS ? objects[index] : NULL;
    if (!object || ((void_com_object *)object)->guest != guest) void_lift_trap(g_cur_func, "COM call on a released object");
    return object;
}

/* The objects a frame binds are thousands of separate allocations: the two reads below were an L3 line each,
 * the guest half and the host object's back pointer, per bind. This direct-mapped table answers a known object
 * without reading either; an entry is filled only after the checks and cleared with the object's last release, so
 * a stale or invalid pointer still reaches the checks. Entries change only under the guest machine lock. */
#define HOST_CACHE_BITS 14u
static struct { uint32_t guest; void *object; } host_cache[1u << HOST_CACHE_BITS];

/* A multiplicative hash: heap blocks of one size class share their low address bits. */
static inline uint32_t host_cache_index(uint32_t guest) { return (uint32_t)(guest * 2654435761u) >> (32u - HOST_CACHE_BITS); }

void *void_com_host(uint32_t guest)
{
    const uint32_t slot = host_cache_index(guest);
    if (__builtin_expect(host_cache[slot].guest == guest && guest, 1)) return host_cache[slot].object;
    /* Objects come from the guest heap: 8-byte aligned, above the null page and
     * inside the window. Anything else takes the checked path. */
    if (__builtin_expect(guest < 0x10000u || guest > 0xFFFFFFF0u - GUEST_HEADER || (guest & 7u), 0))
        return com_host_slow(guest);
    void_com_object *object;
    memcpy(&object, GPTR(guest + GUEST_HOST_POINTER), sizeof object);
    if (__builtin_expect(!object || object->guest != guest, 0)) return com_host_slow(guest);
    host_cache[slot].guest = guest;
    host_cache[slot].object = object;
    return object;
}

uint32_t void_com_addref(void_com_object *object) { return (uint32_t)++object->refs; }

uint32_t void_com_release(void_com_object *object)
{
    int refs = --object->refs;
    if (refs > 0) return (uint32_t)refs;
    uint32_t guest = object->guest;
    {
        const uint32_t slot = host_cache_index(guest);
        if (host_cache[slot].guest == guest) { host_cache[slot].guest = 0; host_cache[slot].object = NULL; }
    }
    pthread_mutex_lock(&com_lock);
    objects[MEM32(guest + 4)] = NULL;
    pthread_mutex_unlock(&com_lock);
    memset(GPTR(guest), 0, GUEST_HEADER);
    void_gfree(guest);
    if (object->destroy) object->destroy(object);
    else free(object);
    return 0;
}

int void_guid_equal(uint32_t guid_va, const uint8_t guid[16])
{
    return guid_va && !memcmp(GPTR(guid_va), guid, 16);
}

void void_guid_format(uint32_t g, char out[40])
{
    if (!g) { strcpy(out, "(null)"); return; }
    snprintf(out, 40, "{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", MEM32(g), MEM16(g + 4), MEM16(g + 6),
        MEM8(g + 8), MEM8(g + 9), MEM8(g + 10), MEM8(g + 11), MEM8(g + 12), MEM8(g + 13), MEM8(g + 14), MEM8(g + 15));
}
