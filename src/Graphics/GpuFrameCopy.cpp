#include "Graphics/GpuFrameCopy.h"
#include <spdlog/spdlog.h>

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

    const uint16_t* src = static_cast<const uint16_t*>(mappedGpuMemory);
    const size_t wordsPerRow = static_cast<size_t>(videoWidth) * 2;

#ifdef PRORES_GPU_VALIDATE
    if (validate) {
        uint16_t y0 = src[0];
        uint16_t u  = src[1];
        uint16_t y1 = src[2];
        uint16_t v  = src[3];
        spdlog::debug("[GPU-CHECK] first macropixel: Y0={} U={} Y1={} V={}", y0, u, y1, v);
        bool ok = (y0 <= 1023 && y1 <= 1023 && u <= 1023 && v <= 1023);
        if (!ok) {
            spdlog::error("[GPU-CHECK] validation failed: {} {} {} {}", y0, u, y1, v);
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

        const uint16_t* srcRow = src + static_cast<size_t>(y) * wordsPerRow;
        for (int x = 0; x < videoWidth; x += 2) {
            size_t base = (x >> 1) * 4;
            uint16_t y0 = srcRow[base + 0];
            uint16_t u  = srcRow[base + 1];
            uint16_t y1 = srcRow[base + 2];
            uint16_t v  = srcRow[base + 3];

            yRow[x]     = static_cast<uint16_t>(y0 << 6);
            yRow[x + 1] = static_cast<uint16_t>(y1 << 6);
            uRow[x >> 1] = static_cast<uint16_t>(u << 6);
            vRow[x >> 1] = static_cast<uint16_t>(v << 6);
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
