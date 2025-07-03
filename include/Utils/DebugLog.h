// FILE: include/Utils/DebugLog.h
#pragma once

#include <string>

// Simple file logger declaration
void LogToFile(const std::string& message);

// Separate logger for detailed ProRes export messages
void LogToProResFile(const std::string& message);
