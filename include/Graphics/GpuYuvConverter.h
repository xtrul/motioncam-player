#ifndef GPU_YUV_CONVERTER_H
#define GPU_YUV_CONVERTER_H

#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>
#include <libavutil/frame.h>
#include "Utils/vma_usage.h"

class Renderer_VK;

class GpuYuvConverter {
public:
    explicit GpuYuvConverter(Renderer_VK* renderer);
    ~GpuYuvConverter();

    bool init(int width, int height);
    void cleanup();

    bool convertToFrame(const uint16_t* raw, int width, int height, AVFrame* frame,
                        const float wbGains[3], const float colourMatrix[9]);
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
