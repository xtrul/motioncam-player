#include "Export/ProResGpuExporter.h"
#include "Decoder/DecoderWrapper.h"
#include "Utils/DebugLog.h"

ProResGpuExporter::ProResGpuExporter(VkDevice device, VmaAllocator allocator, VkQueue queue, VkCommandPool cmdPool)
    : m_converter(device, allocator, queue, cmdPool) {}

ProResGpuExporter::~ProResGpuExporter() {}

bool ProResGpuExporter::run(DecoderWrapper* decoder, const std::string& outPath) {
    LogProRes("[ProResGpuExporter] GPU export requested but not implemented. Falling back to CPU path.");
    return false; // Indicate failure so caller can fallback
}
