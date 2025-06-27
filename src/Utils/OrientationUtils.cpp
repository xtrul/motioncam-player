#include "Utils/OrientationUtils.h"
#include <string>
#include <algorithm>

static OrientationTag degreesToTag(int deg, bool flipped) {
    int norm = ((deg % 360) + 360) % 360;
    switch(norm) {
        case 0:   return flipped ? OrientationTag::kMirror : OrientationTag::kNormal;
        case 90:  return flipped ? OrientationTag::kMirror90CW : OrientationTag::kRotate90CW;
        case 180: return flipped ? OrientationTag::kMirror180 : OrientationTag::kRotate180;
        case 270: return flipped ? OrientationTag::kMirror90CCW : OrientationTag::kRotate90CCW;
        default:  return OrientationTag::kUnknown;
    }
}

static OrientationTag fromAndroidOrientationCode(int val, bool flipped) {
    switch(val) {
        case 0:  return degreesToTag(0, flipped);   // LANDSCAPE
        case 1:  return degreesToTag(90, flipped);  // PORTRAIT
        case 8:  return degreesToTag(180, flipped); // REVERSE_LANDSCAPE
        case 9:  return degreesToTag(270, flipped); // REVERSE_PORTRAIT
        default: return OrientationTag::kUnknown;
    }
}

OrientationTag computeOrientationTag(const nlohmann::json& orientationValue,
                                     bool isFlipped,
                                     OrientationTag defaultTag) {
    if (orientationValue.is_number_integer()) {
        int val = orientationValue.get<int>();
        if (val >= 1 && val <= 8) {
            // Values 1-8 are standard DNG orientation tags
            return static_cast<OrientationTag>(val);
        }
        // Surface rotation style codes (0,1,2,3)
        if (val >= 0 && val <= 3) {
            OrientationTag t = degreesToTag(val * 90, isFlipped);
            if (t != OrientationTag::kUnknown) return t;
        }
        // Android ActivityInfo codes (e.g. 0,1,8,9)
        {
            OrientationTag t = fromAndroidOrientationCode(val, isFlipped);
            if (t != OrientationTag::kUnknown) return t;
        }
        // interpret common degree values directly
        if (val == 0 || val == 90 || val == 180 || val == 270) {
            OrientationTag t = degreesToTag(val, isFlipped);
            if (t != OrientationTag::kUnknown) return t;
        }
    } else if (orientationValue.is_string()) {
        std::string s = orientationValue.get<std::string>();
        std::string lower; lower.reserve(s.size());
        std::transform(s.begin(), s.end(), std::back_inserter(lower), [](unsigned char c){return static_cast<char>(std::tolower(c));});
        // try parse numeric string
        try {
            int val = std::stoi(lower);
            OrientationTag t = degreesToTag(val, isFlipped);
            if (t != OrientationTag::kUnknown) return t;
            t = fromAndroidOrientationCode(val, isFlipped);
            if (t != OrientationTag::kUnknown) return t;
        } catch(...){}
        // recognize orientation names
        if (lower.find("reverse") != std::string::npos && lower.find("portrait") != std::string::npos)
            return degreesToTag(270, isFlipped);
        if (lower.find("reverse") != std::string::npos && lower.find("landscape") != std::string::npos)
            return degreesToTag(180, isFlipped);
        if (lower.find("portrait") != std::string::npos)
            return degreesToTag(90, isFlipped);
        if (lower.find("landscape") != std::string::npos)
            return degreesToTag(0, isFlipped);
        if (lower.find("90") != std::string::npos) return degreesToTag(90, isFlipped);
        if (lower.find("270") != std::string::npos) return degreesToTag(270, isFlipped);
        if (lower.find("180") != std::string::npos) return degreesToTag(180, isFlipped);
        if (lower.find("mirror") != std::string::npos && lower.find("180") == std::string::npos && lower.find("90") == std::string::npos && lower.find("270") == std::string::npos)
            return degreesToTag(0, true);
    }
    return defaultTag;
}

int orientationDegreesFromTag(OrientationTag tag) {
    switch(tag) {
        case OrientationTag::kRotate90CW:
        case OrientationTag::kMirror90CW:
            return 90;
        case OrientationTag::kRotate180:
        case OrientationTag::kMirror180:
            return 180;
        case OrientationTag::kRotate90CCW:
        case OrientationTag::kMirror90CCW:
            return 270;
        case OrientationTag::kNormal:
        case OrientationTag::kMirror:
            return 0;
        default:
            return 0;
    }
}
