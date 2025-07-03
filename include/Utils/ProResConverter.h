#ifndef PRORES_CONVERTER_H
#define PRORES_CONVERTER_H
#include <string>
#include "Utils/OrientationUtils.h"

namespace ProResConverter {
bool convertMcrawToProRes(const std::string& mcrawPath, OrientationTag orientTag);
}

#endif // PRORES_CONVERTER_H
