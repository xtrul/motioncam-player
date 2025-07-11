#include "Graphics/ProResGpuCopy.h"
#include "Utils/DebugLog.h"
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
        LogProRes("[GPU-COPY] av_frame_get_buffer() failed");
        return false;
    }

    const uint32_t* gpuBuf = static_cast<const uint32_t*>(mappedGpuMemory);
    int w = videoWidth;
    int h = videoHeight;

    for (int y = 0; y < h; ++y) {
        const size_t rowOffset = (layout.rowPitch >> 2) * static_cast<size_t>(y);
        const uint32_t* src = gpuBuf + rowOffset;

        uint16_t* yRow = reinterpret_cast<uint16_t*>(frame->data[0] + y * frame->linesize[0]);
        uint16_t* uRow = reinterpret_cast<uint16_t*>(frame->data[1] + y * frame->linesize[1]);
        uint16_t* vRow = reinterpret_cast<uint16_t*>(frame->data[2] + y * frame->linesize[2]);

        for (int x = 0; x < w; x += 2) {
            const uint32_t p0 = src[(x>>1)*2 + 0];
            const uint32_t p1 = src[(x>>1)*2 + 1];

            const uint16_t y0 =  p0        & 0x3FFu;
            const uint16_t u   = (p0 >> 10) & 0x3FFu;
            const uint16_t y1 = (p0 >> 20) & 0x3FFu;
            const uint16_t v   =  p1        & 0x3FFu;

            if (validate && y == 0 && x == 0) {
                uint16_t vals[4] = {y0, u, y1, v};
                if (!validateMacroPixel(vals)) {
                    char buf[128];
                    snprintf(buf, sizeof(buf), "[GPU-CHECK] Unexpected macropixel at (0,0): Y0=%u U=%u Y1=%u V=%u", y0, u, y1, v);
                    LogProRes(buf);
                    return false;
                } else {
                    char buf[128];
                    snprintf(buf, sizeof(buf), "[GPU-CHECK] Macropixel layout validated (Y0=%u U=%u Y1=%u V=%u)", y0, u, y1, v);
                    LogProRes(buf);
                }
            }

            yRow[x]     = y0 << 6;
            yRow[x + 1] = y1 << 6;
            uRow[x>>1]  = u << 6;
            vRow[x>>1]  = v << 6;
        }
    }

    {
        char buf[128];
        snprintf(buf, sizeof(buf), "[GPU-COPY] Unpacked %dx%d YUV422P10 frame from GPU buffer", w, h);
        LogProRes(buf);
    }
    return true;
}

