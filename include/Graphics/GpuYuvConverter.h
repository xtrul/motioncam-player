#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#include <nlohmann/json.hpp>

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
private:
    Renderer_VK* m_renderer{nullptr};
    int m_width{0};
    int m_height{0};
    int m_ringSize{0};
    int m_index{0};

    struct ImageItem {
        VkImage image{VK_NULL_HANDLE};
        VmaAllocation alloc{VK_NULL_HANDLE};
        VkImageView view{VK_NULL_HANDLE};
        VkDescriptorSet set{VK_NULL_HANDLE};
    };
    std::vector<ImageItem> m_images;

    VkDescriptorSetLayout m_descLayout{VK_NULL_HANDLE};
    VkPipelineLayout m_pipelineLayout{VK_NULL_HANDLE};
    VkPipeline m_pipeline{VK_NULL_HANDLE};
    VkDescriptorPool m_descPool{VK_NULL_HANDLE};
};
