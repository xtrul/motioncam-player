#include "Utils/DebugLog.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <mutex>   // For thread-safe logging
#include <string>
#include <filesystem>
#ifdef _WIN32
#   include <windows.h>
#   include <shlobj.h>
#endif
#if defined(ENABLE_PRORES_EXPORT) || defined(ENABLE_HEVC_EXPORT)
#include "ffmpeg_headers.hpp"
#endif

static std::mutex g_log_mutex; // Mutex to protect file access

// helper to format timestamp for log entries
static std::string make_timestamp()
{
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
    return oss.str();
}

// Application base path defined in main.cpp
extern std::string g_AppBasePath;

std::string getLogDirectory() {
#ifdef _WIN32
    std::string logDir;
    wchar_t* appDataPath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, NULL, &appDataPath))) {
        int size = WideCharToMultiByte(CP_UTF8, 0, appDataPath, -1, nullptr, 0, nullptr, nullptr);
        std::string appData(size - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, appDataPath, -1, &appData[0], size, nullptr, nullptr);
        CoTaskMemFree(appDataPath);
        logDir = appData + "\\MotionCam Tools\\Player\\logs";
    } else {
        logDir = std::filesystem::temp_directory_path().string() + "\\MotionCam Tools\\Player\\logs";
    }
    return logDir;
#else
    return (std::filesystem::path(g_AppBasePath) / "Logs").string();
#endif
}

// Helper function to lazily open the log file in a Logs directory
static std::ofstream& get_log_file() {
    static std::ofstream log_file;
    static bool initialized = false;
    if (!initialized) {
        std::filesystem::path logDir = getLogDirectory();
        std::error_code ec;
        std::filesystem::create_directories(logDir, ec); // Ignore errors, file open will fail if directory can't be created
        std::filesystem::path logPath = logDir / "motioncam_player_log.txt";
        log_file.open(logPath, std::ios_base::app | std::ios_base::out);
        initialized = true;
    }
    return log_file;
}

// Separate log file specifically for ProRes export diagnostics
static std::ofstream& get_prores_log_file() {
    static std::ofstream log_file;
    static bool initialized = false;
    if (!initialized) {
        std::filesystem::path logDir = getLogDirectory();
        std::error_code ec;
        std::filesystem::create_directories(logDir, ec);
        std::filesystem::path logPath = logDir / "prores_export_log.txt";
        log_file.open(logPath, std::ios_base::app | std::ios_base::out);
        initialized = true;
    }
    return log_file;
}

// Separate log file for HEVC export diagnostics
static std::ofstream& get_hevc_log_file() {
    static std::ofstream log_file;
    static bool initialized = false;
    if (!initialized) {
        std::filesystem::path logDir = getLogDirectory();
        std::error_code ec;
        std::filesystem::create_directories(logDir, ec);
        std::filesystem::path logPath = logDir / "hevc_export_log.txt";
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

void LogProRes(const std::string& message) {
    std::lock_guard<std::mutex> lock(g_log_mutex);

    std::ofstream& log_file = get_prores_log_file();
    if (log_file.is_open()) {
        log_file << "[" << make_timestamp() << "] " << message << std::endl;
    }
}

void LogHevc(const std::string& message) {
    std::lock_guard<std::mutex> lock(g_log_mutex);

    std::ofstream& log_file = get_hevc_log_file();
    if (log_file.is_open()) {
        log_file << "[" << make_timestamp() << "] " << message << std::endl;
    }
}

void LogFFmpegStatus() {
#if defined(ENABLE_PRORES_EXPORT) || defined(ENABLE_HEVC_EXPORT)
#ifdef ENABLE_PRORES_EXPORT
    LogToFile("[FFmpeg] ProRes export support compiled in.");
    LogProRes("[FFmpeg] ProRes export support compiled in.");
#endif
#ifdef ENABLE_HEVC_EXPORT
    LogToFile("[FFmpeg] HEVC export support compiled in.");
    LogHevc("[FFmpeg] HEVC export support compiled in.");
#   ifdef _WIN32
    const char* dlls[] = {
        "avcodec-61.dll", "avformat-61.dll", "avutil-59.dll",
        "swscale-8.dll", "swresample-5.dll", "avfilter-10.dll" };
    std::filesystem::path exeDir = g_AppBasePath.empty() ? std::filesystem::current_path() : std::filesystem::path(g_AppBasePath);
    bool allLoaded = true;
    for (const char* dll : dlls) {
        std::filesystem::path p = exeDir / dll;
        if (std::filesystem::exists(p)) {
            HMODULE h = LoadLibraryA(p.string().c_str());
            if (h) {
                LogToFile(std::string("[FFmpeg] Loaded ") + dll);
                LogProRes(std::string("[FFmpeg] Loaded ") + dll);
                FreeLibrary(h);
            } else {
                LogToFile(std::string("[FFmpeg] Failed to load ") + dll + " error " + std::to_string(GetLastError()));
                LogProRes(std::string("[FFmpeg] Failed to load ") + dll + " error " + std::to_string(GetLastError()));
                allLoaded = false;
            }
        } else {
            LogToFile(std::string("[FFmpeg] Missing ") + dll);
            LogProRes(std::string("[FFmpeg] Missing ") + dll);
            allLoaded = false;
        }
    }
    if (allLoaded) {
        LogToFile("[FFmpeg] All DLLs loaded; ProRes export enabled.");
        LogProRes("[FFmpeg] All DLLs loaded; ProRes export enabled.");
    } else {
        LogToFile("[FFmpeg] Some FFmpeg DLLs could not be loaded; export may fail.");
        LogProRes("[FFmpeg] Some FFmpeg DLLs could not be loaded; export may fail.");
    }
#   else
    av_register_all();
    int version = avcodec_version();
    LogToFile(std::string("[FFmpeg] avcodec version ") + std::to_string(version));
    LogProRes(std::string("[FFmpeg] avcodec version ") + std::to_string(version));
#   endif
#endif // ENABLE_HEVC_EXPORT
#else
    LogToFile("[FFmpeg] FFmpeg support not built; ProRes export unavailable.");
    LogProRes("[FFmpeg] FFmpeg support not built; ProRes export unavailable.");
#endif
}
