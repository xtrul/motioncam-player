#ifndef GPU_COLOR_PARAMS_H
#define GPU_COLOR_PARAMS_H

struct GpuColorParams {
    int black{0};
    int white{1023};
    float asShotNeutral[3]{1.0f,1.0f,1.0f};
    float colorMatrix[9]{1,0,0,0,1,0,0,0,1};
    int cfaType{0}; // 0 BGGR,1 RGGB,2 GBRG,3 GRBG
};

struct ConvertInfo {
    int width{0};
    int height{0};
    int rowPitch{0};
    int _pad{0};
};

struct ColorPC {
    float wb[3]{1.0f,1.0f,1.0f};
    float colorMat[9]{1,0,0,0,1,0,0,0,1};
};


#endif // GPU_COLOR_PARAMS_H
