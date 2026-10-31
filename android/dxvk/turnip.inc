/* The caller retains this process-lifetime custom loader before creating DXVK. `custom` is the
 * player's own driver chosen in the launcher (PORT_TURNIP_DRIVER, a full path), else the port's
 * libut3_turnip.so beside the native libraries; adrenotools' hooks stay the port's either way. */
#pragma once
#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstring>
#include <string>

inline PFN_vkGetInstanceProcAddr ut3_open_turnip(const char *directory,const char *custom,char *error,size_t error_size) {
  if(!directory||!*directory){snprintf(error,error_size,"Turnip native library directory is empty");return nullptr;}
  std::string path=std::string(directory)+"/",folder=path,driver="libut3_turnip.so";
  if(custom&&*custom=='/'){std::string own=custom;auto slash=own.find_last_of('/');folder=own.substr(0,slash+1);driver=own.substr(slash+1);}
  void *helper=dlopen((path+"libadrenotools.so").c_str(),RTLD_NOW|RTLD_LOCAL);
  using Open=void*(*)(int,int,const char*,const char*,const char*,const char*,const char*,void**);
  auto open=helper?reinterpret_cast<Open>(dlsym(helper,"adrenotools_open_libvulkan")):nullptr;
  void *loader=open?open(RTLD_NOW|RTLD_LOCAL,1,nullptr,path.c_str(),folder.c_str(),driver.c_str(),nullptr,nullptr):nullptr;
  auto get=loader?reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(loader,"vkGetInstanceProcAddr")):nullptr;
  if(!get){const char *detail=dlerror();snprintf(error,error_size,"Turnip loader failed: %s",detail?detail:"no custom driver returned");return nullptr;}
  auto create=reinterpret_cast<PFN_vkCreateInstance>(get(nullptr,"vkCreateInstance"));
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.pApplicationName="UT3 driver validation";app.apiVersion=VK_API_VERSION_1_3;
  VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ci.pApplicationInfo=&app;
  VkInstance instance{};VkResult hr=create?create(&ci,nullptr,&instance):VK_ERROR_INITIALIZATION_FAILED;
  if(hr!=VK_SUCCESS){snprintf(error,error_size,"Turnip Vulkan1.3 instance failed: %d",hr);return nullptr;}
  auto enumerate=reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(get(instance,"vkEnumeratePhysicalDevices"));
  auto properties=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(get(instance,"vkGetPhysicalDeviceProperties"));
  auto properties2=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(get(instance,"vkGetPhysicalDeviceProperties2"));
  auto destroy=reinterpret_cast<PFN_vkDestroyInstance>(get(instance,"vkDestroyInstance"));
  bool supported=false;uint32_t count=8;VkPhysicalDevice devices[8];
  if(enumerate&&properties&&destroy&&enumerate(instance,&count,devices)==VK_SUCCESS)
    for(unsigned i=0;i<count&&i<8;++i){VkPhysicalDeviceProperties p{};properties(devices[i],&p);
      VkPhysicalDeviceDriverProperties d{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
      VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,&d};
      if(properties2&&p.apiVersion>=VK_API_VERSION_1_2)properties2(devices[i],&p2);
      /* Mesa Turnip, not the system's Qualcomm driver: builds of it rename the device, never the driver id. */
      supported|=p.apiVersion>=VK_API_VERSION_1_3&&p.vendorID==0x5143&&(d.driverID==VK_DRIVER_ID_MESA_TURNIP||strstr(p.deviceName,"Turnip"));}
  if(destroy)destroy(instance,nullptr);
  if(!supported){snprintf(error,error_size,"Turnip did not expose a Qualcomm Vulkan1.3 device");return nullptr;}
  return get;
}
