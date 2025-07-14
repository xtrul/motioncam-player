#include "Export/CodecSettings.h"
#include "Utils/DebugLog.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <thread>

extern std::string g_AppBasePath;

CodecSettings loadCodecSettings() {
    CodecSettings settings;
    unsigned hwThreads = std::thread::hardware_concurrency();
    if(hwThreads == 0) hwThreads = 1;
    settings.proRes.threads = hwThreads;
    settings.dnxhr.threads = hwThreads;

    std::filesystem::path base = g_AppBasePath.empty() ? std::filesystem::current_path() : std::filesystem::path(g_AppBasePath);
    std::filesystem::path cfgPath = base / "codec_settings.json";
    if(!std::filesystem::exists(cfgPath)) {
        LogProRes("[Warning] codec_settings.json not found. Using default ProRes settings");
        LogProRes("[Warning] codec_settings.json not found. Using default DNxHR settings");
        return settings;
    }

    try {
        std::ifstream f(cfgPath);
        nlohmann::json j;
        f >> j;
        if(j.contains("ProRes")) {
            auto &pr = j["ProRes"];
            settings.proRes.enabled = pr.value("enabled", settings.proRes.enabled);
            settings.proRes.profile = pr.value("profile", settings.proRes.profile);
            settings.proRes.threads = pr.value("threads", settings.proRes.threads);
            settings.proRes.thread_type = pr.value("thread_type", settings.proRes.thread_type);
            settings.proRes.pix_fmt = pr.value("pix_fmt", settings.proRes.pix_fmt);
            if(pr.contains("flags") && pr["flags"].is_array()) {
                settings.proRes.flags.clear();
                for(auto &v : pr["flags"]) settings.proRes.flags.push_back(v.get<std::string>());
            }
        }
        if(j.contains("DNxHR")) {
            auto &dn = j["DNxHR"];
            settings.dnxhr.enabled = dn.value("enabled", settings.dnxhr.enabled);
            settings.dnxhr.profile = dn.value("profile", settings.dnxhr.profile);
            settings.dnxhr.threads = dn.value("threads", settings.dnxhr.threads);
            settings.dnxhr.thread_type = dn.value("thread_type", settings.dnxhr.thread_type);
            settings.dnxhr.pix_fmt = dn.value("pix_fmt", settings.dnxhr.pix_fmt);
            if(dn.contains("flags") && dn["flags"].is_array()) {
                settings.dnxhr.flags.clear();
                for(auto &v : dn["flags"]) settings.dnxhr.flags.push_back(v.get<std::string>());
            }
        }
    } catch(const std::exception& e) {
        LogProRes(std::string("[Warning] Failed to parse codec_settings.json: ") + e.what());
    }
    return settings;
}
