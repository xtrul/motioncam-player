// FILE: src/main.cpp
#include <iostream>
#include <string>
#include <filesystem>
#include <stdexcept>
#include <fstream>
#include <chrono>
#include <ctime>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <vector> // Required for GetModuleFileNameW buffer

#ifdef _WIN32
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>
#   include <commdlg.h>
#   include <shellapi.h>
#   pragma comment(lib, "comdlg32.lib")
#   include <stdio.h>
#   include <fcntl.h>
#   include <io.h>
#elif defined(__linux__)
#   include <unistd.h> // For readlink
#   include <linux/limits.h> // For PATH_MAX
#elif defined(__APPLE__)
#   include <mach-o/dyld.h> // For _NSGetExecutablePath
// PATH_MAX might be in limits.h or a large buffer might be used
#   ifndef PATH_MAX
#       define PATH_MAX 1024 // Or some other reasonable default
#   endif
#endif

#include <SDL.h>

#include "App/App.h"
#include "Utils/DebugLog.h"
#ifdef _WIN32
#include "Utils/SingleInstanceGuard.h"
#ifdef _WIN32
namespace DebugLogHelper {
    extern std::string wstring_to_utf8(const std::wstring& wstr);
}
#endif
#endif

namespace fs = std::filesystem;

// Global variable to store the application's base path
std::string g_AppBasePath;

void determineAppBasePath(const char* argv0) {
    fs::path exePath;
#ifdef _WIN32
    std::vector<wchar_t> pathBuf;
    DWORD copied = 0;
    do {
        pathBuf.resize(pathBuf.size() + MAX_PATH + 1); // +1 for null terminator safety if needed by some interpretations
        copied = GetModuleFileNameW(NULL, pathBuf.data(), static_cast<DWORD>(pathBuf.size()));
        if (copied == 0) { // Error
            LogToFile("[determineAppBasePath] GetModuleFileNameW failed. Error: " + std::to_string(GetLastError()));
            break;
        }
    } while (copied >= pathBuf.size()); // Loop if buffer was too small

    if (copied > 0) {
        pathBuf.resize(copied); // Resize to actual length
        exePath = fs::path(std::wstring(pathBuf.begin(), pathBuf.end()));
    }
    else {
        // Fallback if GetModuleFileNameW failed catastrophically
        exePath = fs::absolute(fs::path(argv0)); // Try to use argv0
    }
#elif defined(__linux__)
    char path[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", path, PATH_MAX);
    if (count > 0) {
        exePath = fs::path(std::string(path, count));
    }
    else {
        LogToFile("[determineAppBasePath] readlink /proc/self/exe failed. Error: " + std::string(strerror(errno)));
        exePath = fs::absolute(fs::path(argv0)); // Try to use argv0
    }
#elif defined(__APPLE__)
    char path[PATH_MAX];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) == 0) {
        exePath = fs::path(std::string(path));
    }
    else {
        LogToFile("[determineAppBasePath] _NSGetExecutablePath failed (buffer too small or other error).");
        // If buffer was too small, 'size' now contains required size. Could reallocate and retry.
        // For simplicity, falling back.
        exePath = fs::absolute(fs::path(argv0)); // Try to use argv0
    }
#else
    // Generic fallback: use argv[0] and try to make it absolute
    exePath = fs::absolute(fs::path(argv0));
#endif

    if (exePath.has_filename()) { //Check if it's a file path
        if (exePath.has_parent_path()) {
            g_AppBasePath = exePath.parent_path().string();
        }
        else {
            // This case implies exePath is something like "app.exe" found in PATH,
            // and argv0 didn't provide directory info. CWD at startup is the best guess.
            g_AppBasePath = fs::current_path().string();
            LogToFile("[determineAppBasePath] Warning: Executable path has no parent. Using CWD at startup as base path: " + g_AppBasePath);
        }
    }
    else { //exePath might be a directory itself if argv0 was "." or determination failed strangely
        g_AppBasePath = fs::absolute(exePath).string(); //Treat it as a directory path
        LogToFile("[determineAppBasePath] Warning: Executable path seems to be a directory or failed. Using absolute(exePath): " + g_AppBasePath);
    }
    LogToFile(std::string("[main] Determined App Base Path: ") + g_AppBasePath);
}


