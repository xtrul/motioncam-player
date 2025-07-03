#ifndef DEBUG_LOG_H
#define DEBUG_LOG_H

#include <string>

// Simple file logger declaration
void LogToFile(const std::string& message);

// Separate logger for detailed ProRes export messages
void LogToProResFile(const std::string& message);

#endif // DEBUG_LOG_H
