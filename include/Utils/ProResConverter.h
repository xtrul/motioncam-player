#ifndef PRORES_CONVERTER_H
#define PRORES_CONVERTER_H
#include <string>
#include "Utils/OrientationUtils.h"
#include "Decoder/DecoderWrapper.h"

namespace ProResConverter {
bool convertMcrawToProRes(const std::string& mcrawPath, OrientationTag orientTag);
bool exportDecodedFramesToProRes(DecoderWrapper* decoder, const std::string& outputPath);
}

#endif // PRORES_CONVERTER_H
