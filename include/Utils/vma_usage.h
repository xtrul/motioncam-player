#ifndef VMA_USAGE_H
#define VMA_USAGE_H

// This header configures and includes the Vulkan Memory Allocator library (VMA).
// VMA_IMPLEMENTATION should be defined in exactly one .cpp file (e.g., Renderer_VK.cpp)
// before including this header, to compile the library's implementation.

// Ensure vk_mem_alloc.h is available in the include path.
// Download from: https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/

#ifdef VMA_IMPLEMENTATION
    // Use dynamically loaded Vulkan functions. VMA will fetch function pointers
    // using vkGetInstanceProcAddr and vkGetDeviceProcAddr.
    // Set VMA_STATIC_VULKAN_FUNCTIONS to 0 if you are not linking directly against vulkan-1.lib
    // and are using a loader like Volk or letting GLFW/SDL load them.
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#endif

#include "vk_mem_alloc.h" // The actual VMA library header

#endif // VMA_USAGE_H