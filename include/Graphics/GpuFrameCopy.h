#pragma once
#include <cstdint>
#include "ffmpeg_headers.hpp"

/**
 * \brief Copy GPU-mapped packed 10-bit YUV422 data into an AVFrame.
 *
 * The source buffer contains packed 10-bit macropixels in the order Y0 U Y1 V.
 * Samples are tightly packed across dword boundaries (40 bits per macropixel).
 * The destination frame must be allocated for the AV_PIX_FMT_YUV422P10LE
 * format. Values are left-shifted by 6 bits so the 10-bit samples become
 * 16-bit with zero-filled LSBs.
 *
 * When PRORES_GPU_VALIDATE is defined and \p validate is true, the very first
 * macropixel is inspected. If it appears invalid the function logs an error and
 * returns false so the caller may fall back to a CPU implementation.
 */
/**
 * The \p videoWidth must be an even value since the packed GPU buffer stores
 * two Y samples for every chroma pair. The destination \p frame must already
 * be allocated using av_frame_alloc() and configured for AV_PIX_FMT_YUV422P10LE
 * format.
 */
/**
 * The \p rowPitchBytes parameter is the stride in bytes between consecutive
 * rows of the GPU buffer as returned by vkGetImageSubresourceLayout. It may be
 * larger than \p videoWidth * 4 due to alignment requirements.
 */
bool copyFromGpuToFrame(const void* mappedGpuMemory,
                        int videoWidth,
                        int videoHeight,
                        size_t rowPitchBytes,
                        AVFrame* frame,
                        bool validate = false);
