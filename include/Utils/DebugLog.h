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

// Convenience macros for unified logging with severity tags
#ifndef DBG_INFO
#   define DBG_INFO(msg)  do { LogToFile(std::string("[INFO] ") + (msg)); LogProRes(std::string("[INFO] ") + (msg)); } while(0)
#endif
#ifndef DBG_WARN
#   define DBG_WARN(msg)  do { LogToFile(std::string("[WARN] ") + (msg)); LogProRes(std::string("[WARN] ") + (msg)); } while(0)
#endif
#ifndef DBG_ERROR
#   define DBG_ERROR(msg) do { LogToFile(std::string("[ERROR] ") + (msg)); LogProRes(std::string("[ERROR] ") + (msg)); } while(0)
#endif
#ifndef DBG_TRACE
#   define DBG_TRACE(msg) do { LogToFile(std::string("[TRACE] ") + (msg)); LogProRes(std::string("[TRACE] ") + (msg)); } while(0)
#endif

#endif // DEBUG_LOG_H
