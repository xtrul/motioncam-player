#ifndef DEBUG_LOG_H
#define DEBUG_LOG_H

#include <string>

// Simple file logger declaration
void LogToFile(const std::string& message);
void LogDnxhr(const std::string& message);
void LogProRes(const std::string& message);
// Logs whether FFmpeg/DNxHR support is available at runtime
void LogFFmpegStatus();

// Returns path to directory where log files should be stored.
// On Windows this uses the user's roaming AppData directory.
// On other platforms it falls back to the application base path.
std::string getLogDirectory();

#endif // DEBUG_LOG_H
