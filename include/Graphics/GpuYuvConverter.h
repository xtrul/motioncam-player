#ifndef GPU_YUV_CONVERTER_H
#define GPU_YUV_CONVERTER_H

#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>
#include "Utils/vma_usage.h"
#include "ffmpeg_headers.hpp"

class Renderer_VK;

class GpuYuvConverter {
public:
    explicit GpuYuvConverter(Renderer_VK* renderer);
    ~GpuYuvConverter();

    bool init(int width, int height);
    void cleanup();

    bool convertAndReadback(const uint16_t* raw, int width, int height,
                            std::vector<uint16_t>& outPacked);

    // Map the GPU YUV image directly into an AVFrame using proper row pitch
    bool copyFromGpuToFrame(AVFrame* frame);
private:
    Renderer_VK* m_renderer;

    int m_width = 0;
    int m_height = 0;

    VkPipeline m_pipeline{VK_NULL_HANDLE};
    VkPipelineLayout m_pipelineLayout{VK_NULL_HANDLE};
    VkDescriptorSetLayout m_setLayout{VK_NULL_HANDLE};
    VkDescriptorPool m_descPool{VK_NULL_HANDLE};
    VkDescriptorSet m_descSet{VK_NULL_HANDLE};

    VkImage m_yuvImage{VK_NULL_HANDLE};
    VmaAllocation m_yuvAlloc{VK_NULL_HANDLE};
    VkImageView m_yuvView{VK_NULL_HANDLE};

    VkCommandPool m_cmdPool{VK_NULL_HANDLE};

    VkImage m_rawImage{VK_NULL_HANDLE};
    VmaAllocation m_rawAlloc{VK_NULL_HANDLE};
    VkImageView m_rawView{VK_NULL_HANDLE};
    VkSampler m_rawSampler{VK_NULL_HANDLE};
};

#endif // GPU_YUV_CONVERTER_H
