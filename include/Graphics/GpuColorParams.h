#ifndef GPU_COLOR_PARAMS_H
#define GPU_COLOR_PARAMS_H

struct GpuColorParams {
    int black{0};
    int white{1023};
    float asShotNeutral[3]{1.0f,1.0f,1.0f};
    float colorMatrix[9]{1,0,0,0,1,0,0,0,1};
    int cfaType{0}; // 0 BGGR,1 RGGB,2 GBRG,3 GRBG
};

#endif // GPU_COLOR_PARAMS_H
