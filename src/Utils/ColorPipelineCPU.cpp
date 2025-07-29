#include "Utils/ColorPipelineCPU.h"
#include "Utils/QuadBayerDemosaic.h"

// Wrapper that forwards to the quad-Bayer demosaicing implementation.
void convertRawToRGB24(const uint16_t* raw, const CPUColorParams& p,
                       std::vector<uint8_t>& outRGB, unsigned threads)
{
    quadBayerDemosaic(raw, p, outRGB, threads);
}
