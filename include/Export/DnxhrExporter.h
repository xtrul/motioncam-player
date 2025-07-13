#pragma once

#include "ffmpeg_headers.hpp"
#include <cassert>
#include <string>

AVCodecContext* create_dnxhr_hqx_encoder(int width, int height, AVRational time_base);

void LogDnxhr(const std::string& message);
