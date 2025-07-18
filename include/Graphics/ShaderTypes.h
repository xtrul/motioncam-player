#ifndef SHADER_TYPES_H
#define SHADER_TYPES_H

#include <glm/glm.hpp>

struct ShaderParamsUBO {
    alignas(4) int W;
    alignas(4) int H;
    alignas(4) int cfaType;
    alignas(4) float exposure;
    alignas(4) float blackLevel;
    alignas(4) float whiteLevel;
    alignas(4) float invBlackWhiteRange;
    alignas(4) float gainR;
    alignas(4) float gainG;
    alignas(4) float gainB;
    alignas(16) glm::mat4 CCM;
    alignas(4) float saturationAdjustment;
    alignas(4) int orientationDegrees;
};

#endif // SHADER_TYPES_H
