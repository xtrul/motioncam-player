#ifndef GPU_YUV_CONVERTER_H
#define GPU_YUV_CONVERTER_H

#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>
#include <cstddef>
#include "Utils/vma_usage.h"
#include "Graphics/GpuColorParams.h"

class Renderer_VK;
struct AVFrame;

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
};

// Lightweight dispatch used for preview generation
void convertRawToRgbPreview(Renderer_VK* renderer, VkCommandBuffer cmd,
                            VkImage rawImage, VkImage outImage,
                            int width, int height,
                            int cfaType,
                            float wbR, float wbG, float wbB,
                            const float colorMatrix[9],
                            uint32_t blackLevel, uint32_t whiteLevel);

#endif // GPU_YUV_CONVERTER_H
