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
 * Copy a tightly packed GPU buffer into a planar AVFrame.
 *
 * The GPU memory must contain YUV422 data packed as 10-bit macropixels
 * (Y0, U, Y1, V) with no extra padding.  Each macropixel occupies eight
 * bytes so the expected row stride is <code>videoWidth * 4</code>.
 *
 * @param mappedGpuMemory Pointer to the start of the GPU buffer.
 * @param videoWidth      Frame width in pixels (must be even).
 * @param videoHeight     Frame height in pixels.
 * @param frame           Destination AVFrame, already allocated for
 *                        AV_PIX_FMT_YUV422P10LE.
 * @param validate        When true and PRORES_GPU_VALIDATE is defined the first
 *                        macropixel will be inspected for a known pattern.  If
 *                        it does not match, the function returns false.
 */
bool copyFromGpuToFrame(const void* mappedGpuMemory,
                        int videoWidth,
                        int videoHeight,
                        AVFrame* frame,
                        bool validate = false);
