#include "Utils/DebugLog.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <mutex>   // For thread-safe logging
#include <string>
#include <filesystem>

static std::mutex g_log_mutex; // Mutex to protect file access

// Application base path defined in main.cpp
extern std::string g_AppBasePath;

// Helper function to lazily open the log file in a Logs directory
static std::ofstream& get_log_file() {
    static std::ofstream log_file;
    static bool initialized = false;
    if (!initialized) {
        std::filesystem::path logDir = std::filesystem::path(g_AppBasePath) / "Logs";
        std::error_code ec;
        std::filesystem::create_directories(logDir, ec); // Ignore errors, file open will fail if directory can't be created
        std::filesystem::path logPath = logDir / "motioncam_player_log.txt";
        log_file.open(logPath, std::ios_base::app | std::ios_base::out);
        initialized = true;
    }
    return log_file;
}

// Helper for dedicated ProRes export logging
static std::ofstream& get_prores_log_file() {
    static std::ofstream log_file;
    static bool initialized = false;
    if (!initialized) {
        std::filesystem::path logDir = std::filesystem::path(g_AppBasePath) / "Logs";
        std::error_code ec;
        std::filesystem::create_directories(logDir, ec);
        std::filesystem::path logPath = logDir / "prores_export_log.txt";
        log_file.open(logPath, std::ios_base::app | std::ios_base::out);
        initialized = true;
    }
    return log_file;
}

void LogToFile(const std::string& message) {
    std::lock_guard<std::mutex> lock(g_log_mutex); // Lock for thread safety

    std::ofstream& log_file = get_log_file();

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

void LogToProResFile(const std::string& message) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    std::ofstream& log_file = get_prores_log_file();
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
    }
}
