#pragma once
#include <vector>
#include <string>
#include <cstdint>

// Writes a single DNxHR frame to either a raw .dnxhd bitstream or an MXF
// container if `asMxf` is true. The input should already contain the
// properly formatted VC-3 slice data for one frame.
bool writeRawDnxhr(const std::vector<uint8_t>& frame,
                   int width, int height,
                   const std::string& path,
                   bool asMxf = false);

// Attempts to open the file via libavformat and verifies a DNxHR stream can
// be detected. Useful for round-trip testing with FFmpeg.
bool verifyDnxhrFile(const std::string& path);
