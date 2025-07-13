#include "Export/DnxhrExporter.h"
#include "Export/GpuExport.h"
#include "Utils/DebugLog.h"

// Entry point for GPU accelerated DNxHR export. This is currently a stub
// implementation that only logs the invocation.

bool gpuDnxhrExport(const std::string& outputPath) {
    LogToFile("[DNxHR] gpuDnxhrExport invoked for " + outputPath);
    DnxhrExporter exporter(outputPath);
    DnxhrExporter::SliceBuffer slice{};
    // In a real implementation frames would be processed here
    exporter.addFrame(slice);
    exporter.finalize();
    return true;
}
