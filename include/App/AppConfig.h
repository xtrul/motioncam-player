// FILE: include/App/AppConfig.h
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <cstddef>

#define VERBOSE_GPU_YUV 1
#define DEBUG_PATTERN_MODE 0
#define ENABLE_CRC_PLANE 1

const int MAX_FRAMES_IN_FLIGHT = 3;

#ifdef NDEBUG
constexpr size_t kNumPersistentStagingBuffers = 8;
constexpr size_t GpuUploadQueueCapacity = 8; // Note: Your ThreadSafeQueue uses maxSize directly, this might be redundant if not used elsewhere
constexpr size_t DecodeQueueCapacityMultiplier = 2; // Max items in decode queue relative to staging buffers
constexpr size_t AvailableStagingIndicesQueueSlack = 4; // How many more items can ASI queue hold than buffers
#else
constexpr size_t kNumPersistentStagingBuffers = 24;
constexpr size_t GpuUploadQueueCapacity = 24; // Note: Your ThreadSafeQueue uses maxSize directly
constexpr size_t DecodeQueueCapacityMultiplier = 2;
constexpr size_t AvailableStagingIndicesQueueSlack = 8;
#endif

// Constants for IO worker pre-loading logic
constexpr size_t MAX_LEAD_FRAMES_IO_WORKER = 8;
constexpr size_t MAX_LAG_FRAMES_IO_WORKER = 4;

#ifndef NDEBUG
const bool enableValidationLayers = true;
#else
const bool enableValidationLayers = false;
#endif

#endif // APP_CONFIG_H