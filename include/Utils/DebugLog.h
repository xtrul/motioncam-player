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

// Simple logging convenience macros used throughout the player. These prepend
// a severity tag and send the message to both log files.
#define DBG_INFO(msg)  do { \ 
    LogToFile(std::string("[INFO] ") + (msg)); \
    LogProRes(std::string("[INFO] ") + (msg)); \
} while(0)

#define DBG_WARN(msg)  do { \
    LogToFile(std::string("[WARN] ") + (msg)); \
    LogProRes(std::string("[WARN] ") + (msg)); \
} while(0)

#define DBG_ERROR(msg) do { \
    LogToFile(std::string("[ERROR] ") + (msg)); \
    LogProRes(std::string("[ERROR] ") + (msg)); \
} while(0)

#define DBG_TRACE(msg) do { \
    LogToFile(std::string("[TRACE] ") + (msg)); \
    LogProRes(std::string("[TRACE] ") + (msg)); \
} while(0)

#endif // DEBUG_LOG_H
