// --- START OF FILE Logger.cpp ---

#include "Logger.h"
#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <cstdio> // For setvbuf if used

#ifdef _WIN32
#include <windows.h>
#endif

// The static logFile object will be created once.
// If you move this inside the function, it reopens/appends on every call, which is inefficient.
// Keeping it static outside or inside the function (as static local) is fine.
static std::ofstream logFile("mcraw_log.txt", std::ios_base::app | std::ios_base::out);

void Logger::log(const std::string& s) {
    // std::cout << s << '\n'; // Optional: also print to console
    if (logFile.is_open()) {
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        std::time_t time_now = std::chrono::system_clock::to_time_t(now);
        
        std::tm timeinfo{};
#ifdef _WIN32
        localtime_s(&timeinfo, &time_now);
#else
        localtime_r(&time_now, &timeinfo); // POSIX
#endif
        
        std::ostringstream oss_timestamp;
        oss_timestamp << std::put_time(&timeinfo, "%Y-%m-%d %H:%M:%S");
        oss_timestamp << '.' << std::setfill('0') << std::setw(3) << ms.count();
        
        logFile << "[" << oss_timestamp.str() << "] " << s << std::endl; // endl flushes
    }
}
