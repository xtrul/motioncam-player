#pragma once
#include <string>
#include <memory>
#include <vulkan/vulkan.h>
#include "Utils/vma_usage.h"
#include "Graphics/GpuYuvConverter.h"

class DecoderWrapper;

class ProResGpuExporter {
public:
    ProResGpuExporter(VkDevice device, VmaAllocator allocator, VkQueue queue, VkCommandPool cmdPool);
    ~ProResGpuExporter();
    bool run(DecoderWrapper* decoder, const std::string& outPath);
private:
    GpuYuvConverter m_converter;
};
