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
    spdlog::debug("[GPU-COPY] copyFromGpuToFrame w={} h={}",
                  videoWidth, videoHeight);

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

    const uint8_t* base = static_cast<const uint8_t*>(mappedGpuMemory);
    const size_t srcPitch = static_cast<size_t>(videoWidth) * 4;

    if (av_frame_make_writable(frame) < 0) {
        spdlog::error("[GPU-COPY] av_frame_make_writable failed");
        return false;
    }
    av_image_fill_linesizes(frame->linesize, AV_PIX_FMT_YUV422P10LE, videoWidth);

    const int macropixelsPerRow = videoWidth / 2;

    for (int r = 0; r < videoHeight; ++r) {
        const uint8_t* srcRow = base + static_cast<size_t>(r) * srcPitch;
        auto* dstY = reinterpret_cast<uint16_t*>(frame->data[0] + r * frame->linesize[0]);
        auto* dstU = reinterpret_cast<uint16_t*>(frame->data[1] + r * frame->linesize[1]);
        auto* dstV = reinterpret_cast<uint16_t*>(frame->data[2] + r * frame->linesize[2]);

#ifdef PRORES_GPU_VALIDATE
        if (validate && r == 0) {
            uint32_t mp = *reinterpret_cast<const uint32_t*>(srcRow);
            spdlog::debug("[GPU-CHECK] Validate mp0: 0x{:08X}", mp);
            if ((mp & 0x3FFu) != 2 || ((mp >> 10) & 0x3FFu) != 512)
                return false;
        }
#endif
        if (r == 0) {
            uint32_t lo = *reinterpret_cast<const uint32_t*>(srcRow);
            uint32_t hi = *reinterpret_cast<const uint32_t*>(srcRow + 4);
            uint16_t y0 =  (lo        & 0x3FFu);
            uint16_t u  = ((lo >> 10) & 0x3FFu);
            uint16_t y1 = ((lo >> 20) & 0x3FFu);
            uint16_t v  = ((hi & 0xFFu) << 2) | ((lo >> 30) & 0x3u);
            spdlog::debug("[GPU-COPY] first macropixel Y0={} U={} Y1={} V={}",
                          y0, u, y1, v);
        }

        for (int i = 0; i < macropixelsPerRow; ++i) {
            const uint8_t* mp = srcRow + i * 8;
            uint32_t lo = *reinterpret_cast<const uint32_t*>(mp);
            uint32_t hi = *reinterpret_cast<const uint32_t*>(mp + 4);

            uint16_t y0 =  (lo        & 0x3FFu);
            uint16_t u  = ((lo >> 10) & 0x3FFu);
            uint16_t y1 = ((lo >> 20) & 0x3FFu);
            uint16_t v  = ((hi & 0xFFu) << 2) | ((lo >> 30) & 0x3u);

            put_p10(&dstY[2 * i    ], y0);
            put_p10(&dstY[2 * i + 1], y1);
            put_p10(&dstU[i], u);
            put_p10(&dstV[i], v);
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