#ifdef _WIN32
void RedirectIOToConsole() {
    LogToFile("[RedirectIOToConsole] Attempting to redirect IO to console.");
    bool consoleAttached = false;
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        consoleAttached = true;
        LogToFile("[RedirectIOToConsole] Attached to parent console.");
    }
    else if (AllocConsole()) {
        consoleAttached = true;
        LogToFile("[RedirectIOToConsole] Allocated new console.");
    }

    if (consoleAttached) {
        FILE* fp = nullptr;
        if (freopen_s(&fp, "CONOUT$", "w", stdout) == 0 && fp != nullptr) {
            setvbuf(stdout, NULL, _IONBF, 0);
        }
        else {
            LogToFile("[RedirectIOToConsole] Failed to redirect stdout.");
        }
        if (freopen_s(&fp, "CONIN$", "r", stdin) == 0 && fp != nullptr) {
            setvbuf(stdin, NULL, _IONBF, 0);
        }
        else {
            LogToFile("[RedirectIOToConsole] Failed to redirect stdin.");
        }
        if (freopen_s(&fp, "CONOUT$", "w", stderr) == 0 && fp != nullptr) {
            setvbuf(stderr, NULL, _IONBF, 0);
        }
        else {
            LogToFile("[RedirectIOToConsole] Failed to redirect stderr.");
        }
        std::ios::sync_with_stdio(true);
        std::cout << "[RedirectIOToConsole] Console IO redirection attempted." << std::endl;
        std::cerr << "[RedirectIOToConsole] Test: stderr output after redirection." << std::endl;
        std::cout.flush(); std::cerr.flush();
    }
    else {
        LogToFile("[RedirectIOToConsole] Failed to attach or allocate console.");
    }
}
#endif

static std::string OpenMcrawDialog() {
    LogToFile("[OpenMcrawDialog] Called.");
#ifdef _WIN32
    OPENFILENAMEW ofn{};
    wchar_t szFile[MAX_PATH] = { 0 };
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = L"MotionCam RAW files\0*.mcraw\0All Files\0*.*\0";
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrDefExt = L"mcraw";

    if (GetOpenFileNameW(&ofn)) {
        std::string utf8Path = DebugLogHelper::wstring_to_utf8(szFile);
        if (utf8Path.empty() && szFile[0] != L'\0') {
            LogToFile("[OpenMcrawDialog] WideCharToMultiByte failed or returned empty. Error: " + std::to_string(GetLastError()));
            return {};
        }
        LogToFile(std::string("[OpenMcrawDialog] File selected: ") + utf8Path);
        return utf8Path;
    }
    LogToFile("[OpenMcrawDialog] Dialog cancelled or no file selected. GetLastError(): " + std::to_string(GetLastError()));
#else
    LogToFile("[OpenMcrawDialog] File dialog not implemented for this platform.");
    std::cerr << "File dialog not implemented. Please provide file as command line argument." << std::endl;
#endif
    return {};
}

