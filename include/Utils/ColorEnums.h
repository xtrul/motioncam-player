#pragma once

enum class GammaCurve {
    SRGB,
    CineonLog,
    Slog3
};

enum class ColorSpace {
    Rec709,
    BT2020,
    SLogCinema
};

enum class ProResQuality {
    Proxy,
    LT,
    Standard,
    HQ
};
