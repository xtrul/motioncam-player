#include "Utils/EncodingConfig.h"
#include "Utils/DebugLog.h"
#include <filesystem>
#include <fstream>

extern std::string g_AppBasePath;

EncodingConfig loadEncodingConfig() {
    EncodingConfig cfg;
    namespace fs = std::filesystem;
    fs::path base = g_AppBasePath.empty() ? fs::current_path() : fs::path(g_AppBasePath);
    fs::path path = base / "encoding_config.json";
    if (!fs::exists(path)) {
        LogToFile("[EncodingConfig] File not found: " + path.string());
        return cfg;
    }
    try {
        std::ifstream f(path);
        nlohmann::json j;
        f >> j;
        cfg.prores = j.value("prores", nlohmann::json{});
        cfg.dnxhr = j.value("dnxhr", nlohmann::json{});
        cfg.hevc_gpu = j.value("hevc_gpu", nlohmann::json{});
        LogToFile("[EncodingConfig] Loaded config from " + path.string());
    } catch (const std::exception& e) {
        LogToFile(std::string("[EncodingConfig] Failed to parse ") + path.string() + ": " + e.what());
    }
    return cfg;
}
