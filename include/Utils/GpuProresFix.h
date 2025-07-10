#pragma once
#include <cstdint>
extern "C" {
#include <libavutil/frame.h>
}

bool copyFromGpuToFrame(const void *mappedGpuMemory,
                        int videoWidth, int videoHeight,
                        AVFrame *frame,
                        bool validate = false);
