#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <nlohmann/json.hpp>
#include "vma_usage.h"

#ifndef VK_FORMAT_G10X6_B10X6_R10X6_2PACK16
#define VK_FORMAT_G10X6_B10X6_R10X6_2PACK16 VK_FORMAT_G10X6B10X6G10X6R10X6_422_UNORM_4PACK16
#endif

class Renderer_VK;

class GpuYuvConverter {
public:
    GpuYuvConverter();
    ~GpuYuvConverter();

    bool init(Renderer_VK* renderer, int width, int height, int ringSize);
    void cleanup();

    // Dispatch conversion for the raw image currently uploaded in Renderer_VK
    // Returns the VkImage containing the converted YUV result.
    VkImage convert(const nlohmann::json& meta,
                    double blackLevel, double whiteLevel,
                    int cfaOverride);
    bool readback(VkImage image, std::vector<uint16_t>& out);
private:
    Renderer_VK* m_renderer{nullptr};
    int m_width{0};
    int m_height{0};
    int m_ringSize{0};
    int m_index{0};

    struct ImageItem {
        VkImage image{VK_NULL_HANDLE};
        VmaAllocation allocation{VK_NULL_HANDLE};
        VkImageView view{VK_NULL_HANDLE};
        VkDescriptorSet set{VK_NULL_HANDLE};
    };
    std::vector<ImageItem> m_images;

    VkDescriptorSetLayout m_descLayout{VK_NULL_HANDLE};
    VkPipelineLayout m_pipelineLayout{VK_NULL_HANDLE};
    VkPipeline m_pipeline{VK_NULL_HANDLE};
    VkDescriptorPool m_descPool{VK_NULL_HANDLE};
};
