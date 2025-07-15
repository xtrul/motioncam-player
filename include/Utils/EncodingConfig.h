#ifndef ENCODING_CONFIG_H
#define ENCODING_CONFIG_H

#include <nlohmann/json.hpp>

struct EncodingConfig {
    nlohmann::json prores;
    nlohmann::json dnxhr;
    nlohmann::json hevc_gpu;
};

EncodingConfig loadEncodingConfig();

#endif // ENCODING_CONFIG_H
