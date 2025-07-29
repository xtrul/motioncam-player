#pragma once
#include <vector>
#include <cstdint>

struct QuadBayerDemosaicParams {
    int width{0};
    int height{0};
    int cfaType{0}; // 0 BGGR,1 RGGB,2 GBRG,3 GRBG
};

// Simple edge-aware demosaicing producing linear RGB floats
void quadBayerDemosaic(const uint16_t* raw, const QuadBayerDemosaicParams& p,
                       std::vector<float>& outRGB);
