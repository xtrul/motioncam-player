#include "Export/ProResExporterFactory.h"
#include "Export/ProResCpuExporter.h"
#include "Export/ProResGpuExporter.h"

std::unique_ptr<IProResExporter> createProResExporter(ProResMode mode) {
    if (mode == ProResMode::GPU)
        return std::make_unique<ProResGpuExporter>();
    return std::make_unique<ProResCpuExporter>();
}
