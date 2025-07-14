#pragma once
#include <string>
#include <vector>

struct SingleCodecSettings {
    bool enabled = true;
    std::string profile = "hq";
    int threads = 0; // 0 = auto
    std::string thread_type = "frame"; // "frame" or "slice"
    std::string pix_fmt = "yuv422p10le";
    std::vector<std::string> flags;
};

struct CodecSettings {
    SingleCodecSettings proRes;
    SingleCodecSettings dnxhr;
};

CodecSettings loadCodecSettings();
