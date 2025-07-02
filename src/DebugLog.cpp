// --- START OF FILE src/DebugLog.cpp ---
#include "DebugLog.h"

#include <fstream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <cstring>  // For strlen, not strictly needed for this version but often included
#include <filesystem>
#if defined(__APPLE__)
#include <pwd.h>
#include <unistd.h>
#endif


namespace {
std::string getLogFilePath() {
#if defined(__APPLE__)
    const char* home = getenv("HOME");
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        if (pw) home = pw->pw_dir;
    }
    if (home) {
        std::string logDir = std::string(home) + "/Library/Logs/MotionCam Tools";
        std::filesystem::create_directories(logDir);
        return logDir + "/motioncam_player_debug_log.txt";
    }
#endif
    return "motioncam_player_debug_log.txt";
}
} // namespace

// Definition of the logger
void LogToFile(const std::string& message) {
    // Static to ensure it's initialized once and persists.
    // `std::ios_base::app` ensures appending to the file.
    static std::ofstream log_file(getLogFilePath(), std::ios_base::app | std::ios_base::out);

    if (log_file.is_open()) {
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        std::time_t time_now = std::chrono::system_clock::to_time_t(now);

        std::tm timeinfo{};
#ifdef _WIN32
        localtime_s(&timeinfo, &time_now); // Windows-specific thread-safe version
#else
        localtime_r(&time_now, &timeinfo); // POSIX thread-safe version
#endif

        std::ostringstream oss;
        oss << std::put_time(&timeinfo, "%Y-%m-%d %H:%M:%S");
        oss << '.' << std::setfill('0') << std::setw(3) << ms.count();

        log_file << "[" << oss.str() << "] " << message << std::endl; // std::endl flushes the stream
    }
    // Optional: also print to console for immediate feedback during debugging
    // std::cout << "[" << oss.str() << "] " << message << std::endl;
}
