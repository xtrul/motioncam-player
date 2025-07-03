#include "Utils/ProResConverter.h"
#include "Utils/DebugLog.h"
#include <filesystem>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace ProResConverter {

bool convertMcrawToProRes(const std::string& mcrawPath) {
    fs::path inputPath(mcrawPath);
    fs::path outputPath = inputPath;
    outputPath.replace_extension(".mov");

    std::string command = std::string("ffmpeg -y -i \"") + inputPath.string() +
        "\" -c:v prores_ks -profile:v 3 \"" + outputPath.string() + "\"";
    LogToFile(std::string("[convertMcrawToProRes] Command: ") + command);

#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_UTF8, 0, command.c_str(), -1, nullptr, 0);
    if (wlen == 0) {
        LogToFile("[convertMcrawToProRes] UTF-16 len fail.");
        return false;
    }
    std::wstring wcmd(wlen, L'\0');
    if (MultiByteToWideChar(CP_UTF8, 0, command.c_str(), -1, wcmd.data(), wlen) == 0) {
        LogToFile("[convertMcrawToProRes] UTF-16 conv fail.");
        return false;
    }
    STARTUPINFOW si{}; PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);
    if (!CreateProcessW(nullptr, wcmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP,
                        nullptr, nullptr, &si, &pi)) {
        LogToFile(std::string("[convertMcrawToProRes] CreateProcessW failed. ") + std::to_string(GetLastError()));
        return false;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
#else
    int ret = system(command.c_str());
    if (ret != 0) {
        LogToFile("[convertMcrawToProRes] system() returned non-zero.");
        return false;
    }
    return true;
#endif
}

} // namespace ProResConverter
