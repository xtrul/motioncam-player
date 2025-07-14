#include "Export/SliceWriter.h"
#include "Utils/DebugLog.h"
#include <fstream>

bool SliceWriter::writeFrame(const std::vector<Slice>& slices,
                             int width, int height,
                             std::vector<uint8_t>& out,
                             const std::string& dumpPath,
                             bool debugHex)
{
    size_t start = out.size();
    LogDnxhr(std::string("[SliceWriter] writing ") +
             std::to_string(slices.size()) + " slices");

    // DNxHR frame header prefix. This is a stripped down version of the official
    // VC-3 header: 0x830e00 00 0002 80 signals a new frame. Following fields are
    // not strictly compliant but allow simple frame size/qp logging.
    const uint8_t framePrefix[] = {0x83, 0x0E, 0x00, 0x00, 0x00, 0x00, 0x02, 0x80};
    out.insert(out.end(), std::begin(framePrefix), std::end(framePrefix));
    out.push_back(static_cast<uint8_t>((width >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(width & 0xFF));
    out.push_back(static_cast<uint8_t>((height >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(height & 0xFF));
    out.push_back(static_cast<uint8_t>((slices.size() >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(slices.size() & 0xFF));

    static const uint8_t zigzag[64] = {
        0,  1,  8, 16,  9,  2,  3, 10,
        17, 24, 32, 25, 18, 11,  4,  5,
        12, 19, 26, 33, 40, 48, 41, 34,
        27, 20, 13,  6,  7, 14, 21, 28,
        35, 42, 49, 56, 57, 50, 43, 36,
        29, 22, 15, 23, 30, 37, 44, 51,
        58, 59, 52, 45, 38, 31, 39, 46,
        53, 60, 61, 54, 47, 55, 62, 63
    };

    for(size_t idx=0; idx<slices.size(); ++idx){
        const auto& s = slices[idx];
        // Simplified slice header: start code and qscale
        const uint8_t slicePrefix[] = {0x00, 0x00, 0x01, 0xE0};
        out.insert(out.end(), std::begin(slicePrefix), std::end(slicePrefix));
        out.push_back(s.qp);
        size_t sliceStart = out.size();

        int run = 0;
        for(int i=0;i<64;++i){
            int16_t c = 0;
            if(i < static_cast<int>(s.coeffs.size()))
                c = s.coeffs[zigzag[i]];
            if(c == 0){
                run++;
                continue;
            }
            out.push_back(static_cast<uint8_t>(run & 0xFF));
            out.push_back(static_cast<uint8_t>((static_cast<uint16_t>(c) >> 8) & 0xFF));
            out.push_back(static_cast<uint8_t>(static_cast<uint16_t>(c) & 0xFF));
            run = 0;
        }
        // End of block marker
        out.push_back(0);

        if(debugHex){
            size_t sliceEnd = out.size();
            std::string shex;
            for(size_t b = sliceStart; b < sliceEnd; ++b){
                char buf[4];
                snprintf(buf, sizeof(buf), "%02X ", out[b]);
                shex += buf;
            }
            LogDnxhr("[SliceWriter] slice " + std::to_string(idx) +
                     " bytes=" + std::to_string(sliceEnd - sliceStart) +
                     " hex=" + shex);
        }
    }

    size_t written = out.size() - start;
    LogDnxhr(std::string("[SliceWriter] frame bytes=") + std::to_string(written));

    // Pad frame size to 8192 bytes as required by DNxHR alignment rules.
    size_t padTarget = (written + 8191) & ~static_cast<size_t>(8191);
    if(padTarget > written) {
        LogDnxhr(std::string("[SliceWriter] padding ") + std::to_string(padTarget - written) + " bytes to 8192-byte boundary");
        out.insert(out.end(), padTarget - written, 0);
        written = padTarget;
    }
    if(debugHex){
        LogDnxhr(std::string("[SliceWriter] padded frame size=") + std::to_string(written));
    }

    // Log first few bytes in hex for debugging slice header integrity.
    std::string hex;
    for(size_t i = 0; i < std::min<size_t>(16, written); ++i) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%02X ", out[start + i]);
        hex += buf;
    }
    LogDnxhr(std::string("[SliceWriter] first bytes: ") + hex);

    if(debugHex){
        std::string frameHex;
        for(size_t i=start;i<start+written;++i){
            char buf[4];
            snprintf(buf,sizeof(buf),"%02X ", out[i]);
            frameHex += buf;
        }
        LogDnxhr("[SliceWriter] frame hex=" + frameHex);
    }

    if(!dumpPath.empty()){
        std::ofstream f(dumpPath, std::ios::binary | std::ios::app);
        if(f.is_open()){
            f.write(reinterpret_cast<const char*>(out.data() + start), written);
            LogDnxhr(std::string("[SliceWriter] dumped to ") + dumpPath);
        } else {
            LogDnxhr(std::string("[SliceWriter] failed to open dump path ") + dumpPath);
        }
    }
    return true;
}
