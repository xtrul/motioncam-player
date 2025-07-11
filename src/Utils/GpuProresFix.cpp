#include "Utils/GpuProresFix.h"
#include <spdlog/spdlog.h>
#include "Utils/DebugLog.h"
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/intreadwrite.h>
}
#include <vector>
#include <algorithm>
#include <sstream>

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

    uint16_t yMinS = 65535, yMaxS = 0, uMinS = 65535, uMaxS = 0, vMinS = 65535, vMaxS = 0;
    uint16_t firstY0 = 0, firstY1 = 0, firstU = 0, firstV = 0;
    bool firstSet = false;

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
            if (!firstSet) {
                firstSet = true;
                firstY0 = y0;
                firstU  = u;
                firstY1 = y1;
                firstV  = v;
                if (validate) {
                    std::ostringstream oss;
                    oss << "[GPU-CHECK] rowPitch=" << rowPitch
                        << " firstMacropixel Y0=" << y0
                        << " U=" << u << " Y1=" << y1 << " V=" << v;
                    LogProRes(oss.str());
                    if (y0 != 2 || u != 512 || y1 != 4 || v != 512) {
                        LogProRes("[GPU-CHECK] Unexpected macropixel layout");
                        return false;
                    }
                }
            }
            yMinS = std::min(yMinS, std::min(y0, y1));
            yMaxS = std::max(yMaxS, std::max(y0, y1));
            uMinS = std::min(uMinS, u);
            uMaxS = std::max(uMaxS, u);
            vMinS = std::min(vMinS, v);
            vMaxS = std::max(vMaxS, v);

            if (validate && !y && !x) {
                if (!validateValues(y0, u, y1, v)) {
                    std::ostringstream err;
                    err << "[GPU-CHECK] Bad macropixel layout: Y0=" << y0
                        << " U=" << u << " Y1=" << y1 << " V=" << v;
                    LogProRes(err.str());
                    return false;
                }
                // logged earlier
            }
            AV_WL16(&Y[x],     y0 << 6);
            AV_WL16(&Y[x + 1], y1 << 6);
            AV_WL16(&U[x >> 1], u << 6);
            AV_WL16(&V[x >> 1], v << 6);
            src += 5;
        }
    }

    if (validate) {
        {
            std::ostringstream oss;
            oss << "[GPU-CHECK] src min/max: Y " << yMinS << '/' << yMaxS
                << " U " << uMinS << '/' << uMaxS
                << " V " << vMinS << '/' << vMaxS;
            LogProRes(oss.str());
        }
    }

    uint16_t yMinD = 65535, yMaxD = 0, uMinD = 65535, uMaxD = 0, vMinD = 65535, vMaxD = 0;
    for (int y = 0; y < h; ++y) {
        const uint16_t* Y = reinterpret_cast<const uint16_t*>(f->data[0] + y * f->linesize[0]);
        const uint16_t* U = reinterpret_cast<const uint16_t*>(f->data[1] + y * f->linesize[1]);
        const uint16_t* V = reinterpret_cast<const uint16_t*>(f->data[2] + y * f->linesize[2]);
        for (int x = 0; x < w; ++x) {
            uint16_t yv = Y[x];
            yMinD = std::min(yMinD, yv); yMaxD = std::max(yMaxD, yv);
        }
        for (int x = 0; x < w/2; ++x) {
            uint16_t uv = U[x]; uMinD = std::min(uMinD, uv); uMaxD = std::max(uMaxD, uv);
            uint16_t vv = V[x]; vMinD = std::min(vMinD, vv); vMaxD = std::max(vMaxD, vv);
        }
    }
    if (validate) {
        std::ostringstream oss;
        oss << "[GPU-CHECK] dst min/max: Y " << yMinD << '/' << yMaxD
            << " U " << uMinD << '/' << uMaxD
            << " V " << vMinD << '/' << vMaxD
            << " firstPix: " << firstY0 << ',' << firstY1
            << ',' << firstU << ',' << firstV;
        LogProRes(oss.str());
        if (yMinD < 64 || yMaxD > 1023 || uMaxD > 1023 || vMaxD > 1023) {
            LogProRes("[GPU-CHECK] Out-of-range plane after copy");
            return false;
        }
    }
    LogProRes("[GPU-COPY] Unpacked frame OK");
    return true;
}
