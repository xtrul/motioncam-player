#include "Export/DnxhrExporter.h"
#include "Utils/DebugLog.h"

DnxhrExporter::DnxhrExporter(const std::string& outputPath)
    : m_outputPath(outputPath) {
    LogToFile("[DNxHR] exporter created for " + outputPath);
}

DnxhrExporter::~DnxhrExporter() = default;

bool DnxhrExporter::addFrame(const SliceBuffer& /*slice*/) {
    LogToFile("[DNxHR] addFrame stub called");
    return true;
}

bool DnxhrExporter::finalize() {
    LogToFile("[DNxHR] finalize stub called");
    return true;
}
