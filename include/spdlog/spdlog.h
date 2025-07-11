#pragma once
#include <cstdarg>
#include <cstdio>

namespace spdlog {
inline void error(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

inline void debug(const char* fmt, ...) {
#ifndef NDEBUG
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
#else
    (void)fmt;
#endif
}
} // namespace spdlog
