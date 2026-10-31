#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstring>
#include <string>

using OpenVulkan=void*(*)(int,int,const char*,const char*,const char*,const char*,const char*,void**);
int main(int argc,char **argv) {
  if(argc!=2)return 2;
  std::string directory=std::string(argv[1])+"/";
  void *helper=dlopen((directory+"libadrenotools.so").c_str(),RTLD_NOW|RTLD_LOCAL);
  auto open=helper?reinterpret_cast<OpenVulkan>(dlsym(helper,"adrenotools_open_libvulkan")):nullptr;
  void *loader=open?open(RTLD_NOW|RTLD_LOCAL,1,nullptr,directory.c_str(),directory.c_str(),"libut3_turnip.so",nullptr,nullptr):nullptr;
  auto get=loader?reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(loader,"vkGetInstanceProcAddr")):nullptr;
  if(!get){printf("FAIL custom loader: %s\n",dlerror());return 2;}
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.pApplicationName="UT3 Turnip actual capability probe";app.apiVersion=VK_API_VERSION_1_3;
  VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ci.pApplicationInfo=&app;
  auto create=reinterpret_cast<PFN_vkCreateInstance>(get(nullptr,"vkCreateInstance"));
  VkInstance instance{};VkResult hr=create(&ci,nullptr,&instance);
  if(hr!=VK_SUCCESS){printf("FAIL vkCreateInstance1.3 result=%d\n",hr);return 2;}
  auto enumerate=reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(get(instance,"vkEnumeratePhysicalDevices"));
  auto properties=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(get(instance,"vkGetPhysicalDeviceProperties"));
  auto features=reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures>(get(instance,"vkGetPhysicalDeviceFeatures"));
  auto formats=reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties>(get(instance,"vkGetPhysicalDeviceFormatProperties"));
  uint32_t count=8;VkPhysicalDevice devices[8];hr=enumerate(instance,&count,devices);
  unsigned failures=hr!=VK_SUCCESS||!count;
  for(unsigned i=0;i<count&&i<8;++i){
    VkPhysicalDeviceProperties p{};VkPhysicalDeviceFeatures f{};properties(devices[i],&p);features(devices[i],&f);
    bool actual=p.apiVersion>=VK_API_VERSION_1_3&&p.vendorID==0x5143&&strstr(p.deviceName,"Turnip");
    if(!actual)++failures;
    printf("%s actual device=%s api=%u.%u.%u vendor=%04x deviceid=%04x BC=%u preciseOcclusion=%u\n",actual?"PASS":"FAIL",p.deviceName,VK_VERSION_MAJOR(p.apiVersion),VK_VERSION_MINOR(p.apiVersion),VK_VERSION_PATCH(p.apiVersion),p.vendorID,p.deviceID,f.textureCompressionBC,f.occlusionQueryPrecise);
    for(VkFormat format:{VK_FORMAT_BC1_RGBA_UNORM_BLOCK,VK_FORMAT_BC2_UNORM_BLOCK,VK_FORMAT_BC3_UNORM_BLOCK,VK_FORMAT_BC4_UNORM_BLOCK,VK_FORMAT_BC5_UNORM_BLOCK}){
      VkFormatProperties fp{};formats(devices[i],format,&fp);bool sampled=fp.optimalTilingFeatures&VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
      printf("%s format=%u optimal-sampled=%u\n",sampled?"PASS":"FAIL",format,unsigned(sampled));if(!sampled)++failures;
    }
  }
  reinterpret_cast<PFN_vkDestroyInstance>(get(instance,"vkDestroyInstance"))(instance,nullptr);
  printf("%s custom Turnip actual Vulkan1.3/BC1-5 capability probe failures=%u; no game or rendering\n",failures?"FAIL":"PASS",failures);
  return failures?1:0;
}
