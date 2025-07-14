#pragma once
#include <string>
#include <nlohmann/json.hpp>

struct CodecSettings {
    bool enabled = true;
    std::string profile;
    int threads = 0;
    std::string thread_type;
    std::string pix_fmt;
    nlohmann::json flags; // optional extra encoder flags
};

// Load settings for a codec from codec_settings.json located next to the executable.
// codecName should be "ProRes" or "DNxHR".
CodecSettings loadCodecSettings(const std::string& codecName);
