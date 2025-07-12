#ifndef GPU_YUV_CONVERTER_H
#define GPU_YUV_CONVERTER_H

#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>
#include <libavutil/frame.h>
#include "Utils/vma_usage.h"

class Renderer_VK;

struct GpuColorParams {
    int black{0};
    int white{1023};
    float asShotNeutral[3]{1.0f,1.0f,1.0f};
    float colorMatrix[9]{1,0,0,0,1,0,0,0,1};
    int cfaType{0}; // 0 BGGR,1 RGGB,2 GBRG,3 GRBG
};

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

    VkImage m_planeImages[3]{VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE};
    VmaAllocation m_planeAllocs[3]{VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkImageView m_planeViews[3]{VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE};

    VkCommandPool m_cmdPool{VK_NULL_HANDLE};

    VkImage m_rawImage{VK_NULL_HANDLE};
    VmaAllocation m_rawAlloc{VK_NULL_HANDLE};
    VkImageView m_rawView{VK_NULL_HANDLE};
    VkSampler m_rawSampler{VK_NULL_HANDLE};
};

#endif // GPU_YUV_CONVERTER_H
