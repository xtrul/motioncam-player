#ifndef APP_STATE_H
#define APP_STATE_H

#include <vector>
#include <cstdint>
#include <string>
#include <optional>
#include <vulkan/vulkan.h>      // For VkBuffer
#include "vma_usage.h"          // For VmaAllocation
#include <nlohmann/json.hpp>    // For nlohmann::json
#include <motioncam/Decoder.hpp> // For motioncam::Timestamp

struct StagingBufferInfo {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
};

struct CompressedFramePacket {
    motioncam::Timestamp timestamp;
    std::vector<uint8_t> compressedPayload;
    std::vector<uint8_t> metadataPayload;
    int width = 0;
    int height = 0;
    int compressionType = 0;
    size_t frameIndex = 0;
    size_t fileLoadID = 0; // For stale packet identification
};

struct GpuUploadPacket {
    motioncam::Timestamp timestamp;
    size_t stagingBufferIndex;
    nlohmann::json metadata;
    int width = 0;
    int height = 0;
    size_t frameIndex = 0;
    size_t fileLoadID = 0; // For stale packet identification
};

#endif // APP_STATE_H