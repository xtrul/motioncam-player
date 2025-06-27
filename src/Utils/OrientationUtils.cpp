#include "Utils/OrientationUtils.h"
#include <string>
#include <algorithm>

static nlohmann::json searchOrientation(const nlohmann::json& j) {
    if (j.is_object()) {
        auto it = j.find("orientation");
        if (it != j.end()) return *it;
        for (auto it2 = j.begin(); it2 != j.end(); ++it2) {
            if (it2.key().find("orientation") != std::string::npos) return *it2;
            if (it2->is_object()) {
                nlohmann::json sub = searchOrientation(*it2);
                if (!sub.is_null()) return sub;
            }
        }
    }
    return nlohmann::json();
}

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
    nlohmann::json val = orientationValue;
    if (val.is_object()) {
        val = searchOrientation(val);
    }
    if (val.is_number_integer()) {
        int iv = val.get<int>();
        if (iv >= 1 && iv <= 8) {
            // Values 1-8 are standard DNG orientation tags
            return static_cast<OrientationTag>(iv);
        }
        // Surface rotation style codes (0,1,2,3)
        if (iv >= 0 && iv <= 3) {
            OrientationTag t = degreesToTag(iv * 90, isFlipped);
            if (t != OrientationTag::kUnknown) return t;
        }
        // Android ActivityInfo codes (e.g. 0,1,8,9)
        {
            OrientationTag t = fromAndroidOrientationCode(iv, isFlipped);
            if (t != OrientationTag::kUnknown) return t;
        }
        // interpret common degree values directly
        if (iv == 0 || iv == 90 || iv == 180 || iv == 270) {
            OrientationTag t = degreesToTag(iv, isFlipped);
            if (t != OrientationTag::kUnknown) return t;
        }
    } else if (val.is_string()) {
        std::string s = val.get<std::string>();
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

nlohmann::json findOrientationValue(const nlohmann::json& j) {
    return searchOrientation(j);
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
