#pragma once
#include <array>
struct ConvertInfo {
    int width{0};
    int height{0};
    int rowPitch{0};
    int unused{0};
};
static_assert(sizeof(ConvertInfo) == 16, "ConvertInfo must be 16 bytes");
struct ColorPC {
    std::array<float,3> wb{1.f,1.f,1.f};
    std::array<float,9> colorMat{1,0,0,0,1,0,0,0,1};
};
static_assert(sizeof(ColorPC) == 48, "ColorPC must be 48 bytes");
