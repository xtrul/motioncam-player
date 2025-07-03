#include "Utils/ProResConverter.h"
#include "Utils/DebugLog.h"
#include <filesystem>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
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

struct CpuParams {
    int W;
    int H;
    int cfaType;
    float exposure;
    float blackLevel;
    float whiteLevel;
    float invBlackWhiteRange;
    float gainR;
    float gainG;
    float gainB;
    glm::mat3 CCM;
    float saturationAdjustment;
};

static int getCfaType(const std::string& c) {
    std::string upper = c;
    std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char ch){return static_cast<char>(std::toupper(ch));});
    if (upper == "BGGR") return 0;
    if (upper == "RGGB") return 1;
    if (upper == "GBRG") return 2;
    if (upper == "GRBG") return 3;
    return 0;
}

static float srgb_eotf(float v) {
    v = std::clamp(v, 0.0f, 1.0f);
    return (v <= 0.0031308f) ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
}

static CpuParams computeParams(const nlohmann::json& frameMeta, const nlohmann::json& containerMeta) {
    CpuParams p{};
    p.W = frameMeta.value("width", 0);
    p.H = frameMeta.value("height", 0);
    std::string cfa = containerMeta.value("sensorArrangement", containerMeta.value("sensorArrangment", "BGGR"));
    p.cfaType = getCfaType(cfa);
    p.exposure = 1.0f;
    double staticBlack = 0.0;
    if (containerMeta.contains("blackLevel")) {
        const auto& jb = containerMeta.at("blackLevel");
        if (jb.is_array() && !jb.empty()) {
            double avg = 0.0; size_t cnt = 0;
            for (const auto& v : jb) { if (v.is_number()) { avg += v.get<double>(); cnt++; } }
            if (cnt > 0) staticBlack = avg / cnt;
        } else if (jb.is_number()) staticBlack = jb.get<double>();
    }
    double staticWhite = containerMeta.value("whiteLevel", 65535.0);
    float blackLvl = static_cast<float>(staticBlack);
    if (frameMeta.contains("dynamicBlackLevel")) {
        const auto& jb = frameMeta.at("dynamicBlackLevel");
        if (jb.is_array() && !jb.empty()) {
            double avg = 0.0; size_t cnt = 0;
            for (const auto& v : jb) { if (v.is_number()) { avg += v.get<double>(); cnt++; } }
            if (cnt > 0) blackLvl = static_cast<float>(avg / cnt);
        } else if (jb.is_number()) {
            blackLvl = jb.get<float>();
        }
    }
    float whiteLvl = static_cast<float>(staticWhite);
    if (frameMeta.contains("dynamicWhiteLevel") && frameMeta.at("dynamicWhiteLevel").is_number()) {
        whiteLvl = frameMeta.at("dynamicWhiteLevel").get<float>();
    }
    p.blackLevel = blackLvl;
    p.whiteLevel = whiteLvl;
    float range = p.whiteLevel - p.blackLevel;
    p.invBlackWhiteRange = (range <= 1e-5f) ? 1.0f : (1.0f / range);
    auto asn_json = frameMeta.value("asShotNeutral", nlohmann::json::array({1.0,1.0,1.0}));
    std::vector<double> asn_values = {1.0,1.0,1.0};
    if (asn_json.is_array() && asn_json.size() >= 3) {
        bool ok = true; std::vector<double> tmp; tmp.reserve(asn_json.size());
        for (const auto& e : asn_json) { if (e.is_number()) tmp.push_back(e.get<double>()); else { ok=false; break; } }
        if (ok && tmp.size() >= 3) asn_values = tmp;
    }
    p.gainG = 1.0f;
    p.gainR = (asn_values.size() >= 2 && asn_values[0] > 1e-6 && asn_values[1] > 1e-6) ? static_cast<float>(asn_values[1] / asn_values[0]) : 1.0f;
    p.gainB = (asn_values.size() >= 3 && asn_values[2] > 1e-6 && asn_values[1] > 1e-6) ? static_cast<float>(asn_values[1] / asn_values[2]) : 1.0f;
    glm::mat3 ccm(1.0f);
    nlohmann::json ccm_json;
    if (frameMeta.contains("ColorMatrix2") && frameMeta.at("ColorMatrix2").is_array() && frameMeta.at("ColorMatrix2").size() == 9) ccm_json = frameMeta.at("ColorMatrix2");
    else if (frameMeta.contains("ColorMatrix") && frameMeta.at("ColorMatrix").is_array() && frameMeta.at("ColorMatrix").size() == 9) ccm_json = frameMeta.at("ColorMatrix");
    if (!ccm_json.is_null()) {
        bool valid = true; int idx=0;
        for (int r=0;r<3 && valid;++r) for (int c=0;c<3;++c,++idx) {
            if (ccm_json.at(idx).is_number()) ccm[c][r] = ccm_json.at(idx).get<float>(); else { valid=false; break; }
        }
        if (!valid) ccm = glm::mat3(1.0f);
    }
    p.CCM = ccm;
    p.saturationAdjustment = 1.5f;
    return p;
}

