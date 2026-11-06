/* DXVK's public D3D9 Vulkan interop ABI (DXVK, zlib licence). */
#pragma once
#include <d3d9.h>
#include <vulkan/vulkan.h>
struct Ut3VkTexture : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetVulkanImageInfo(VkImage*,VkImageLayout*,VkImageCreateInfo*)=0;
};
struct Ut3VkDevice : IUnknown {
    virtual void STDMETHODCALLTYPE GetVulkanHandles(VkInstance*,VkPhysicalDevice*,VkDevice*)=0;
    virtual void STDMETHODCALLTYPE GetSubmissionQueue(VkQueue*,uint32_t*,uint32_t*)=0;
    virtual void STDMETHODCALLTYPE TransitionTextureLayout(Ut3VkTexture*,const VkImageSubresourceRange*,VkImageLayout,VkImageLayout)=0;
    virtual void STDMETHODCALLTYPE FlushRenderingCommands()=0;
    virtual void STDMETHODCALLTYPE LockSubmissionQueue()=0;
    virtual void STDMETHODCALLTYPE ReleaseSubmissionQueue()=0;
};
static const GUID UT3_VK_TEXTURE={0xd56344f5,0x8d35,0x46fd,{0x80,0x6d,0x94,0xc3,0x51,0xb4,0x72,0xc1}};
static const GUID UT3_VK_DEVICE={0x2eaa4b89,0x0107,0x4bdb,{0x87,0xf7,0x0f,0x54,0x1c,0x49,0x3c,0xe0}};
