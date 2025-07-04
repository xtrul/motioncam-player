#ifndef DEBUG_LOG_H
#define DEBUG_LOG_H

#include <string>

// Simple file logger declaration
void LogToFile(const std::string& message);
// Separate log for ProRes export workflow
void LogProRes(const std::string& message);

// Returns path to directory where log files should be stored.
// On Windows this uses the user's roaming AppData directory.
// On other platforms it falls back to the application base path.
std::string getLogDirectory();

#endif // DEBUG_LOG_H
