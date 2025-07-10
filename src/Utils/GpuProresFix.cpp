#include "Utils/GpuProresFix.h"
#include <spdlog/spdlog.h>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/intreadwrite.h>
}
#include <vector>

static inline bool validateValues(uint16_t y0, uint16_t u, uint16_t y1, uint16_t v)
{
#ifdef PRORES_GPU_VALIDATE
    return (y0 <= 1023 && u <= 1023 && y1 <= 1023 && v <= 1023);
#else
    (void)y0; (void)u; (void)y1; (void)v; return true;
#endif
}

bool copyFromGpuToFrame(const void *mappedGpuMemory,
                        int w, int h,
                        size_t rowPitch,
                        AVFrame *f, bool validate)
{
    const uint8_t *gpu = static_cast<const uint8_t *>(mappedGpuMemory);
    f->format = AV_PIX_FMT_YUV422P10LE;
    f->width = w;
    f->height = h;
    if (av_frame_get_buffer(f, 32) < 0) {
        spdlog::error("[GPU-COPY] av_frame_get_buffer failed");
        return false;
    }

    for (int y = 0; y < h; ++y) {
        const uint8_t* rowSrc = gpu + static_cast<size_t>(y) * rowPitch;
        uint16_t* Y = reinterpret_cast<uint16_t*>(f->data[0] + y * f->linesize[0]);
        uint16_t* U = reinterpret_cast<uint16_t*>(f->data[1] + y * f->linesize[1]);
        uint16_t* V = reinterpret_cast<uint16_t*>(f->data[2] + y * f->linesize[2]);

        const uint8_t* src = rowSrc;
        for (int x = 0; x < w; x += 2) {
            uint32_t lo = AV_RL32(src);
            uint32_t hi = AV_RL32(src + 4);
            uint16_t y0 =  (lo       ) & 0x3FF;
            uint16_t u  = (lo >> 10) & 0x3FF;
            uint16_t y1 = (lo >> 20) & 0x3FF;
            uint16_t v  = ((hi & 0xFF) << 2) | ((lo >> 30) & 0x3);
            if (validate && !y && !x) {
                if (!validateValues(y0, u, y1, v)) {
                    spdlog::error("[GPU-CHECK] Bad macropixel layout: Y0=%u U=%u Y1=%u V=%u", y0, u, y1, v);
                    return false;
                }
                spdlog::debug("[GPU-CHECK] rowPitch=%zu firstMacropixel Y0=%u U=%u Y1=%u V=%u", rowPitch, y0, u, y1, v);
            }
            AV_WL16(&Y[x], y0);
            AV_WL16(&Y[x + 1], y1);
            AV_WL16(&U[x >> 1], u);
            AV_WL16(&V[x >> 1], v);
            src += 5;
        }
    }
    spdlog::debug("[GPU-COPY] Unpacked %dx%d frame", w, h);
    return true;
}
