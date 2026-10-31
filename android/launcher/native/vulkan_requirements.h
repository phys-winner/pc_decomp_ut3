/* What a Vulkan device needs for a port's DXVK renderer, as DXVK 2.4 itself decides it:
 * DxvkDeviceFilter::testAdapter (apiVersion >= 1.3), the required device extensions of
 * dxvk_extensions.h (VK_KHR_swapchain, VK_EXT_robustness2), D3D9DeviceEx::GetDeviceFeatures
 * (checked by DxvkAdapter::checkFeatureSupport) and the features DxvkAdapter::createDevice enables
 * unconditionally; the Android WSI needs VK_KHR_android_surface on the instance. The games keep
 * their textures as DXT1/3/5, which DXVK cannot decode: BC1-3 must be sampleable with optimal
 * tiling (port.json "needsBC": false drops that requirement). From the Heroes V port's
 * android/dxvk/vulkan_requirements.h. Pure C on Vulkan's headers. */
#ifndef PORT_VULKAN_REQUIREMENTS_H
#define PORT_VULKAN_REQUIREMENTS_H
#include <stdio.h>
#include <string.h>
#include <vulkan/vulkan_core.h>

typedef struct port_vk_caps {
    uint32_t api_version;
    int swapchain, robustness2, android_surface;             /* extensions */
    VkPhysicalDeviceFeatures core;
    VkBool32 sampler_mirror_clamp_to_edge, vulkan_memory_model, host_query_reset, timeline_semaphore;   /* 1.2 */
    VkBool32 shader_demote_to_helper_invocation, synchronization2, dynamic_rendering, maintenance4;     /* 1.3 */
    VkBool32 robust_buffer_access2, null_descriptor;          /* VK_EXT_robustness2 */
    int bc1, bc2, bc3;                                       /* optimal-tiling SAMPLED_IMAGE */
} port_vk_caps;

typedef struct { char *out; size_t size, used; int count; } port_vk_list;

static void port_vk_need(port_vk_list *list, int ok, const char *token)
{
    if (ok) return;
    if (list->size > list->used + 1) {
        int n = snprintf(list->out + list->used, list->size - list->used, "%s%s", list->count ? "," : "", token);
        if (n > 0) list->used = list->used + (size_t)n < list->size ? list->used + (size_t)n : list->size - 1;
    }
    ++list->count;
}

/* The missing requirements as comma-separated tokens (Renderers.java names them for the player);
 * returns how many. Without Vulkan 1.3 the 1.2/1.3 feature structures were not queried. */
static int port_vk_missing(const port_vk_caps *c, int needs_bc, char *out, size_t size)
{
    port_vk_list list = { out, size, 0, 0 };
    int v13 = c->api_version >= VK_API_VERSION_1_3;
    if (size) out[0] = 0;
    port_vk_need(&list, v13, "vulkan13");
    port_vk_need(&list, c->android_surface, "androidSurface");
    port_vk_need(&list, c->swapchain, "swapchain");
    port_vk_need(&list, c->robustness2, "robustness2");
    port_vk_need(&list, !c->robustness2 || c->robust_buffer_access2, "robustBufferAccess2");
    port_vk_need(&list, !c->robustness2 || c->null_descriptor, "nullDescriptor");
    port_vk_need(&list, c->core.robustBufferAccess, "robustBufferAccess");
    port_vk_need(&list, c->core.geometryShader, "geometryShader");
    port_vk_need(&list, c->core.imageCubeArray, "imageCubeArray");
    port_vk_need(&list, c->core.depthClamp, "depthClamp");
    port_vk_need(&list, c->core.depthBiasClamp, "depthBiasClamp");
    port_vk_need(&list, c->core.fillModeNonSolid, "fillModeNonSolid");
    port_vk_need(&list, c->core.sampleRateShading, "sampleRateShading");
    port_vk_need(&list, c->core.shaderClipDistance, "shaderClipDistance");
    port_vk_need(&list, c->core.shaderCullDistance, "shaderCullDistance");
    port_vk_need(&list, c->core.occlusionQueryPrecise, "occlusionQueryPrecise");
    port_vk_need(&list, c->core.multiViewport, "multiViewport");
    port_vk_need(&list, c->core.independentBlend, "independentBlend");
    port_vk_need(&list, c->core.fullDrawIndexUint32, "fullDrawIndexUint32");
    if (v13) {
        port_vk_need(&list, c->sampler_mirror_clamp_to_edge, "samplerMirrorClampToEdge");
        port_vk_need(&list, c->vulkan_memory_model, "vulkanMemoryModel");
        port_vk_need(&list, c->host_query_reset, "hostQueryReset");
        port_vk_need(&list, c->timeline_semaphore, "timelineSemaphore");
        port_vk_need(&list, c->shader_demote_to_helper_invocation, "shaderDemoteToHelperInvocation");
        port_vk_need(&list, c->synchronization2, "synchronization2");
        port_vk_need(&list, c->dynamic_rendering, "dynamicRendering");
        port_vk_need(&list, c->maintenance4, "maintenance4");
    }
    if (needs_bc) port_vk_need(&list, c->bc1 && c->bc2 && c->bc3, "bcTextures");
    return list.count;
}

#endif
