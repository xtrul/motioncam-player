#pragma once
#include <array>

struct GpuColorParams {
    int black{0};
    int white{65535};
    std::array<float,3> asShotNeutral{1.0f,1.0f,1.0f};
    std::array<float,9> colorMatrix{1,0,0,0,1,0,0,0,1};
    int cfaType{0}; // 0 BGGR,1 RGGB,2 GBRG,3 GRBG
};
