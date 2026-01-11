#ifndef ORIENTATION_UTILS_H
#define ORIENTATION_UTILS_H

#include <nlohmann/json.hpp>

enum class OrientationTag {
    kUnknown = 0,
    kNormal = 1,
    kMirror = 2,
    kRotate180 = 3,
    kMirror180 = 4,
    kMirror90CW = 5,
    kRotate90CW = 6,
    kMirror90CCW = 7,
    kRotate90CCW = 8
};

OrientationTag computeOrientationTag(const nlohmann::json& orientationValue,
                                     bool isFlipped,
                                     OrientationTag defaultTag = OrientationTag::kNormal);

int orientationDegreesFromTag(OrientationTag tag);

// Search recursively for a JSON field whose key contains "orientation" and
// return the associated value. Returns null json if not found.
nlohmann::json findOrientationValue(const nlohmann::json& j);

#endif // ORIENTATION_UTILS_H
