#include "Utils/GpuProresFix.h"
#include <spdlog/spdlog.h>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/intreadwrite.h>
}
#include <vector>

static inline bool validateMacroPixel(const uint16_t *p)
{
#ifdef PRORES_GPU_VALIDATE
    return (p[0] <= 1023 && p[1] <= 1023 && p[2] <= 1023 && p[3] <= 1023);
#else
    (void)p; return true;
#endif
}

bool copyFromGpuToFrame(const void *mappedGpuMemory,
                        int w, int h,
                        AVFrame *f, bool validate)
{
    const uint16_t *gpu = static_cast<const uint16_t *>(mappedGpuMemory);
    f->format = AV_PIX_FMT_YUV422P10LE;
    f->width = w;
    f->height = h;
    if (av_frame_get_buffer(f, 32) < 0) {
        spdlog::error("[GPU-COPY] av_frame_get_buffer failed");
        return false;
    }

    for (int y = 0; y < h; ++y) {
        uint8_t* Y = f->data[0] + y * f->linesize[0];
        uint8_t* U = f->data[1] + y * f->linesize[1];
        uint8_t* V = f->data[2] + y * f->linesize[2];
        size_t base = static_cast<size_t>(y) * (w / 2) * 4;
        for (int x = 0; x < w; x += 2) {
            size_t src = base + (x >> 1) * 4;
            uint16_t y0 = gpu[src + 0];
            uint16_t u  = gpu[src + 1];
            uint16_t y1 = gpu[src + 2];
            uint16_t v  = gpu[src + 3];
            if (validate && !y && !x) {
                if (!validateMacroPixel(&gpu[src])) {
                    spdlog::error("[GPU-CHECK] Bad macro-pixel layout: Y0=%u U=%u Y1=%u V=%u", y0, u, y1, v);
                    return false;
                } else {
                    spdlog::debug("[GPU-CHECK] Macropixel layout validated (Y0=%u U=%u Y1=%u V=%u)", y0, u, y1, v);
                }
            }
            AV_WL16(Y + (x << 1),     y0 << 6);
            AV_WL16(Y + ((x + 1) << 1), y1 << 6);
            AV_WL16(U + ((x >> 1) << 1), u << 6);
            AV_WL16(V + ((x >> 1) << 1), v << 6);
        }
    }
    spdlog::debug("[GPU-COPY] Unpacked %dx%d frame", w, h);
    return true;
}
