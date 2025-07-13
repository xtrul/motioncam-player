#include "Export/DnxhrBackend.h"
#include "Utils/DebugLog.h"

bool DnxhrBackend::initialize(const std::string& path, int width, int height,
                             int fpsNum, int fpsDen) {
    LogToFile("[DNxHR] initialize stub");
    (void)path; (void)width; (void)height; (void)fpsNum; (void)fpsDen;
    return true;
}

bool DnxhrBackend::writeSlice(const SliceBuffer& slice) {
    (void)slice;
    return true;
}

void DnxhrBackend::finalize() {
    LogToFile("[DNxHR] finalize stub");
}
