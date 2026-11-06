/* Single-queue FSR3 interpolation on DXVK's device; SDK owns the optical flow and interpolation. */
#include <d3d9.h>
#include <vulkan/vulkan.h>
#include <FidelityFX/host/ffx_frameinterpolation.h>
#include <FidelityFX/host/ffx_opticalflow.h>
#include <FidelityFX/host/backends/vk/ffx_vk.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>
#include "../dxvk/vulkan_interop.inc"
#include "motion_spirv.inc"

namespace {
PFN_vkGetDeviceProcAddr driverProc;
float frameCamera[12];
PFN_vkVoidFunction compatibleProc(VkDevice device,const char *name) {
    auto p=driverProc(device,name);
    if(!p){std::string core=name;if(core.size()>3&&core.substr(core.size()-3)=="KHR"){
        core.resize(core.size()-3);p=driverProc(device,core.c_str());}}
    return p;
}
void check(int code,const char *what){if(code)throw std::runtime_error(std::string(what)+": "+std::to_string(code));}
#define VK(x) check(int(x),#x)
#define FFX(x) check(int(x),#x)
#define VK_FUNCTIONS(X) \
 X(CreateCommandPool) X(DestroyCommandPool) X(AllocateCommandBuffers) X(ResetCommandPool) \
 X(BeginCommandBuffer) X(EndCommandBuffer) X(CreateFence) X(DestroyFence) X(WaitForFences) X(ResetFences) X(QueueSubmit) \
 X(CreateImage) X(DestroyImage) X(GetImageMemoryRequirements) X(AllocateMemory) X(FreeMemory) X(BindImageMemory) \
 X(CreateImageView) X(DestroyImageView) X(CreateSampler) X(DestroySampler) \
 X(CmdPipelineBarrier) X(CmdCopyImage) X(CmdBlitImage) X(CreateDescriptorSetLayout) X(DestroyDescriptorSetLayout) \
 X(CreateDescriptorPool) X(DestroyDescriptorPool) X(AllocateDescriptorSets) X(UpdateDescriptorSets) \
 X(CreatePipelineLayout) X(DestroyPipelineLayout) X(CreateShaderModule) X(DestroyShaderModule) X(CreateComputePipelines) X(DestroyPipeline) \
 X(CmdBindPipeline) X(CmdBindDescriptorSets) X(CmdPushConstants) X(CmdDispatch)
struct Context {
    VkInstance instance{};VkPhysicalDevice gpu{};VkDevice device{};VkQueue queue{};uint32_t family{},width{},height{};
    Ut3VkDevice *interop{};
#define DECL(name) PFN_vk##name name{};
    VK_FUNCTIONS(DECL)
#undef DECL
    VkCommandPool pool{};VkCommandBuffer cmd{};VkFence fence{};
    VkDescriptorSetLayout setLayout{};VkDescriptorPool descriptors{};VkDescriptorSet set{};
    VkPipelineLayout pipelineLayout{};VkPipeline pipeline{};VkSampler sampler{};
    VkImage depthCopy{};VkDeviceMemory depthMemory{};VkImageView depthView{};VkImageCreateInfo depthInfo{};
    bool depthInitialized=false,pending=false;uint64_t frameID=0;float previousFov=0;
    VkDeviceContext native{};std::vector<char> scratch;FfxInterface backend{};uint32_t sharedContext=0;
    std::unique_ptr<FfxFrameInterpolationContext> fi;std::unique_ptr<FfxOpticalflowContext> of;
    bool fiReady=false,ofReady=false;
    std::vector<FfxResourceInternal> owned;std::vector<VkImageView> views;
    FfxResource color{},output{},motion{},depth{},flow{},scd{},dilatedDepth{},dilatedMotion{},previousDepth{};
    VkImageView motionView{},outputDepthView{},flowView{};

