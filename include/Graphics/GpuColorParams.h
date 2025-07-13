#ifndef GPU_COLOR_PARAMS_H
#define GPU_COLOR_PARAMS_H

struct GpuColorParams {
    float wbR{1.0f};
    float wbG{1.0f};
    float wbB{1.0f};
    float colorMatrix[9]{1,0,0,0,1,0,0,0,1};
    int   cfaType{0};   // 0 BGGR,1 RGGB,2 GBRG,3 GRBG
    int   fullSwing{0}; // 0 studio, 1 full
    unsigned int black{64};
    unsigned int white{1023};
};

struct PerfStat {
    double gpuMs = 0.0;
    double encMs = 0.0;
    double ioMs  = 0.0;
    uint32_t frames = 0;
};

extern PerfStat gPerf;

#endif // GPU_COLOR_PARAMS_H