static void convertFrameRGB(const uint16_t* raw, const CpuParams& p, std::vector<uint16_t>& out) {
    out.resize(static_cast<size_t>(p.W) * static_cast<size_t>(p.H) * 3);
    auto readVal = [&](int x, int y) -> uint16_t {
        x = std::clamp(x, 0, p.W - 1);
        y = std::clamp(y, 0, p.H - 1);
        return raw[y * p.W + x];
    };
    auto lin = [&](uint16_t v) -> float {
        float t = (static_cast<float>(v) - p.blackLevel) * p.invBlackWhiteRange;
        t = std::clamp(t * p.exposure, 0.0f, 1.0f);
        return t;
    };
    auto interpG = [&](int x, int y) -> float {
        return 0.25f * (lin(readVal(x + 1, y)) + lin(readVal(x - 1, y)) + lin(readVal(x, y + 1)) + lin(readVal(x, y - 1)));
    };
    auto interpH = [&](int x, int y) -> float {
        return 0.5f * (lin(readVal(x + 1, y)) + lin(readVal(x - 1, y)));
    };
    auto interpV = [&](int x, int y) -> float {
        return 0.5f * (lin(readVal(x, y + 1)) + lin(readVal(x, y - 1)));
    };
    auto interpD = [&](int x, int y) -> float {
        return 0.25f * (lin(readVal(x + 1, y + 1)) + lin(readVal(x - 1, y + 1)) + lin(readVal(x + 1, y - 1)) + lin(readVal(x - 1, y - 1)));
    };

    for (int y = 0; y < p.H; ++y) {
        bool ye = (y % 2) == 0;
        for (int x = 0; x < p.W; ++x) {
            bool xe = (x % 2) == 0;
            float r = 0.0f, g = 0.0f, b = 0.0f;
            switch (p.cfaType) {
                case 0: // BGGR
                    if (ye) {
                        if (xe) { b = lin(readVal(x,y)); g = interpG(x,y); r = interpD(x,y); }
                        else { g = lin(readVal(x,y)); r = interpV(x,y); b = interpH(x,y); }
                    } else {
                        if (xe) { g = lin(readVal(x,y)); r = interpH(x,y); b = interpV(x,y); }
                        else { r = lin(readVal(x,y)); g = interpG(x,y); b = interpD(x,y); }
                    }
                    break;
                case 1: // RGGB
                    if (ye) {
                        if (xe) { r = lin(readVal(x,y)); g = interpG(x,y); b = interpD(x,y); }
                        else { g = lin(readVal(x,y)); r = interpH(x,y); b = interpV(x,y); }
                    } else {
                        if (xe) { g = lin(readVal(x,y)); r = interpV(x,y); b = interpH(x,y); }
                        else { b = lin(readVal(x,y)); g = interpG(x,y); r = interpD(x,y); }
                    }
                    break;
                case 2: // GBRG
                    if (ye) {
                        if (xe) { g = lin(readVal(x,y)); r = interpV(x,y); b = interpH(x,y); }
                        else { b = lin(readVal(x,y)); g = interpG(x,y); r = interpD(x,y); }
                    } else {
                        if (xe) { r = lin(readVal(x,y)); g = interpG(x,y); b = interpD(x,y); }
                        else { g = lin(readVal(x,y)); r = interpH(x,y); b = interpV(x,y); }
                    }
                    break;
                default: // GRBG
                    if (ye) {
                        if (xe) { g = lin(readVal(x,y)); r = interpH(x,y); b = interpV(x,y); }
                        else { r = lin(readVal(x,y)); g = interpG(x,y); b = interpD(x,y); }
                    } else {
                        if (xe) { b = lin(readVal(x,y)); g = interpG(x,y); r = interpD(x,y); }
                        else { g = lin(readVal(x,y)); r = interpV(x,y); b = interpH(x,y); }
                    }
                    break;
            }
            float r_wb = std::clamp(r * p.gainR, 0.0f, 1.0f);
            float g_wb = std::clamp(g * p.gainG, 0.0f, 1.0f);
            float b_wb = std::clamp(b * p.gainB, 0.0f, 1.0f);
            glm::vec3 col = p.CCM * glm::vec3(r_wb, g_wb, b_wb);
            col = glm::clamp(col, 0.0f, 1.0f);
            float luminance = glm::dot(col, glm::vec3(0.2126f,0.7152f,0.0722f));
            glm::vec3 grayscale(luminance);
            glm::vec3 sat = glm::mix(grayscale, col, p.saturationAdjustment);
            sat = glm::clamp(sat, 0.0f, 1.0f);
            uint16_t r16 = static_cast<uint16_t>(std::clamp(srgb_eotf(sat.r) * 65535.0f, 0.0f, 65535.0f));
            uint16_t g16 = static_cast<uint16_t>(std::clamp(srgb_eotf(sat.g) * 65535.0f, 0.0f, 65535.0f));
            uint16_t b16 = static_cast<uint16_t>(std::clamp(srgb_eotf(sat.b) * 65535.0f, 0.0f, 65535.0f));
            size_t idx = (static_cast<size_t>(y) * p.W + x) * 3;
            out[idx] = r16; out[idx+1] = g16; out[idx+2] = b16;
        }
    }
}

