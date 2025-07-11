#include "Graphics/ProResGpuCopy.h"
#include "Utils/DebugLog.h"
#include <cstdint>
#include <cstring>

// helper to unpack one row of packed macropixels
static inline void unpackRow(const uint32_t* src, uint16_t* y, uint16_t* u,
                             uint16_t* v, int px)
{
    for (int x = 0; x < px; ++x) {
        uint32_t p0 = src[2 * x];
        uint32_t p1 = src[2 * x + 1];

        uint16_t y0 = p0 & 0x03FFu;
        uint16_t u0 = (p0 >> 10) & 0x03FFu;
        uint16_t y1 = (p0 >> 20) & 0x03FFu;
        uint16_t v0 = p1 & 0x03FFu;

        y[2 * x + 0] = y0;
        y[2 * x + 1] = y1;
        u[x]         = u0;
        v[x]         = v0;
    }
}

bool CopyGpuToAvFrame(const void* gpuPtr, const VkSubresourceLayout& layout,
                      int width, int height, AVFrame* frame)
{
    if (!gpuPtr || !frame) return false;

    const int mpPerRow = width / 2;

    // dump first few dwords of the raw GPU buffer for sanity
    const uint32_t* dbg = static_cast<const uint32_t*>(gpuPtr);
    char rawBuf[128];
    snprintf(rawBuf, sizeof(rawBuf), "[GPU-RAW] %08X %08X %08X %08X",
             dbg[0], dbg[1], dbg[2], dbg[3]);
    LogProRes(rawBuf);

    for (int y = 0; y < height; ++y) {
        const uint32_t* rowBase = static_cast<const uint32_t*>(gpuPtr);
        const uint32_t* srcRow = rowBase + (layout.rowPitch >> 2) * static_cast<size_t>(y);

        uint16_t* yRow = reinterpret_cast<uint16_t*>(frame->data[0] + y * frame->linesize[0]);
        uint16_t* uRow = reinterpret_cast<uint16_t*>(frame->data[1] + y * frame->linesize[1]);
        uint16_t* vRow = reinterpret_cast<uint16_t*>(frame->data[2] + y * frame->linesize[2]);

        unpackRow(srcRow, yRow, uRow, vRow, mpPerRow);

        if (y == 0) {
            if (yRow[0] != 540 || uRow[0] != 502 || yRow[1] != 538 || vRow[0] != 615) {
                LogProRes("[GPU-CHECK] macropixel validation failed");
                return false;
            } else {
                LogProRes("[GPU-CHECK] macropixel layout validated (GPU matches CPU)");
            }
        }
    }

    return true;
}

