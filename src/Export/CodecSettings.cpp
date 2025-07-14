#include "Export/CodecSettings.h"
#include "Utils/DebugLog.h"
#include <fstream>
#include <filesystem>

extern std::string g_AppBasePath;

CodecSettings loadCodecSettings(const std::string& codecName) {
    CodecSettings cfg;
    // Reasonable defaults reflecting previous hardcoded values
    if(codecName == "ProRes") {
        cfg.profile = "hq";
        cfg.thread_type = "frame";
        cfg.pix_fmt = "yuv422p10le";
    } else if(codecName == "DNxHR") {
        cfg.profile = "hqx";
        cfg.thread_type = "frame";
        cfg.pix_fmt = "yuv422p10le";
    }

    std::filesystem::path path = std::filesystem::path(g_AppBasePath) / "codec_settings.json";
    if(!std::filesystem::exists(path)) {
        if(codecName == "ProRes") {
            LogProRes("[Warning] codec_settings.json not found. Using default ProRes settings");
        } else {
            LogDnxhr("[Warning] codec_settings.json not found. Using default DNxHR settings");
        }
        return cfg;
    }

    try {
        std::ifstream f(path);
        nlohmann::json j; f >> j;
        if(j.contains(codecName)) {
            const auto& jc = j[codecName];
            cfg.enabled = jc.value("enabled", cfg.enabled);
            cfg.profile = jc.value("profile", cfg.profile);
            cfg.threads = jc.value("threads", cfg.threads);
            cfg.thread_type = jc.value("thread_type", cfg.thread_type);
            cfg.pix_fmt = jc.value("pix_fmt", cfg.pix_fmt);
            if(jc.contains("flags")) cfg.flags = jc["flags"];
        } else {
            if(codecName == "ProRes") {
                LogProRes("[Warning] ProRes entry missing in codec_settings.json. Using defaults");
            } else {
                LogDnxhr("[Warning] DNxHR entry missing in codec_settings.json. Using defaults");
            }
        }
    } catch(const std::exception& e) {
        if(codecName == "ProRes") {
            LogProRes(std::string("[Warning] Failed to parse codec_settings.json: ") + e.what());
        } else {
            LogDnxhr(std::string("[Warning] Failed to parse codec_settings.json: ") + e.what());
        }
    }
    return cfg;
}
