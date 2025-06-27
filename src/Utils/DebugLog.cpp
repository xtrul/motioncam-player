#include "Utils/DebugLog.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <mutex> // For thread-safe logging

static std::mutex g_log_mutex; // Mutex to protect file access

void LogToFile(const std::string& message) {
    std::lock_guard<std::mutex> lock(g_log_mutex); // Lock for thread safety

    static std::ofstream log_file("motioncam_player_log.txt", std::ios_base::app | std::ios_base::out);

    if (log_file.is_open()) {
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        std::time_t time_now = std::chrono::system_clock::to_time_t(now);

        std::tm timeinfo{};
#ifdef _WIN32
        localtime_s(&timeinfo, &time_now);
#else
        localtime_r(&time_now, &timeinfo);
#endif

        std::ostringstream oss;
        oss << std::put_time(&timeinfo, "%Y-%m-%d %H:%M:%S");
        oss << '.' << std::setfill('0') << std::setw(3) << ms.count();

        log_file << "[" << oss.str() << "] " << message << std::endl;
        // log_file.flush(); // Optional: flush immediately, impacts performance
    }
}