int main(int argc, char* argv[]) {
#if defined(_WIN32) && !defined(NDEBUG)
#endif
    // Initialize logging as early as possible.
    // LogToFile("Application starting..."); // First log message

    // Determine and set the application base path.
    determineAppBasePath(argc > 0 ? argv[0] : "");


#ifdef _WIN32
    static const wchar_t* kMutexName = L"MCRAW_PLAYER_SINGLE_INSTANCE_MUTEX_V2_UNIQUE";
    SingleInstanceGuard instanceGuard(kMutexName);

    LogToFile(std::string("[main] SingleInstanceGuard created. Mutex handle valid: ") + (instanceGuard.getMutexHandle() != NULL && instanceGuard.getMutexHandle() != INVALID_HANDLE_VALUE ? "YES" : "NO") +
        ". GetLastError() after CreateMutexW: " + std::to_string(instanceGuard.getLastErrorAfterCreation()) +
        ". alreadyRunning() reports: " + (instanceGuard.alreadyRunning() ? "true" : "false"));

    if (instanceGuard.alreadyRunning()) {
        LogToFile("[main] Another instance is already running (detected by alreadyRunning() == true).");

        if (argc >= 2 && argv[1] != nullptr) {
            HWND hwnd = FindWindowW(L"MCRAW_PLAYER_IPC_WND_CLASS", nullptr);
            if (hwnd) {
                LogToFile(std::string("[main] Found existing instance window (HWND: ") + std::to_string(reinterpret_cast<uintptr_t>(hwnd)) + "). Sending file: " + argv[1]);
                std::filesystem::path fsPath(argv[1]);
                std::wstring wFilePath = fsPath.wstring();

                COPYDATASTRUCT cds{};
                cds.dwData = 0x4D435257;
                cds.cbData = static_cast<DWORD>((wFilePath.length() + 1) * sizeof(wchar_t));
                cds.lpData = (void*)wFilePath.c_str();

                SendMessageW(hwnd, WM_COPYDATA, (WPARAM)NULL, (LPARAM)&cds);
                LogToFile("[main] WM_COPYDATA sent.");
            }
            else {
                LogToFile("[main] Could not find existing instance window by class MCRAW_PLAYER_IPC_WND_CLASS to forward arguments.");
            }
        }
        else {
            LogToFile("[main] No file argument to forward (argc < 2 or argv[1] is null).");
        }
        LogToFile("[main] Exiting secondary instance.");
        return 0;
    }
    LogToFile("[main] This appears to be the first instance, or CreateMutexW did not report ERROR_ALREADY_EXISTS for this instance.");
#endif

    try {
        LogToFile(std::string("[main] Current Working Directory (at start): ") + fs::current_path().string());
    }
    catch (const fs::filesystem_error& e) {
        LogToFile(std::string("[main] Error getting CWD: ") + e.what());
    }

    LogToFile("--------------------------------------------------");
    LogToFile(std::string("[main] Continuing main() for primary instance. argc: ") + std::to_string(argc));
    if (argc > 0 && argv[0] != nullptr) {
        LogToFile(std::string("[main] argv[0]: ") + argv[0]);
    }
    else if (argc > 0) {
        LogToFile(std::string("[main] argv[0] is nullptr."));
    }


    std::string inPath;
    if (argc >= 2 && argv[1] != nullptr) {
        inPath = argv[1];
        LogToFile(std::string("[main] Input file from command line: ") + inPath);
    }
    else {
        LogToFile("[main] No command line argument provided or argv[1] is null, opening file dialog...");
        inPath = OpenMcrawDialog();
        if (inPath.empty()) {
            LogToFile("[main] No input file selected from dialog or dialog cancelled. Exiting.");
            return 0;
        }
        LogToFile(std::string("[main] Input file from dialog: ") + inPath);
    }

    if (!fs::exists(inPath) || !fs::is_regular_file(inPath)) {
        std::string errorMsg = "[main] Input file not found or not a regular file: " + inPath;
        LogToFile(errorMsg);
#ifdef _WIN32
        MessageBoxA(NULL, errorMsg.c_str(), "Error - MCRAW Player", MB_OK | MB_ICONERROR);
#endif
        std::cerr << errorMsg << std::endl;
        return 1;
    }
    if (fs::path(inPath).extension() != ".mcraw") {
        std::string errorMsg = "[main] Input file must have a .mcraw extension: " + inPath;
        LogToFile(errorMsg);
#ifdef _WIN32
        MessageBoxA(NULL, errorMsg.c_str(), "Error - MCRAW Player", MB_OK | MB_ICONERROR);
#endif
        std::cerr << errorMsg << std::endl;
        return 1;
    }

    LogToFile(std::string("[main] Initializing App with file: ") + inPath);
    try {
        App app(inPath);
        LogToFile("[main] App object created. Calling app.run()...");
        if (!app.run()) {
            LogToFile("[main] App::run() returned false. Application will exit.");
#ifdef _WIN32
            MessageBoxA(NULL, "Application run failed. See mcraw_player_debug_log.txt for details.", "Runtime Error - MCRAW Player", MB_OK | MB_ICONERROR);
#endif
            std::cerr << "[main] App::run() returned false. Application will exit." << std::endl;
            return 1;
        }
        LogToFile("[main] App::run() finished successfully.");
    }
    catch (const std::exception& e) {
        std::string errorMsg = "[main] FATAL STD EXCEPTION: " + std::string(e.what());
        LogToFile(errorMsg);
#ifdef _WIN32
        MessageBoxA(NULL, errorMsg.c_str(), "Runtime Error - MCRAW Player", MB_OK | MB_ICONERROR);
#endif
        std::cerr << errorMsg << std::endl;
        return 1;
    }
    catch (...) {
        std::string errorMsg = "[main] FATAL UNKNOWN EXCEPTION occurred.";
        LogToFile(errorMsg);
#ifdef _WIN32
        MessageBoxA(NULL, errorMsg.c_str(), "Runtime Error - MCRAW Player", MB_OK | MB_ICONERROR);
#endif
        std::cerr << errorMsg << std::endl;
        return 1;
    }

    LogToFile("[main] Application exiting normally (end of main).");
    return 0;
}