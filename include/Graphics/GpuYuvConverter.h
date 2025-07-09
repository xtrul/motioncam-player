#pragma once
#include <vulkan/vulkan.h>
#include "Utils/vma_usage.h"

class GpuYuvConverter {
public:
    GpuYuvConverter(VkDevice device, VmaAllocator allocator, VkQueue queue, VkCommandPool cmdPool);
    ~GpuYuvConverter();

    bool init(uint32_t width, uint32_t height, uint32_t ringSize);
    void cleanup();

    VkImage getImage(uint32_t index) const;

private:
    VkDevice m_device;
    VmaAllocator m_allocator;
    VkQueue m_queue;
    VkCommandPool m_cmdPool;
    uint32_t m_ringSize{0};
    std::vector<VkImage> m_images;
};
