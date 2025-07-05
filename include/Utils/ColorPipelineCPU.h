#pragma once
#include <vector>
#include <cstdint>
#include <array>

enum class GammaCurve { SRGB, CineonLog, SLog3 };
enum class ColorSpace { Rec709, BT2020, Cinema };

struct CPUColorParams {
    int width{0};
    int height{0};
    int cfaType{0}; // 0 BGGR,1 RGGB,2 GBRG,3 GRBG
    double blackLevel{0.0};
    double whiteLevel{65535.0};
    float gainR{1.0f};
    float gainG{1.0f};
    float gainB{1.0f};
    std::array<float,9> ccm{1,0,0,0,1,0,0,0,1};
    float saturation{1.0f};
    GammaCurve gamma{GammaCurve::SRGB};
    ColorSpace color{ColorSpace::Rec709};
};

void convertRawToRGB24(const uint16_t* raw, const CPUColorParams& params,
                       std::vector<uint8_t>& outRGB, unsigned threads = 1);
