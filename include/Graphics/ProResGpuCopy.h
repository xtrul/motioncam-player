#ifndef PRORES_GPU_COPY_H
#define PRORES_GPU_COPY_H

#include <vulkan/vulkan.h>
extern "C" {
#include <libavutil/frame.h>
}

bool CopyGpuToAvFrame(const void* gpuPtr,
                      const VkSubresourceLayout& layout,
                      int width, int height,
                      AVFrame* frame);

#endif // PRORES_GPU_COPY_H
