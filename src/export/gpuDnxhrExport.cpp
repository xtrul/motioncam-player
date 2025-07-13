#include "Export/DnxhrBackend.h"
#include "Export/GpuSliceTransfer.h"
#include "Utils/DebugLog.h"

bool gpuDnxhrExport(const uint16_t* raw, int width, int height) {
    (void)raw; (void)width; (void)height;
    LogToFile("[DNxHR] gpuDnxhrExport stub called");
    return true;
}
