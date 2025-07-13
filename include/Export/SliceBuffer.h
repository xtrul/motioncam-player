#ifndef SLICE_BUFFER_H
#define SLICE_BUFFER_H

#include <cstdint>
#include <vector>

struct SliceBuffer {
    std::vector<int16_t> lumaBlocks;
    std::vector<int16_t> chromaUBlocks;
    std::vector<int16_t> chromaVBlocks;
};

#endif // SLICE_BUFFER_H
