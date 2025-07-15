#ifndef ENCODING_CONFIG_H
#define ENCODING_CONFIG_H

#include <string>

struct ProResSettings {
    std::string codec = "prores_ks";
    std::string profile = "standard";
    std::string pixFmt = "yuv422p10le";
    int bitrate = 185000000;
    int sliceCount = 0;
    int threadCount = 0;
};

struct DnxhrSettings {
    std::string profile = "HQX";
    std::string pixFmt = "yuv422p10le";
    int bitrate = 185000000;
    int threadCount = 0;
};

struct HevcGpuSettings {
    std::string encoder = "hevc_amf";
    std::string usage = "transcoding";
    std::string quality = "slow";
    std::string profile = "main10";
    std::string tier = "high";
    std::string pixFmt = "p010";
    std::string rateControl = "abr";
    std::string bitrate = "250M";
    std::string maxrate = "250M";
    std::string bufsize = "500M";
    int gop = 1;
    int forcedIdr = 1;
    std::string colorPrimaries = "bt709";
    std::string colorTrc = "bt709";
    std::string colorspace = "bt709";
    int qpI = -1;
    int qpP = -1;
    int qpB = -1;
};

struct EncodingConfig {
    ProResSettings prores;
    DnxhrSettings dnxhr;
    HevcGpuSettings hevcGpu;
};

EncodingConfig loadEncodingConfig(const std::string& path);

#endif // ENCODING_CONFIG_H
