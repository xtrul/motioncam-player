// In vma_usage.h or where you include vk_mem_alloc.h
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnullability-completeness"
#endif

#ifdef VMA_IMPLEMENTATION
    #define VMA_STATIC_VULKAN_FUNCTIONS 0
    #define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#endif
#include "vk_mem_alloc.h"

#if defined(__clang__)
#pragma clang diagnostic pop
#endif
