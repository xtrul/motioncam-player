#pragma once
#include <memory>
#include "Export/IProResExporter.h"

enum class ProResMode { CPU, GPU };

std::unique_ptr<IProResExporter> createProResExporter(ProResMode mode);
