#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>
#include "Utils/vma_usage.h"
#include "Graphics/Renderer_VK.h"

class GpuDctQuantizer {
public:
    explicit GpuDctQuantizer(Renderer_VK* renderer);
    ~GpuDctQuantizer();

    bool init(int width, int height);
    void cleanup();

    bool process(VkImage luma, VkImage chromaU, VkImage chromaV, uint32_t* outBuf, size_t bufSize);

private:
    Renderer_VK* m_renderer;
    int m_width{0};
    int m_height{0};
    VkPipeline m_pipeline{VK_NULL_HANDLE};
    VkPipelineLayout m_layout{VK_NULL_HANDLE};
    VkDescriptorSetLayout m_setLayout{VK_NULL_HANDLE};
    VkDescriptorPool m_pool{VK_NULL_HANDLE};
    VkDescriptorSet m_set{VK_NULL_HANDLE};
    VkBuffer m_outBuffer{VK_NULL_HANDLE};
    VmaAllocation m_outAlloc{VK_NULL_HANDLE};
};
