#ifndef DEBUG_LOG_H
#define DEBUG_LOG_H

#include <string>

// Simple file logger declaration
void LogToFile(const std::string& message);
void LogProRes(const std::string& message);
// Logs whether FFmpeg/ProRes support is available at runtime
void LogFFmpegStatus();

// Returns path to directory where log files should be stored.
// On Windows this uses the user's roaming AppData directory.
// On other platforms it falls back to the application base path.
std::string getLogDirectory();

// Convenience macros used throughout the player for rich diagnostics
#define DBG_INFO(msg)  LogProRes(std::string("[INFO] ")  + (msg))
#define DBG_WARN(msg)  LogProRes(std::string("[WARN] ")  + (msg))
#define DBG_ERROR(msg) LogProRes(std::string("[ERROR] ") + (msg))
#define DBG_TRACE(msg) LogProRes(std::string("[TRACE] ") + (msg))

#endif // DEBUG_LOG_H
