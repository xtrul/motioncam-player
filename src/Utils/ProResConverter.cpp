#include "Utils/ProResConverter.h"
#include "Utils/DebugLog.h"
#include <filesystem>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace ProResConverter {

static bool isMirrorTag(OrientationTag tag) {
    return tag == OrientationTag::kMirror || tag == OrientationTag::kMirror180 ||
           tag == OrientationTag::kMirror90CW || tag == OrientationTag::kMirror90CCW;
}

static std::string buildFilter(OrientationTag tag) {
    int deg = orientationDegreesFromTag(tag);
    bool mirror = isMirrorTag(tag);
    std::string filter;
    switch (deg) {
        case 90:  filter = "transpose=1"; break;
        case 180: filter = "transpose=2,transpose=2"; break;
        case 270: filter = "transpose=2"; break;
        default: break;
    }
    if (mirror) {
        if (!filter.empty()) filter += ",";
        filter += "hflip";
    }
    return filter;
}

bool convertMcrawToProRes(const std::string& mcrawPath, OrientationTag orientTag) {
    fs::path inputPath(mcrawPath);
    fs::path outputPath = inputPath;
    outputPath.replace_extension(".mov");

    std::string filter = buildFilter(orientTag);
    std::string command = std::string("ffmpeg -y -i \"") + inputPath.string() +
        "\" -c:v prores_ks -profile:v 3";
    if (!filter.empty()) {
        command += " -vf \"" + filter + "\"";
    }
    command += " \"" + outputPath.string() + "\"";
    LogProRes(std::string("Command: ") + command);

#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_UTF8, 0, command.c_str(), -1, nullptr, 0);
    if (wlen == 0) {
        LogProRes("UTF-16 len fail.");
        return false;
    }
    std::wstring wcmd(wlen, L'\0');
    if (MultiByteToWideChar(CP_UTF8, 0, command.c_str(), -1, wcmd.data(), wlen) == 0) {
        LogProRes("UTF-16 conv fail.");
        return false;
    }
    STARTUPINFOW si{}; PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);
    if (!CreateProcessW(nullptr, wcmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP,
                        nullptr, nullptr, &si, &pi)) {
        LogProRes(std::string("CreateProcessW failed. ") + std::to_string(GetLastError()));
        return false;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    LogProRes("Conversion finished successfully.");
    return true;
#else
    int ret = system(command.c_str());
    if (ret != 0) {
        LogProRes("system() returned non-zero.");
        return false;
    }
    LogProRes("Conversion finished successfully.");
    return true;
#endif
}

} // namespace ProResConverter
