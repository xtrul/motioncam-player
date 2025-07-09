#ifndef COMPUTE_PIPELINE_H
#define COMPUTE_PIPELINE_H

#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>

class Renderer_VK;

namespace ComputePipeline {
    bool createRawToYuvPipeline(Renderer_VK* renderer);
    void cleanup(Renderer_VK* renderer);
    bool dispatchRawToYuv(Renderer_VK* renderer, VkCommandBuffer commandBuffer, int width, int height);
    bool runRawToYuvAndReadback(Renderer_VK* renderer, const uint16_t* raw,
                                int width, int height,
                                std::vector<uint16_t>& outPacked);
}

#endif // COMPUTE_PIPELINE_H
