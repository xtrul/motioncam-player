#ifndef GPU_FRAME_COPY_H
#define GPU_FRAME_COPY_H

#include <cstdint>
#include <spdlog/spdlog.h>
extern "C" {
#include <libavutil/frame.h>
}

namespace GpuFrameCopy {

#ifdef PRORES_GPU_VALIDATE
inline bool validateMacroPixel(const uint16_t *p) {
    return (p[0] <= 1023 && p[1] <= 1023 && p[2] <= 1023 && p[3] <= 1023);
}
#else
inline bool validateMacroPixel(const uint16_t *p) { (void)p; return true; }
#endif

inline bool copyFromGpuToFrame(const void *mappedGpuMemory,
                               int videoWidth, int videoHeight,
                               AVFrame *frame,
                               bool validate=false)
{
    const uint16_t *gpuBuf = static_cast<const uint16_t *>(mappedGpuMemory);

    frame->format = AV_PIX_FMT_YUV422P10LE;
    frame->width = videoWidth;
    frame->height = videoHeight;
    if (av_frame_get_buffer(frame, 32) < 0) {
        spdlog::error("[GPU-COPY] av_frame_get_buffer() failed");
        return false;
    }

    const int w = videoWidth;
    const int h = videoHeight;

    for (int y=0; y<h; ++y) {
        auto *yRow = reinterpret_cast<uint16_t*>(frame->data[0] + y * frame->linesize[0]);
        auto *uRow = reinterpret_cast<uint16_t*>(frame->data[1] + y * frame->linesize[1]);
        auto *vRow = reinterpret_cast<uint16_t*>(frame->data[2] + y * frame->linesize[2]);

        size_t srcBase = static_cast<size_t>(y) * (w/2) * 4;
        for (int x=0; x<w; x+=2) {
            size_t src = srcBase + (x>>1)*4;
            uint16_t y0 = gpuBuf[src];
            uint16_t u  = gpuBuf[src+1];
            uint16_t y1 = gpuBuf[src+2];
            uint16_t v  = gpuBuf[src+3];
            if (validate && y==0 && x==0) {
                if (!validateMacroPixel(&gpuBuf[src])) {
                    spdlog::error("[GPU-CHECK] Unexpected macropixel at (0,0): Y0={} U={} Y1={} V={}", y0,u,y1,v);
                    return false;
                } else {
                    spdlog::debug("[GPU-CHECK] Macropixel layout validated (Y0={} U={} Y1={} V={})", y0,u,y1,v);
                }
            }
            yRow[x]   = y0 << 6;
            yRow[x+1] = y1 << 6;
            uRow[x>>1] = u << 6;
            vRow[x>>1] = v << 6;
        }
    }
    spdlog::debug("[GPU-COPY] Unpacked {}x{} YUV422P10 frame from GPU buffer", w, h);
    return true;
}

} // namespace GpuFrameCopy

#endif // GPU_FRAME_COPY_H
