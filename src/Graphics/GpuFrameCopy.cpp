#include "Graphics/GpuFrameCopy.h"
#include <spdlog/spdlog.h>

namespace {
static inline void put_p10(uint16_t* dst, uint16_t v10) {
    *dst = static_cast<uint16_t>(v10 << 6);
}
}

bool copyFromGpuToFrame(const void* mappedGpuMemory,
                        int videoWidth,
                        int videoHeight,
                        AVFrame* frame,
                        bool validate)
{
    if (!mappedGpuMemory || !frame) {
        return false;
    }

    if (videoWidth % 2 != 0) {
        spdlog::error("[GPU-COPY] video width must be even: {}", videoWidth);
        return false;
    }

    if (frame->format != AV_PIX_FMT_YUV422P10LE) {
        spdlog::error("[GPU-COPY] unexpected AVFrame format {}", frame->format);
        return false;
    }

    const uint32_t* gpuMacropixels =
        static_cast<const uint32_t*>(mappedGpuMemory);
    const size_t macropixelsPerRow = static_cast<size_t>(videoWidth) / 2;

#ifdef PRORES_GPU_VALIDATE
    if (validate) {
        uint32_t m = gpuMacropixels[0];
        uint16_t y0 =  m        & 0x3FFu;
        uint16_t cb = (m >> 10) & 0x3FFu;
        uint16_t y1 = (m >> 20) & 0x3FFu;
        uint16_t cr = (m >> 30) & 0x3FFu;
        spdlog::debug("[GPU-CHECK] first macropixel: Y0={} U={} Y1={} V={}", y0, cb, y1, cr);
        bool ok = (y0 > 480 && y0 < 544 && y1 > 480 && y1 < 544 &&
                   cb > 480 && cb < 544 && cr > 480 && cr < 544);
        if (!ok) {
            spdlog::error("[GPU-CHECK] validation failed: {} {} {} {}", y0, cb, y1, cr);
            return false;
        }
    }
#else
    (void)validate;
#endif

    spdlog::debug("[GPU-COPY] begin: {}x{}", videoWidth, videoHeight);

    for (int y = 0; y < videoHeight; ++y) {
        auto* yRow = reinterpret_cast<uint16_t*>(frame->data[0] + y * frame->linesize[0]);
        auto* uRow = reinterpret_cast<uint16_t*>(frame->data[1] + y * frame->linesize[1]);
        auto* vRow = reinterpret_cast<uint16_t*>(frame->data[2] + y * frame->linesize[2]);

        const uint32_t* srcRow = gpuMacropixels + static_cast<size_t>(y) * macropixelsPerRow;
        for (int x = 0; x < videoWidth; x += 2) {
            uint32_t m = srcRow[x / 2];
            uint16_t y0 =  m        & 0x3FFu;
            uint16_t cb = (m >> 10) & 0x3FFu;
            uint16_t y1 = (m >> 20) & 0x3FFu;
            uint16_t cr = (m >> 30) & 0x3FFu;

            put_p10(&yRow[x    ], y0);
            put_p10(&yRow[x + 1], y1);
            put_p10(&uRow[x / 2], cb);
            put_p10(&vRow[x / 2], cr);
        }
    }

    const uint16_t* dbgY = reinterpret_cast<const uint16_t*>(frame->data[0]);
    const uint16_t* dbgU = reinterpret_cast<const uint16_t*>(frame->data[1]);
    const uint16_t* dbgV = reinterpret_cast<const uint16_t*>(frame->data[2]);
    spdlog::debug("[GPU-COPY] first AVFrame macropixel: Y0={} Y1={} U={} V={}",
                  dbgY[0] >> 6, dbgY[1] >> 6, dbgU[0] >> 6, dbgV[0] >> 6);

    spdlog::debug("[GPU-COPY] copy complete");
    return true;
}
