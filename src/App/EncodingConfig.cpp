#include "App/EncodingConfig.h"
#include <nlohmann/json.hpp>
#include <fstream>

EncodingConfig loadEncodingConfig(const std::string& path) {
    EncodingConfig cfg;
    std::ifstream in(path);
    if (!in.is_open()) return cfg;
    nlohmann::json j;
    try {
        in >> j;
    } catch (...) {
        return cfg;
    }
    if (j.contains("prores")) {
        const auto& p = j["prores"];
        cfg.prores.codec = p.value("codec", cfg.prores.codec);
        cfg.prores.profile = p.value("profile", cfg.prores.profile);
        cfg.prores.pixFmt = p.value("pix_fmt", cfg.prores.pixFmt);
        cfg.prores.bitrate = p.value("bitrate", cfg.prores.bitrate);
        cfg.prores.sliceCount = p.value("slice_count", cfg.prores.sliceCount);
        cfg.prores.threadCount = p.value("thread_count", cfg.prores.threadCount);
    }
    if (j.contains("dnxhr")) {
        const auto& d = j["dnxhr"];
        cfg.dnxhr.profile = d.value("profile", cfg.dnxhr.profile);
        cfg.dnxhr.pixFmt = d.value("pix_fmt", cfg.dnxhr.pixFmt);
        cfg.dnxhr.bitrate = d.value("bitrate", cfg.dnxhr.bitrate);
        cfg.dnxhr.threadCount = d.value("thread_count", cfg.dnxhr.threadCount);
    }
    if (j.contains("hevc_gpu")) {
        const auto& h = j["hevc_gpu"];
        cfg.hevcGpu.encoder = h.value("encoder", cfg.hevcGpu.encoder);
        cfg.hevcGpu.usage = h.value("usage", cfg.hevcGpu.usage);
        cfg.hevcGpu.quality = h.value("quality", cfg.hevcGpu.quality);
        cfg.hevcGpu.profile = h.value("profile", cfg.hevcGpu.profile);
        cfg.hevcGpu.tier = h.value("tier", cfg.hevcGpu.tier);
        cfg.hevcGpu.pixFmt = h.value("pix_fmt", cfg.hevcGpu.pixFmt);
        cfg.hevcGpu.rateControl = h.value("rate_control", cfg.hevcGpu.rateControl);
        cfg.hevcGpu.bitrate = h.value("bitrate", cfg.hevcGpu.bitrate);
        cfg.hevcGpu.maxrate = h.value("maxrate", cfg.hevcGpu.maxrate);
        cfg.hevcGpu.bufsize = h.value("bufsize", cfg.hevcGpu.bufsize);
        cfg.hevcGpu.gop = h.value("gop", cfg.hevcGpu.gop);
        cfg.hevcGpu.forcedIdr = h.value("forced_idr", cfg.hevcGpu.forcedIdr);
        cfg.hevcGpu.colorPrimaries = h.value("color_primaries", cfg.hevcGpu.colorPrimaries);
        cfg.hevcGpu.colorTrc = h.value("color_trc", cfg.hevcGpu.colorTrc);
        cfg.hevcGpu.colorspace = h.value("colorspace", cfg.hevcGpu.colorspace);
        cfg.hevcGpu.qpI = h.value("qp_i", cfg.hevcGpu.qpI);
        cfg.hevcGpu.qpP = h.value("qp_p", cfg.hevcGpu.qpP);
        cfg.hevcGpu.qpB = h.value("qp_b", cfg.hevcGpu.qpB);
    }
    return cfg;
}
