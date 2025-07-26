#ifndef GPU_YUV_CONVERTER_H
#define GPU_YUV_CONVERTER_H

#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>
#include <libavutil/frame.h>
#include "Utils/vma_usage.h"
#include "Graphics/GpuColorParams.h"

class Renderer_VK;

class GpuYuvConverter {
public:
    explicit GpuYuvConverter(Renderer_VK* renderer);
    ~GpuYuvConverter();

    bool init(int width, int height);
    void cleanup();

    bool convertToFrame(const uint16_t* raw, int width, int height, AVFrame* frame,
                        const GpuColorParams& params);
private:
    Renderer_VK* m_renderer;

    VkPipeline m_pipeline{VK_NULL_HANDLE};
    VkPipelineLayout m_pipelineLayout{VK_NULL_HANDLE};

    VkPipeline m_amazePipeline{VK_NULL_HANDLE};
    VkPipelineLayout m_amazePipelineLayout{VK_NULL_HANDLE};
    VkPipeline m_rgb2yuvPipeline{VK_NULL_HANDLE};
    VkPipelineLayout m_rgb2yuvPipelineLayout{VK_NULL_HANDLE};
    VkDescriptorSetLayout m_setLayout{VK_NULL_HANDLE};
    VkDescriptorPool m_descPool{VK_NULL_HANDLE};
    VkDescriptorSet m_descSet{VK_NULL_HANDLE};

    VkImage m_yuvImage{VK_NULL_HANDLE};
    VmaAllocation m_yuvAlloc{VK_NULL_HANDLE};
    VkImageView m_yuvView{VK_NULL_HANDLE};

    VkImage m_rgbImage{VK_NULL_HANDLE};
    VmaAllocation m_rgbAlloc{VK_NULL_HANDLE};
    VkImageView m_rgbView{VK_NULL_HANDLE};

    VkBuffer m_debugBuffer{VK_NULL_HANDLE};
    VmaAllocation m_debugAlloc{VK_NULL_HANDLE};
    void* m_debugPtr{nullptr};

    VkCommandPool m_cmdPool{VK_NULL_HANDLE};

    VkImage m_rawImage{VK_NULL_HANDLE};
    VmaAllocation m_rawAlloc{VK_NULL_HANDLE};
    VkImageView m_rawView{VK_NULL_HANDLE};
};

#endif // GPU_YUV_CONVERTER_H