bool exportDecodedFramesToProRes(DecoderWrapper* decoder, const std::string& outputPath) {
    if (!decoder || !decoder->getDecoder()) {
        LogProRes("Decoder pointer invalid");
        return false;
    }
    const auto& frames = decoder->getDecoder()->getFrames();
    if (frames.empty()) {
        LogProRes("No frames found for export");
        return false;
    }

    std::vector<uint8_t> rawData;
    nlohmann::json meta;
    decoder->getDecoder()->loadFrame(frames[0], rawData, meta);
    CpuParams params = computeParams(meta, decoder->getContainerMetadata());
    int W = params.W, H = params.H;
    if (W <= 0 || H <= 0) {
        LogProRes("Invalid frame dimensions");
        return false;
    }

    std::string cmd = std::string("ffmpeg -y -f rawvideo -pix_fmt rgb48le -s ") + std::to_string(W) + "x" + std::to_string(H) +
        " -i - -c:v prores_ks -profile:v 3 \"" + outputPath + "\"";
    LogProRes(std::string("Running: ") + cmd);
#ifdef _WIN32
    FILE* pipe = _popen(cmd.c_str(), "wb");
#else
    FILE* pipe = popen(cmd.c_str(), "w");
#endif
    if (!pipe) {
        LogProRes("Failed to launch ffmpeg");
        return false;
    }

    std::vector<uint16_t> rgb;
    convertFrameRGB(reinterpret_cast<const uint16_t*>(rawData.data()), params, rgb);
    fwrite(rgb.data(), sizeof(uint16_t), rgb.size(), pipe);
    for (size_t i = 1; i < frames.size(); ++i) {
        decoder->getDecoder()->loadFrame(frames[i], rawData, meta);
        params = computeParams(meta, decoder->getContainerMetadata());
        convertFrameRGB(reinterpret_cast<const uint16_t*>(rawData.data()), params, rgb);
        fwrite(rgb.data(), sizeof(uint16_t), rgb.size(), pipe);
    }
#ifdef _WIN32
    int ret = _pclose(pipe);
#else
    int ret = pclose(pipe);
#endif
    if (ret != 0) {
        LogProRes("ffmpeg returned non-zero");
        return false;
    }
    LogProRes("Conversion finished successfully.");
    return true;
}

} // namespace ProResConverter
