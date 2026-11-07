/* UT3's PC hardware appraisal cannot classify Android renderers or CPUs. */
#include "host.h"

static void_fn physx_scene, physx_cloth, physx_fluid;
extern void __real_sub_010FC820(void);

void __wrap_sub_010FC820(void)
{
    static int last=-1;
    __real_sub_010FC820();
    if ((int)g_eax != last) {
        last=(int)g_eax;
        void_log("[physx] original effects switch: %s",last?"on (CPU compatibility)":"off");
    }
}

static void physx_cpu_create(void_fn original, uint32_t offset, uint32_t hardware, const char *kind)
{
    uint32_t descriptor = ARG(0);
    if (descriptor > UINT32_MAX - offset - 4 || !void_vm_writable(descriptor + offset, 4))
        void_fatal("Invalid PhysX %s descriptor", kind);
    uint32_t field = descriptor + offset, previous = MEM32(field);
    /* PhysX 2.7.3: scene.simType=1, cloth.flags & 0x2000, fluid.flags & 0x10 select the PPU. */
    uint32_t cpu = hardware ? previous & ~hardware : previous == 1 ? 0 : previous;
    uint32_t compartment = hardware == 0x2000 && cpu != previous ? descriptor + 0x10C : 0;
    if (compartment && !void_vm_writable(compartment, 4)) void_fatal("Invalid PhysX cloth compartment");
    uint32_t previous_compartment = compartment ? MEM32(compartment) : 0;
    /* SDK 2.7.3 rejects a cloth compartment unless NX_CLF_HARDWARE is set (1E13CCBB). */
    if (compartment) MEM32(compartment) = 0;
    MEM32(field) = cpu;
    original();
    MEM32(field) = previous;
    if (compartment) MEM32(compartment) = previous_compartment;
    if (cpu != previous) {
        static unsigned reports;
        if (reports++ < 16) void_log("[physx] %s hardware effects -> original CPU simulation: %s", kind, g_eax ? "created" : "creation failed");
    }
}

static void physx_cpu_scene(void) { physx_cpu_create(physx_scene, 0x38, 0, "scene"); }
static void physx_cpu_cloth(void) { physx_cpu_create(physx_cloth, 0x100, 0x2000, "cloth"); }
static void physx_cpu_fluid(void) { physx_cpu_create(physx_fluid, 0x11C, 0x10, "fluid"); }

static void physx_effects_available(void)
{
    /* The compatibility processor implements the effects on the CPU; no PPU/CUDA device is enumerated. */
    g_eax = 1;
    g_esp += 4;
}

void __wrap_sub_00B6AF10(void)
{
    uint32_t result = ARG(0);
    if (!void_vm_writable(result, 12)) void_fatal("Invalid compatibility result buffer");
    /* Match the disabled appraisal's saved levels; keep the player's SystemSettings. */
    MEM32(result) = MEM32(result + 4) = MEM32(result + 8) = 5;
    g_eax = result;
    g_esp += 4;
}

extern void void_physx_native_init(void);

void void_features_init(void)
{
    void_physx_native_init();
    void_dispatch_add(0x00B6AF10u, __wrap_sub_00B6AF10);
    physx_scene = void_dispatch_find(0x1E12A1D0u);
    physx_cloth = void_dispatch_find(0x1E13C930u);
    physx_fluid = void_dispatch_find(0x1E13AED0u);
    if (!physx_scene || !physx_cloth || !physx_fluid) void_fatal("PhysX CPU compatibility entry points unavailable");
    void_dispatch_add(0x1E12A1D0u, physx_cpu_scene);
    void_dispatch_add(0x1E13C930u, physx_cpu_cloth);
    void_dispatch_add(0x1E13AED0u, physx_cpu_fluid);
    void_dispatch_add(0x1E1276D0u, physx_effects_available);
    void_dispatch_add(0x010FC820u,__wrap_sub_010FC820);
    void_log("[ut3] PC hardware appraisal disabled; graphics are selected by the player");
    void_log("[physx] hardware-effects compatibility uses the original CPU solver (scenes, cloth, fluids); no PPU/CUDA");
}