    Context(uint32_t w,uint32_t h):width(w),height(h){}
    void init(IDirect3DDevice9 *d,PFN_vkGetInstanceProcAddr loader) {
        uint32_t w=width,h=height;
        VK(d->QueryInterface(UT3_VK_DEVICE,reinterpret_cast<void**>(&interop)));
        interop->GetVulkanHandles(&instance,&gpu,&device);uint32_t index;
        interop->GetSubmissionQueue(&queue,&index,&family);
        driverProc=reinterpret_cast<PFN_vkGetDeviceProcAddr>(loader(instance,"vkGetDeviceProcAddr"));
        if(!driverProc)throw std::runtime_error("Vulkan device procedure table unavailable");
#define LOAD(name) name=reinterpret_cast<PFN_vk##name>(compatibleProc(device,"vk" #name));if(!name)throw std::runtime_error("Missing Vulkan function: vk" #name);
        VK_FUNCTIONS(LOAD)
#undef LOAD
        native={device,gpu,compatibleProc};scratch.resize(ffxGetScratchMemorySizeVK(gpu,3));
        FFX(ffxGetInterfaceVK(&backend,ffxGetDeviceVK(&native),scratch.data(),scratch.size(),3));
        FfxFrameInterpolationContextDescription fid{};fid.backendInterface=backend;
        fid.flags=FFX_FRAMEINTERPOLATION_ENABLE_DEPTH_INFINITE;
        fid.maxRenderSize=fid.displaySize={w,h};
        fid.backBufferFormat=fid.previousInterpolationSourceFormat=FFX_SURFACE_FORMAT_R8G8B8A8_UNORM;
        fi=std::make_unique<FfxFrameInterpolationContext>();FFX(ffxFrameInterpolationContextCreate(fi.get(),&fid));fiReady=true;
        FfxOpticalflowContextDescription ofd{};ofd.backendInterface=backend;ofd.resolution={w,h};
        of=std::make_unique<FfxOpticalflowContext>();FFX(ffxOpticalflowContextCreate(of.get(),&ofd));ofReady=true;
        FFX(backend.fpCreateBackendContext(&backend,FFX_EFFECT_SHAREDRESOURCES,nullptr,&sharedContext));
        FfxOpticalflowSharedResourceDescriptions ofres{};FFX(ffxOpticalflowGetSharedResourceDescriptions(of.get(),&ofres));
        flow=resource(ofres.opticalFlowVector);scd=resource(ofres.opticalFlowSCD);
        FfxFrameInterpolationSharedResourceDescriptions fires{};FFX(ffxFrameInterpolationGetSharedResourceDescriptions(fi.get(),&fires));
        dilatedDepth=resource(fires.dilatedDepth);dilatedMotion=resource(fires.dilatedMotionVectors);
        previousDepth=resource(fires.reconstructedPrevNearestDepth);
        color=texture(FFX_SURFACE_FORMAT_R8G8B8A8_UNORM,L"UT3 current");output=texture(FFX_SURFACE_FORMAT_R8G8B8A8_UNORM,L"UT3 interpolated");
        motion=texture(FFX_SURFACE_FORMAT_R16G16_FLOAT,L"UT3 optical motion");depth=texture(FFX_SURFACE_FORMAT_R32_FLOAT,L"UT3 scene depth");
        motionView=view(motion,VK_FORMAT_R16G16_SFLOAT);outputDepthView=view(depth,VK_FORMAT_R32_SFLOAT);
        flowView=view(flow,VK_FORMAT_R16G16_SINT);
        VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pi.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;pi.queueFamilyIndex=family;
        VK(CreateCommandPool(device,&pi,nullptr,&pool));
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ai.commandPool=pool;ai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ai.commandBufferCount=1;
        VK(AllocateCommandBuffers(device,&ai,&cmd));
        VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};fci.flags=VK_FENCE_CREATE_SIGNALED_BIT;VK(CreateFence(device,&fci,nullptr,&fence));
        VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};sci.magFilter=sci.minFilter=VK_FILTER_NEAREST;sci.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sci.addressModeU=sci.addressModeV=sci.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;VK(CreateSampler(device,&sci,nullptr,&sampler));
        VkDescriptorSetLayoutBinding bindings[4]{};
        for(unsigned i=0;i<4;++i)bindings[i]={i,i<2?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
        VkDescriptorSetLayoutCreateInfo sl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};sl.bindingCount=4;sl.pBindings=bindings;VK(CreateDescriptorSetLayout(device,&sl,nullptr,&setLayout));
        VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,2},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,2}};
        VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};dp.maxSets=1;dp.poolSizeCount=2;dp.pPoolSizes=sizes;VK(CreateDescriptorPool(device,&dp,nullptr,&descriptors));
        VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};da.descriptorPool=descriptors;da.descriptorSetCount=1;da.pSetLayouts=&setLayout;VK(AllocateDescriptorSets(device,&da,&set));
        VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT,0,24};VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pl.setLayoutCount=1;pl.pSetLayouts=&setLayout;pl.pushConstantRangeCount=1;pl.pPushConstantRanges=&range;
        VK(CreatePipelineLayout(device,&pl,nullptr,&pipelineLayout));
        VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};sm.codeSize=sizeof motion_spirv;sm.pCode=motion_spirv;VkShaderModule shader;
        VK(CreateShaderModule(device,&sm,nullptr,&shader));
        VkComputePipelineCreateInfo cp{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};cp.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_COMPUTE_BIT,shader,"main",nullptr};cp.layout=pipelineLayout;
        VkResult status=CreateComputePipelines(device,VK_NULL_HANDLE,1,&cp,nullptr,&pipeline);DestroyShaderModule(device,shader,nullptr);VK(status);
    }
    FfxResource resource(const FfxCreateResourceDescription &desc) {
        FfxResourceInternal res{};FFX(backend.fpCreateResource(&backend,&desc,sharedContext,&res));owned.push_back(res);
        return backend.fpGetResource(&backend,res);
    }
    FfxResource texture(FfxSurfaceFormat format,const wchar_t *name) {
        FfxCreateResourceDescription desc{};desc.heapType=FFX_HEAP_TYPE_DEFAULT;
        desc.resourceDescription={FFX_RESOURCE_TYPE_TEXTURE2D,format,width,height,1,1,FFX_RESOURCE_FLAGS_NONE,FFX_RESOURCE_USAGE_UAV};
        desc.initialState=FFX_RESOURCE_STATE_UNORDERED_ACCESS;desc.name=name;
        desc.initData.type=FFX_RESOURCE_INIT_DATA_TYPE_UNINITIALIZED;return resource(desc);
    }
    VkImage image(const FfxResource &r){return reinterpret_cast<VkImage>(r.resource);}
    VkImageView view(const FfxResource &r,VkFormat format) {
        VkImageViewCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};ci.image=image(r);ci.viewType=VK_IMAGE_VIEW_TYPE_2D;ci.format=format;
        ci.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};VkImageView result;VK(CreateImageView(device,&ci,nullptr,&result));views.push_back(result);return result;
    }
    void initialize(FfxResource &r) {
        if(r.description.flags&FFX_RESOURCE_FLAGS_UNDEFINED){
            barrier(image(r),VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_GENERAL);
            r.description.flags=FfxResourceFlags(r.description.flags&~FFX_RESOURCE_FLAGS_UNDEFINED);
        }
    }
    void begin() {
        if(pending){VK(WaitForFences(device,1,&fence,VK_TRUE,UINT64_MAX));pending=false;}
        interop->FlushRenderingCommands();VK(ResetCommandPool(device,pool,0));
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;VK(BeginCommandBuffer(cmd,&bi));
    }
    void submit() {
        VK(EndCommandBuffer(cmd));VK(ResetFences(device,1,&fence));
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};si.commandBufferCount=1;si.pCommandBuffers=&cmd;
        interop->LockSubmissionQueue();VkResult status=QueueSubmit(queue,1,&si,fence);interop->ReleaseSubmissionQueue();pending=status==VK_SUCCESS;VK(status);
    }
    void barrier(VkImage img,VkImageLayout oldLayout,VkImageLayout next,VkImageAspectFlags aspect=VK_IMAGE_ASPECT_COLOR_BIT) {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.srcAccessMask=oldLayout==VK_IMAGE_LAYOUT_UNDEFINED?0:VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
        b.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;b.oldLayout=oldLayout;b.newLayout=next;
        b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.image=img;b.subresourceRange={aspect,0,1,0,1};
        CmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);
    }
    void blit(VkImage src,VkImage dst,uint32_t w,uint32_t h) {
        VkImageBlit region{};region.srcSubresource=region.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
        region.srcOffsets[1]=region.dstOffsets[1]={int32_t(w),int32_t(h),1};
        CmdBlitImage(cmd,src,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,dst,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region,VK_FILTER_NEAREST);
    }
    void cloneDepth(const VkImageCreateInfo &original) {
        if(depthCopy&&depthInfo.format==original.format&&depthInfo.extent.width==original.extent.width&&depthInfo.extent.height==original.extent.height)return;
        if(depthView)DestroyImageView(device,depthView,nullptr);
        if(depthCopy)DestroyImage(device,depthCopy,nullptr);if(depthMemory)FreeMemory(device,depthMemory,nullptr);
        depthView=VK_NULL_HANDLE;depthCopy=VK_NULL_HANDLE;depthMemory=VK_NULL_HANDLE;
        depthInfo=original;depthInfo.pNext=nullptr;depthInfo.flags=0;depthInfo.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
        depthInfo.sharingMode=VK_SHARING_MODE_EXCLUSIVE;depthInfo.queueFamilyIndexCount=0;depthInfo.pQueueFamilyIndices=nullptr;depthInfo.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
        VK(CreateImage(device,&depthInfo,nullptr,&depthCopy));VkMemoryRequirements req;GetImageMemoryRequirements(device,depthCopy,&req);
        VkPhysicalDeviceMemoryProperties memory{};vkGetPhysicalDeviceMemoryProperties(gpu,&memory);uint32_t type=0;
        while(type<memory.memoryTypeCount&&(!(req.memoryTypeBits&(1u<<type))||!(memory.memoryTypes[type].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)))++type;
        if(type==memory.memoryTypeCount)throw std::runtime_error("No device memory for scene depth");
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;ai.memoryTypeIndex=type;VK(AllocateMemory(device,&ai,nullptr,&depthMemory));VK(BindImageMemory(device,depthCopy,depthMemory,0));
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vi.image=depthCopy;vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.format=original.format;vi.subresourceRange={VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,1};VK(CreateImageView(device,&vi,nullptr,&depthView));depthInitialized=false;
    }
    bool frame(IDirect3DSurface9 *bb,IDirect3DSurface9 *sceneDepth,const uint32_t rect[4],float hfov,float nearClip,float dt,bool reset,bool restore) {
        Ut3VkTexture *bbi=nullptr,*dsi=nullptr;VK(bb->QueryInterface(UT3_VK_TEXTURE,reinterpret_cast<void**>(&bbi)));
        VkImage bbImage;VkImageLayout bbLayout;VkImageCreateInfo bbInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        HRESULT hr=bbi->GetVulkanImageInfo(&bbImage,&bbLayout,&bbInfo);bbi->Release();VK(hr);
        begin();
        if(restore){
            barrier(image(color),VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);barrier(bbImage,bbLayout,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            blit(image(color),bbImage,width,height);barrier(bbImage,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,bbLayout);barrier(image(color),VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL);submit();return true;
        }
        for(FfxResource *r:{&color,&output,&motion,&depth,&flow,&scd,&dilatedDepth,&dilatedMotion,&previousDepth})initialize(*r);
        VK(sceneDepth->QueryInterface(UT3_VK_TEXTURE,reinterpret_cast<void**>(&dsi)));
        VkImage dsImage;VkImageLayout dsLayout;VkImageCreateInfo dsInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        hr=dsi->GetVulkanImageInfo(&dsImage,&dsLayout,&dsInfo);dsi->Release();VK(hr);
        if(dsInfo.samples!=VK_SAMPLE_COUNT_1_BIT||!(dsInfo.usage&VK_IMAGE_USAGE_TRANSFER_SRC_BIT))throw std::runtime_error("Unsupported multisampled or non-transferable scene depth");
        cloneDepth(dsInfo);
        barrier(bbImage,bbLayout,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);barrier(image(color),VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        blit(bbImage,image(color),width,height);barrier(bbImage,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,bbLayout);barrier(image(color),VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL);
        VkImageAspectFlags aspects=VK_IMAGE_ASPECT_DEPTH_BIT;
        if(dsInfo.format==VK_FORMAT_D24_UNORM_S8_UINT||dsInfo.format==VK_FORMAT_D32_SFLOAT_S8_UINT||dsInfo.format==VK_FORMAT_D16_UNORM_S8_UINT)aspects|=VK_IMAGE_ASPECT_STENCIL_BIT;
        barrier(dsImage,dsLayout,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,aspects);barrier(depthCopy,depthInitialized?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,aspects);
        VkImageCopy dc{};dc.srcSubresource=dc.dstSubresource={VK_IMAGE_ASPECT_DEPTH_BIT,0,0,1};dc.extent=dsInfo.extent;
        CmdCopyImage(cmd,dsImage,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,depthCopy,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&dc);
        barrier(dsImage,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,dsLayout,aspects);barrier(depthCopy,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL,aspects);depthInitialized=true;
        reset=reset||!frameID||dt>250||std::abs(hfov-previousFov)>.05f;previousFov=hfov;
        FfxOpticalflowDispatchDescription optical{};optical.commandList=ffxGetCommandListVK(cmd);optical.color=color;
        optical.opticalFlowVector=flow;optical.opticalFlowSCD=scd;optical.reset=reset;optical.backbufferTransferFunction=FFX_BACKBUFFER_TRANSFER_FUNCTION_SRGB;optical.minMaxLuminance={0,1};
        FFX(ffxOpticalflowContextDispatch(of.get(),&optical));barrier(image(flow),VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL);
        VkDescriptorImageInfo info[]={{sampler,flowView,VK_IMAGE_LAYOUT_GENERAL},{sampler,depthView,VK_IMAGE_LAYOUT_GENERAL},{VK_NULL_HANDLE,motionView,VK_IMAGE_LAYOUT_GENERAL},{VK_NULL_HANDLE,outputDepthView,VK_IMAGE_LAYOUT_GENERAL}};
        VkWriteDescriptorSet writes[4]{};for(unsigned i=0;i<4;++i){writes[i]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};writes[i].dstSet=set;writes[i].dstBinding=i;writes[i].descriptorCount=1;writes[i].descriptorType=i<2?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;writes[i].pImageInfo=&info[i];}
        UpdateDescriptorSets(device,4,writes,0,nullptr);CmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);CmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,pipelineLayout,0,1,&set,0,nullptr);
        float push[]={float(rect[0]),float(rect[1]),float(rect[2]),float(rect[3]),float(width),float(height)};CmdPushConstants(cmd,pipelineLayout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof push,push);CmdDispatch(cmd,(width+7)/8,(height+7)/8,1);
        barrier(image(motion),VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL);barrier(image(depth),VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL);
        float vfov=2*std::atan(std::tan(hfov*.5f)*float(height)/width);
        FfxFrameInterpolationPrepareDescription prepare{};prepare.commandList=ffxGetCommandListVK(cmd);prepare.renderSize={width,height};prepare.motionVectorScale={float(width),float(height)};
        prepare.frameTimeDelta=dt;prepare.cameraNear=nearClip;prepare.cameraFar=1.e30f;prepare.cameraFovAngleVertical=vfov;prepare.viewSpaceToMetersFactor=.01f;
        prepare.depth=depth;prepare.motionVectors=motion;prepare.frameID=frameID;
        prepare.dilatedDepth=dilatedDepth;prepare.dilatedMotionVectors=dilatedMotion;prepare.reconstructedPrevDepth=previousDepth;
        memcpy(prepare.cameraPosition,frameCamera,12);memcpy(prepare.cameraUp,frameCamera+3,12);
        memcpy(prepare.cameraRight,frameCamera+6,12);memcpy(prepare.cameraForward,frameCamera+9,12);
        FFX(ffxFrameInterpolationPrepare(fi.get(),&prepare));
        FfxFrameInterpolationDispatchDescription dispatch{};dispatch.commandList=prepare.commandList;dispatch.displaySize=dispatch.renderSize={width,height};
        dispatch.currentBackBuffer=color;dispatch.output=output;dispatch.interpolationRect={0,0,int32_t(width),int32_t(height)};
        dispatch.opticalFlowVector=flow;dispatch.opticalFlowSceneChangeDetection=scd;dispatch.opticalFlowBufferSize={flow.description.width,flow.description.height};dispatch.opticalFlowScale={1.f/width,1.f/height};dispatch.opticalFlowBlockSize=8;
        dispatch.cameraNear=nearClip;dispatch.cameraFar=1.e30f;dispatch.cameraFovAngleVertical=vfov;dispatch.viewSpaceToMetersFactor=.01f;dispatch.frameTimeDelta=dt;dispatch.reset=reset;
        dispatch.backBufferTransferFunction=FFX_BACKBUFFER_TRANSFER_FUNCTION_SRGB;dispatch.minMaxLuminance[1]=1;dispatch.frameID=frameID++;
        dispatch.dilatedDepth=dilatedDepth;dispatch.dilatedMotionVectors=dilatedMotion;dispatch.reconstructedPrevDepth=previousDepth;
        FFX(ffxFrameInterpolationDispatch(fi.get(),&dispatch));
        if(!reset){barrier(image(output),VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);barrier(bbImage,bbLayout,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);blit(image(output),bbImage,width,height);barrier(bbImage,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,bbLayout);barrier(image(output),VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL);}
        submit();return !reset;
    }
    ~Context() {
        if(pending)WaitForFences(device,1,&fence,VK_TRUE,UINT64_MAX);
        if(pipeline)DestroyPipeline(device,pipeline,nullptr);if(pipelineLayout)DestroyPipelineLayout(device,pipelineLayout,nullptr);
        if(descriptors)DestroyDescriptorPool(device,descriptors,nullptr);if(setLayout)DestroyDescriptorSetLayout(device,setLayout,nullptr);
        if(sampler)DestroySampler(device,sampler,nullptr);if(depthView)DestroyImageView(device,depthView,nullptr);
        if(depthCopy)DestroyImage(device,depthCopy,nullptr);if(depthMemory)FreeMemory(device,depthMemory,nullptr);
        for(auto v:views)DestroyImageView(device,v,nullptr);
        for(auto r:owned)backend.fpDestroyResource(&backend,r,sharedContext);
        if(ofReady)ffxOpticalflowContextDestroy(of.get());if(fiReady)ffxFrameInterpolationContextDestroy(fi.get());
        if(sharedContext)backend.fpDestroyBackendContext(&backend,sharedContext);
        if(fence)DestroyFence(device,fence,nullptr);if(pool)DestroyCommandPool(device,pool,nullptr);if(interop)interop->Release();
    }
};
std::unique_ptr<Context> context;IDirect3DDevice9 *owner;char lastError[1024];
}
extern "C" __attribute__((visibility("default"))) const char *ut3_fsr3_error(){return lastError;}
extern "C" __attribute__((visibility("default"))) void ut3_fsr3_camera(const float *camera){memcpy(frameCamera,camera,sizeof frameCamera);}
extern "C" __attribute__((visibility("default"))) void ut3_fsr3_reset(){context.reset();owner=nullptr;}
extern "C" __attribute__((visibility("default"))) int ut3_fsr3_frame(void *device,void *depth,const uint32_t rect[4],float hfov,float nearClip,float dt,int reset,int restore,PFN_vkGetInstanceProcAddr loader) {
    try {
        auto *d=static_cast<IDirect3DDevice9*>(device);IDirect3DSurface9 *bb=nullptr;VK(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&bb));
        D3DSURFACE_DESC desc{};HRESULT hr=bb->GetDesc(&desc);if(FAILED(hr)){bb->Release();VK(hr);}
        bool result;
        try{
            if(!context||owner!=d||context->width!=desc.Width||context->height!=desc.Height){context.reset();auto next=std::make_unique<Context>(desc.Width,desc.Height);next->init(d,loader);context=std::move(next);owner=d;}
            result=context->frame(bb,static_cast<IDirect3DSurface9*>(depth),rect,hfov,nearClip,dt,reset,restore);
        }
        catch(...){bb->Release();throw;}
        bb->Release();return result?1:0;
    }catch(const std::exception &error){snprintf(lastError,sizeof lastError,"%s",error.what());return -1;}
}
