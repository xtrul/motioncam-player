#include <gtest/gtest.h>
#include "Graphics/GpuYuvConverter.h"
#include "Graphics/Renderer_VK.h"
#include <vulkan/vulkan.h>

// Minimal Vulkan setup for test
struct MinimalVk {
    VkInstance instance{VK_NULL_HANDLE};
    VkPhysicalDevice phys{VK_NULL_HANDLE};
    VkDevice device{VK_NULL_HANDLE};
    VmaAllocator alloc{VK_NULL_HANDLE};
    VkQueue queue{VK_NULL_HANDLE};
    VkCommandPool pool{VK_NULL_HANDLE};
    bool init(){
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.apiVersion = VK_API_VERSION_1_1;
        VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        ci.pApplicationInfo=&app;
        if(vkCreateInstance(&ci,nullptr,&instance)!=VK_SUCCESS) return false;
        uint32_t count=0; vkEnumeratePhysicalDevices(instance,&count,nullptr);
        if(count==0) return false; std::vector<VkPhysicalDevice> devs(count); vkEnumeratePhysicalDevices(instance,&count,devs.data());
        phys=devs[0];
        uint32_t qCount=0; vkGetPhysicalDeviceQueueFamilyProperties(phys,&qCount,nullptr);
        std::vector<VkQueueFamilyProperties> qProps(qCount); vkGetPhysicalDeviceQueueFamilyProperties(phys,&qCount,qProps.data());
        uint32_t qFamily=0; for(uint32_t i=0;i<qCount;++i){ if(qProps[i].queueFlags&VK_QUEUE_GRAPHICS_BIT){qFamily=i;break;}}
        float pr=1.f; VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        qci.queueFamilyIndex=qFamily; qci.queueCount=1; qci.pQueuePriorities=&pr;
        VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; dci.queueCreateInfoCount=1; dci.pQueueCreateInfos=&qci;
        if(vkCreateDevice(phys,&dci,nullptr,&device)!=VK_SUCCESS) return false;
        vkGetDeviceQueue(device,qFamily,0,&queue);
        VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pci.queueFamilyIndex=qFamily; pci.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        if(vkCreateCommandPool(device,&pci,nullptr,&pool)!=VK_SUCCESS) return false;
        VmaAllocatorCreateInfo ai{}; ai.vulkanApiVersion=VK_API_VERSION_1_1; ai.physicalDevice=phys; ai.device=device; ai.instance=instance; VmaVulkanFunctions funcs{}; funcs.vkGetInstanceProcAddr=vkGetInstanceProcAddr; funcs.vkGetDeviceProcAddr=vkGetDeviceProcAddr; ai.pVulkanFunctions=&funcs;
        if(vmaCreateAllocator(&ai,&alloc)!=VK_SUCCESS) return false;
        return true;
    }
    void cleanup(){ if(alloc) vmaDestroyAllocator(alloc); if(pool) vkDestroyCommandPool(device,pool,nullptr); if(device) vkDestroyDevice(device,nullptr); if(instance) vkDestroyInstance(instance,nullptr); }
};

TEST(GpuYuvConverterTest, SimpleConversion){
    MinimalVk vk; if(!vk.init()) GTEST_SKIP();
    Renderer_VK renderer(vk.phys,vk.device,vk.alloc,vk.queue,vk.pool);
    GpuYuvConverter conv(&renderer);
    ASSERT_TRUE(conv.init(2,2));
    uint16_t pattern[4]={0,65535,65535,0};
    std::vector<uint16_t> out; conv.convertAndReadback(pattern,2,2,0,out);
    double sumU=0,sumV=0; for(size_t i=0;i<out.size();i+=4){ sumU+=out[i+1]; sumV+=out[i+3]; }
    ASSERT_NE(sumU/2.0,0); ASSERT_NE(sumV/2.0,0);
    conv.cleanup(); vk.cleanup();
}
