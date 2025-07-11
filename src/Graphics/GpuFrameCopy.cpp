#include "Graphics/GpuFrameCopy.h"
#include <spdlog/spdlog.h>
#include <bit>

namespace {
static inline void put_p10(uint16_t* dst, uint16_t v10) {
    *dst = static_cast<uint16_t>(v10 << 6);
}

inline void unpackRow(const uint32_t* src,
                      uint16_t* Y,
                      uint16_t* U,
                      uint16_t* V,
                      int mpx)
{
    for (int i = 0; i < mpx; ++i) {
        uint32_t w = src[i];
#if __cpp_lib_bitops >= 201907L
        uint32_t rotated = std::rotr(w, 30);
#else
        uint32_t rotated = (w >> 30) | (w << 2);
#endif
        Y[2 * i]     = static_cast<uint16_t>((w & 0x3FFu) << 6);
        U[i]         = static_cast<uint16_t>(((w >> 10) & 0x3FFu) << 6);
        Y[2 * i + 1] = static_cast<uint16_t>(((w >> 20) & 0x3FFu) << 6);
        V[i]         = static_cast<uint16_t>((rotated & 0x3FFu) << 6);
    }
}
}

bool copyFromGpuToFrame(const void* mappedGpuMemory,
                        int videoWidth,
                        int videoHeight,
                        size_t rowPitchBytes,
                        AVFrame* frame,
                        bool validate)
{
    spdlog::debug("[GPU-COPY] copyFromGpuToFrame w={} h={} rowPitch={}",
                  videoWidth, videoHeight, rowPitchBytes);
    spdlog::debug("[GPU-COPY] rowPitch={} expected={}",
                  rowPitchBytes, videoWidth * 4);

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
    const size_t srcPitch = rowPitchBytes;

    if (av_frame_make_writable(frame) < 0) {
        spdlog::error("[GPU-COPY] av_frame_make_writable failed");
        return false;
    }
    av_image_fill_linesizes(frame->linesize, AV_PIX_FMT_YUV422P10LE, videoWidth);

    const int macropixelsPerRow = videoWidth / 2;

    for (int r = 0; r < videoHeight; ++r) {
        const uint32_t* src = reinterpret_cast<const uint32_t*>(base + static_cast<size_t>(r) * srcPitch);
        auto* dstY = reinterpret_cast<uint16_t*>(frame->data[0] + r * frame->linesize[0]);
        auto* dstU = reinterpret_cast<uint16_t*>(frame->data[1] + r * frame->linesize[1]);
        auto* dstV = reinterpret_cast<uint16_t*>(frame->data[2] + r * frame->linesize[2]);

#ifdef PRORES_GPU_VALIDATE
        if (validate && r == 0) {
            uint32_t mp = src[0];
            spdlog::debug("[GPU-CHECK] Validate mp0: 0x{:08X}", mp);
            if ((mp & 0x3FFu) != 2 || ((mp >> 10) & 0x3FFu) != 512)
                return false;
        }
#endif
        if (r == 0) {
#if __cpp_lib_bitops >= 201907L
            uint32_t rot = std::rotr(src[0], 30);
#else
            uint32_t rot = (src[0] >> 30) | (src[0] << 2);
#endif
            spdlog::debug("[GPU-COPY] first macropixel Y0={} U={} Y1={} V={}",
                          src[0] & 0x3FFu,
                          (src[0] >> 10) & 0x3FFu,
                          (src[0] >> 20) & 0x3FFu,
                          rot & 0x3FFu);
        }

        unpackRow(src, dstY, dstU, dstV, macropixelsPerRow);
    }

    const uint16_t* dbgY = reinterpret_cast<const uint16_t*>(frame->data[0]);
    const uint16_t* dbgU = reinterpret_cast<const uint16_t*>(frame->data[1]);
    const uint16_t* dbgV = reinterpret_cast<const uint16_t*>(frame->data[2]);
    spdlog::debug("[GPU-COPY] first AVFrame macropixel: Y0={} Y1={} U={} V={}",
                  dbgY[0] >> 6, dbgY[1] >> 6, dbgU[0] >> 6, dbgV[0] >> 6);

    spdlog::debug("[GPU-COPY] copy complete");
    return true;
}
