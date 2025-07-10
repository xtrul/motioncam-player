#pragma once
#include <cstdint>
extern "C" {
#include <libavutil/frame.h>
}

bool copyFromGpuToFrame(const void *mappedGpuMemory,
                        int videoWidth, int videoHeight,
                        size_t gpuRowPitch,
                        AVFrame *frame,
                        bool validate = false);
