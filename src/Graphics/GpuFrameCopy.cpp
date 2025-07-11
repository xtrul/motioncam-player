#include "Graphics/GpuFrameCopy.h"
#include <spdlog/spdlog.h>

namespace {
// Write a 10-bit value into a 16-bit little-endian container
inline void put_p10(uint16_t* dst, uint16_t v10)
{
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

    const size_t rowPitchBytes = static_cast<size_t>(videoWidth) * 4;
    const uint8_t* base = static_cast<const uint8_t*>(mappedGpuMemory);

    if (av_frame_make_writable(frame) < 0) {
        spdlog::error("[GPU-COPY] av_frame_make_writable failed");
        return false;
    }
    av_image_fill_linesizes(frame->linesize, AV_PIX_FMT_YUV422P10LE, videoWidth);

    const int groupsPerRow = videoWidth / 2;

    for (int r = 0; r < videoHeight; ++r) {
        const uint8_t* srcRow = base + static_cast<size_t>(r) * rowPitchBytes;
        auto* dstY = reinterpret_cast<uint16_t*>(frame->data[0] + r * frame->linesize[0]);
        auto* dstU = reinterpret_cast<uint16_t*>(frame->data[1] + r * frame->linesize[1]);
        auto* dstV = reinterpret_cast<uint16_t*>(frame->data[2] + r * frame->linesize[2]);

#ifdef PRORES_GPU_VALIDATE
        if (validate && r == 0) {
            const uint32_t w0 = reinterpret_cast<const uint32_t*>(srcRow)[0];
            const uint32_t w1 = reinterpret_cast<const uint32_t*>(srcRow)[1];
            uint16_t y0 = (w0 >> 10) & 0x3FF;
            uint16_t u0 = (w0 >> 0) & 0x3FF;
            uint16_t y1 = (w1 >> 0) & 0x3FF;
            uint16_t v0 = (w1 >> 10) & 0x3FF;
            spdlog::debug("[GPU-CHECK] first group w0=0x{:08X} w1=0x{:08X}", w0, w1);
            if (y0 != 512 || u0 != 512 || y1 != 512 || v0 != 512) {
                spdlog::warn("[GPU-CHECK] macropixel 0 bad: Y0={} U0={} Y1={} V0={}",
                             y0, u0, y1, v0);
                return false;
            }
        }
#endif

        if (r == 0) {
            const uint32_t w0 = reinterpret_cast<const uint32_t*>(srcRow)[0];
            const uint32_t w1 = reinterpret_cast<const uint32_t*>(srcRow)[1];
            spdlog::debug("[GPU-COPY] first macropixel Y0={} U0={} Y1={} V0={}",
                          (w0 >> 10) & 0x3FF,
                          (w0 >> 0) & 0x3FF,
                          (w1 >> 0) & 0x3FF,
                          (w1 >> 10) & 0x3FF);
        }

        const uint32_t* src32 = reinterpret_cast<const uint32_t*>(srcRow);
        for (int g = 0; g < groupsPerRow; ++g) {
            const uint32_t w0 = src32[g * 2 + 0];
            const uint32_t w1 = src32[g * 2 + 1];

            uint16_t y0 = (w0 >> 10) & 0x3FF;
            uint16_t u0 = (w0 >> 0) & 0x3FF;
            uint16_t y1 = (w1 >> 0) & 0x3FF;
            uint16_t v0 = (w1 >> 10) & 0x3FF;

            put_p10(&dstY[g * 2 + 0], y0);
            put_p10(&dstY[g * 2 + 1], y1);
            put_p10(&dstU[g], u0);
            put_p10(&dstV[g], v0);
        }
    }

    const uint16_t* dbgY = reinterpret_cast<const uint16_t*>(frame->data[0]);
    const uint16_t* dbgU = reinterpret_cast<const uint16_t*>(frame->data[1]);
    const uint16_t* dbgV = reinterpret_cast<const uint16_t*>(frame->data[2]);
    spdlog::debug("[GPU-COPY] first AVFrame macropixel: Y0={} Y1={} U={} V={}",
                  dbgY[0], dbgY[1], dbgU[0], dbgV[0]);

    spdlog::debug("[GPU-COPY] copy complete");
    return true;
}
