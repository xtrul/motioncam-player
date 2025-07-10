#pragma once
#include <cstdint>
#include "ffmpeg_headers.hpp"

/**
 * \brief Copy GPU-mapped packed 10-bit YUV422 data into an AVFrame.
 *
 * The source buffer contains 32-bit macropixels packed as Y0, U, Y1, V
 * (four 10-bit samples). The destination frame must be allocated for the
 * AV_PIX_FMT_YUV422P10LE format. Values are left-shifted by 6 bits when written
 * so the 10-bit samples become 16-bit samples with zero-filled LSBs.
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
bool copyFromGpuToFrame(const void* mappedGpuMemory,
                        int videoWidth,
                        int videoHeight,
                        AVFrame* frame,
                        bool validate = false);
