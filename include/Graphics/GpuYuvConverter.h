#ifndef GPU_YUV_CONVERTER_H
#define GPU_YUV_CONVERTER_H

#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>
#include <libavutil/frame.h>
#include <chrono>
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
    bool beginAsync(const uint16_t* raw, const GpuColorParams& params, int& slotIdx);
    bool completeAsync(int slotIdx, AVFrame* frame);

    static constexpr int kAsyncBuffers = 4;
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

    struct Slot {
        VkBuffer stagingBuf{VK_NULL_HANDLE};
        VmaAllocation stagingAlloc{VK_NULL_HANDLE};
        void* stagingMap{nullptr};
        VkBuffer readbackBuf{VK_NULL_HANDLE};
        VmaAllocation readbackAlloc{VK_NULL_HANDLE};
        void* readbackMap{nullptr};
        VkCommandBuffer cmd{VK_NULL_HANDLE};
        VkFence fence{VK_NULL_HANDLE};
        std::chrono::steady_clock::time_point start{};
    };
    std::vector<Slot> m_slots;
    size_t m_nextSlot{0};
    int m_width{0};
    int m_height{0};
    VkDeviceSize m_rawSize{0};
    VkDeviceSize m_outSize{0};
};

#endif // GPU_YUV_CONVERTER_H
