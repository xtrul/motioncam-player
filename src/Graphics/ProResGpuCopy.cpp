#include "Graphics/ProResGpuCopy.h"
#include "Utils/DebugLog.h"
#include <cstdint>
#include <cstring>

// helper to unpack one row of packed macropixels
static inline void unpackRow(const uint32_t* src, uint16_t* y, uint16_t* u,
                             uint16_t* v, int px)
{
    for (int x = 0; x < px; ++x) {
        uint32_t mp = src[x];
        y[2 * x + 0] = uint16_t((mp & 0x000003FFu) << 6);           // Y0
        u[x]         = uint16_t(((mp >> 10) & 0x000003FFu) << 6);   // U
        y[2 * x + 1] = uint16_t(((mp >> 20) & 0x000003FFu) << 6);   // Y1
        v[x]         = uint16_t(((mp >> 30) | (mp << 2)) << 6);     // V
    }
}

bool CopyGpuToAvFrame(const void* gpuPtr, const VkSubresourceLayout& layout,
                      int width, int height, AVFrame* frame)
{
    if (!gpuPtr || !frame) return false;

    const int mpPerRow = width / 2;

    for (int y = 0; y < height; ++y) {
        const uint32_t* srcRow = reinterpret_cast<const uint32_t*>(
            static_cast<const uint8_t*>(gpuPtr) + layout.rowPitch * static_cast<size_t>(y));

        uint16_t* yRow = reinterpret_cast<uint16_t*>(frame->data[0] + y * frame->linesize[0]);
        uint16_t* uRow = reinterpret_cast<uint16_t*>(frame->data[1] + y * frame->linesize[1]);
        uint16_t* vRow = reinterpret_cast<uint16_t*>(frame->data[2] + y * frame->linesize[2]);

        unpackRow(srcRow, yRow, uRow, vRow, mpPerRow);

        if (y == 0) {
            // verify the first macropixel after unpack
            if ((yRow[0] >> 6) != 2 || (uRow[0] >> 6) != 512 ||
                (yRow[1] >> 6) != 4 || (vRow[0] >> 6) != 512) {
                LogProRes("[GPU-CHECK] macropixel validation failed");
                return false;
            } else {
                LogProRes("[GPU-CHECK] macropixel layout validated" );
            }
        }
    }

    return true;
}

