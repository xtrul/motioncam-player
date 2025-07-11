#pragma once
#include <vulkan/vulkan.h>
#include <mutex>

extern std::mutex gVkQueueSubmitMutex;

inline VkResult queueSubmitLocked(VkQueue queue,
                                  uint32_t submitCount,
                                  const VkSubmitInfo* pSubmits,
                                  VkFence fence)
{
    std::lock_guard<std::mutex> lock(gVkQueueSubmitMutex);
    return vkQueueSubmit(queue, submitCount, pSubmits, fence);
}
