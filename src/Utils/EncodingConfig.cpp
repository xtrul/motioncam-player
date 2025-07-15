#include "Utils/EncodingConfig.h"
#include "Utils/DebugLog.h"
#include <fstream>
#include <filesystem>

extern std::string g_AppBasePath;

nlohmann::json loadEncodingConfig()
{
    std::filesystem::path cfgPath = std::filesystem::path(g_AppBasePath) / "encoding_config.json";
    std::ifstream f(cfgPath);
    if (!f.is_open())
        return nlohmann::json::object();
    try {
        nlohmann::json j;
        f >> j;
        return j;
    } catch (const std::exception& e) {
        LogToFile(std::string("[EncodingConfig] parse failed: ") + e.what());
        return nlohmann::json::object();
    }
}
