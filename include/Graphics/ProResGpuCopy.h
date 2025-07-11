#ifndef PRORES_GPU_COPY_H
#define PRORES_GPU_COPY_H

#include <vulkan/vulkan.h>
extern "C" {
#include <libavutil/frame.h>
}

bool copyFromGpuToFrame(const void* mappedGpuMemory,
                        const VkSubresourceLayout& layout,
                        int width, int height,
                        AVFrame* frame,
                        bool validate = false);

#endif // PRORES_GPU_COPY_H
