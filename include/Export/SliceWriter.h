#pragma once
#include <vector>
#include <cstdint>
#include <string>

struct Slice {
    std::vector<uint16_t> coeffs;
    uint8_t qp{0};
};

class SliceWriter {
public:
    // Writes a frame worth of slices into the output buffer. The resulting
    // frame is padded to a 8192-byte boundary as required by DNxHR. If
    // `dumpPath` is non-empty, the raw bytes for this frame are also appended to
    // that file so the bitstream can be inspected externally.
    bool writeFrame(const std::vector<Slice>& slices,
                    int width, int height,
                    std::vector<uint8_t>& out,
                    const std::string& dumpPath = "",
                    bool debugHex = false);
};
