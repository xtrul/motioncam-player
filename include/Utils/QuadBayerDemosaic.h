#pragma once
#include <vector>
#include <cstdint>
#include "Utils/ColorPipelineCPU.h"

// Simple bilinear-based demosaic for quad-bayer sensors.
// Uses a 4x4 repeating pattern and averages neighbouring
// pixels two units apart.

void quadBayerDemosaic(const uint16_t* raw,
                       const CPUColorParams& params,
                       std::vector<uint8_t>& outRGB,
                       unsigned threads = 1);
