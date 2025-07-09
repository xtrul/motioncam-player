#ifndef COMPUTE_PIPELINE_H
#define COMPUTE_PIPELINE_H

#include <vulkan/vulkan.h>

class Renderer_VK;

namespace ComputePipeline {
    bool createRawToYuvPipeline(Renderer_VK* renderer);
    void cleanup(Renderer_VK* renderer);
    bool dispatchRawToYuv(Renderer_VK* renderer, VkCommandBuffer commandBuffer, int width, int height);
}

#endif // COMPUTE_PIPELINE_H
