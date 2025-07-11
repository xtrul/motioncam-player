#include "Utils/GpuProresFix.h"
#include "Utils/DebugLog.h"
extern "C" {
#include <libavutil/imgutils.h>
#include <libavutil/intreadwrite.h>
}

static void unpackRow(const uint32_t* src,
                      uint16_t* yDst,
                      uint16_t* uDst,
                      uint16_t* vDst,
                      int mpPerRow)
{
    for (int i = 0; i < mpPerRow; ++i)
    {
        uint32_t lo = src[i * 2 + 0];
        uint32_t hi = src[i * 2 + 1];
        uint16_t y0 = lo & 0x03FF;
        uint16_t u0 = (lo >> 10) & 0x03FF;
        uint16_t y1 = (lo >> 20) | ((hi & 0x0003) << 12);
        uint16_t v0 = (hi >> 2) & 0x03FF;
        yDst[i * 2 + 0] = y0;
        yDst[i * 2 + 1] = y1;
        uDst[i] = u0;
        vDst[i] = v0;
    }
}

bool copyFromGpuToFrame(const void* mappedGpuMemory,
                        int w, int h,
                        size_t rowPitch,
                        AVFrame* f,
                        bool validate)
{
    const uint8_t* base = static_cast<const uint8_t*>(mappedGpuMemory);
    f->format = AV_PIX_FMT_YUV422P10LE;
    f->width = w;
    f->height = h;
    spdlog::debug("[GPU-COPY] start w={} h={} rowPitch={}", w, h, rowPitch);
    if (av_frame_get_buffer(f, 32) < 0)
    {
        spdlog::error("[GPU-COPY] av_frame_get_buffer failed");
        return false;
    }
    spdlog::debug("[GPU-COPY] linesizes Y={} U={} V={}", f->linesize[0], f->linesize[1], f->linesize[2]);

    uint16_t yMin = 65535, yMax = 0;
    uint16_t uMin = 65535, uMax = 0;
    uint16_t vMin = 65535, vMax = 0;

    for (int y = 0; y < h; ++y)
    {
        const uint32_t* src = reinterpret_cast<const uint32_t*>(base + static_cast<size_t>(y) * rowPitch);
        uint16_t* yDst = reinterpret_cast<uint16_t*>(f->data[0] + y * f->linesize[0]);
        uint16_t* uDst = reinterpret_cast<uint16_t*>(f->data[1] + y * f->linesize[1]);
        uint16_t* vDst = reinterpret_cast<uint16_t*>(f->data[2] + y * f->linesize[2]);
        unpackRow(src, yDst, uDst, vDst, w / 2);

        for (int i = 0; i < w; ++i)
        {
            uint16_t val = yDst[i];
            yMin = std::min(yMin, val);
            yMax = std::max(yMax, val);
        }
        for (int i = 0; i < w / 2; ++i)
        {
            uint16_t uv = uDst[i];
            uint16_t vv = vDst[i];
            uMin = std::min(uMin, uv); uMax = std::max(uMax, uv);
            vMin = std::min(vMin, vv); vMax = std::max(vMax, vv);
        }

        if (validate && y == 0)
        {
            spdlog::debug("[GPU-CHECK] rowPitch={} firstMacropixel Y0={} U={} Y1={} V={}",
                          rowPitch, yDst[0], uDst[0], yDst[1], vDst[0]);
            if (!(64 <= yDst[0] && yDst[0] <= 940 &&
                  64 <= yDst[1] && yDst[1] <= 940 &&
                  400 <= uDst[0] && uDst[0] <= 624 &&
                  400 <= vDst[0] && vDst[0] <= 624))
            {
                spdlog::error("[GPU-CHECK] Unexpected macropixel layout");
                return false;
            }
        }
    }

    if (validate)
    {
        spdlog::debug("[GPU-CHECK] min/max: Y {}-{} U {}-{} V {}-{}", yMin, yMax, uMin, uMax, vMin, vMax);
    }

    spdlog::debug("[GPU-COPY] Unpacked {}x{} frame", w, h);
    return true;
}
