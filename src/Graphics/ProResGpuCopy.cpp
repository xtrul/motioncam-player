#include "Graphics/ProResGpuCopy.h"
#include <spdlog/spdlog.h>
#include <cstdint>
extern "C" {
#include <libavutil/frame.h>
}

static inline bool validateMacroPixel(const uint16_t* p)
{
#ifdef PRORES_GPU_VALIDATE
    return (p[0] <= 1023 && p[1] <= 1023 && p[2] <= 1023 && p[3] <= 1023);
#else
    (void)p;
    return true;
#endif
}

bool copyFromGpuToFrame(const void* mappedGpuMemory,
                        const VkSubresourceLayout& layout,
                        int videoWidth, int videoHeight,
                        AVFrame* frame,
                        bool validate)
{
    if (!mappedGpuMemory || !frame) return false;

    frame->format = AV_PIX_FMT_YUV422P10LE;
    frame->width  = videoWidth;
    frame->height = videoHeight;
    if (av_frame_get_buffer(frame, 32) < 0) {
        spdlog::error("[GPU-COPY] av_frame_get_buffer() failed");
        return false;
    }

    const uint16_t* gpuBuf = static_cast<const uint16_t*>(mappedGpuMemory);
    int w = videoWidth;
    int h = videoHeight;

    for (int y = 0; y < h; ++y) {
        const size_t rowOffset = (layout.rowPitch >> 1) * static_cast<size_t>(y);
        const uint16_t* src = gpuBuf + rowOffset;

        uint16_t* yRow = reinterpret_cast<uint16_t*>(frame->data[0] + y * frame->linesize[0]);
        uint16_t* uRow = reinterpret_cast<uint16_t*>(frame->data[1] + y * frame->linesize[1]);
        uint16_t* vRow = reinterpret_cast<uint16_t*>(frame->data[2] + y * frame->linesize[2]);

        for (int x = 0; x < w; x += 2) {
            const uint16_t y0 = src[(x>>1)*4 + 0];
            const uint16_t u  = src[(x>>1)*4 + 1];
            const uint16_t y1 = src[(x>>1)*4 + 2];
            const uint16_t v  = src[(x>>1)*4 + 3];

            if (validate && y == 0 && x == 0) {
                if (!validateMacroPixel(src)) {
                    spdlog::error("[GPU-CHECK] Unexpected macropixel at (0,0): Y0={} U={} Y1={} V={}", y0, u, y1, v);
                    return false;
                } else {
                    spdlog::debug("[GPU-CHECK] Macropixel layout validated (Y0={} U={} Y1={} V={})", y0, u, y1, v);
                }
            }

            yRow[x]     = y0 << 6;
            yRow[x + 1] = y1 << 6;
            uRow[x>>1]  = u << 6;
            vRow[x>>1]  = v << 6;
        }
    }

    spdlog::debug("[GPU-COPY] Unpacked {}x{} YUV422P10 frame from GPU buffer", w, h);
    return true;
}

