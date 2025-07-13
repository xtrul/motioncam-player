#ifndef DNXHR_EXPORTER_H
#define DNXHR_EXPORTER_H

#include <string>
#include <vector>
#include <cstdint>

class DnxhrExporter {
public:
    struct SliceBuffer {
        std::vector<int16_t> lumaBlocks;
        std::vector<int16_t> chromaUBlocks;
        std::vector<int16_t> chromaVBlocks;
    };

    explicit DnxhrExporter(const std::string& outputPath);
    ~DnxhrExporter();

    bool addFrame(const SliceBuffer& slice);
    bool finalize();

private:
    std::string m_outputPath;
};

#endif // DNXHR_EXPORTER_H
