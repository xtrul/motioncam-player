// --- START OF FILE src/main.cpp ---

// 1. Standard C++ Headers
#include <iostream>     // For std::cout, std::cerr
#include <string>       // For std::string
#include <filesystem>   // For std::filesystem
#include <stdexcept>    // For std::runtime_error
#include <fstream>      // For std::ofstream (used in DebugLog.cpp)
#include <chrono>       // For std::chrono (used in DebugLog.cpp)
#include <ctime>        // For std::time_t (used in DebugLog.cpp)
#include <cstring>      // For strlen, strdup
#include <iomanip>      // For std::put_time (used in DebugLog.cpp)
#include <sstream>      // For std::ostringstream (used in DebugLog.cpp)
#include <cstdio>       // For fprintf, stderr, fflush, popen, fgets, pclose, perror

// 2. Platform-specific headers
#ifdef _WIN32
#   include <windows.h>
#   include <shobjidl.h>
#   include <fcntl.h>
#   include <io.h>
#elif defined(__APPLE__)
#   include <mach-o/dyld.h>
#   include <unistd.h>
#   include <libgen.h>
#   include <limits.h>
#   include <sys/wait.h> // For WIFEXITED, WEXITSTATUS with pclose
#endif

// 3. Third-party library headers
#include <SDL.h>

// 4. Your Project's Headers
#include "App.h"
#include "DebugLog.h"
#include "SingleInstance.h"
#if defined(__APPLE__)
#include "MacOpenFile.h"
#endif

namespace fs = std::filesystem;

#ifdef _WIN32
void RedirectIOToConsole() {
    // Using fprintf for early diagnostics in this function too, if needed
    fprintf(stderr, "[RedirectIOToConsole_EARLY] Attempting to redirect IO.\n"); fflush(stderr);
    LogToFile("[RedirectIOToConsole] Attempting to redirect IO to console.");
    // ... (rest of your RedirectIOToConsole implementation) ...
    // Ensure to use LogToFile or fprintf(stderr,...) for its internal logging
    bool consoleAttached = false;
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        consoleAttached = true;
        fprintf(stderr, "[RedirectIOToConsole_EARLY] Attached to parent console.\n"); fflush(stderr);
        LogToFile("[RedirectIOToConsole] Attached to parent console.");
    }
    else if (AllocConsole()) {
        consoleAttached = true;
        fprintf(stderr, "[RedirectIOToConsole_EARLY] Allocated new console.\n"); fflush(stderr);
        LogToFile("[RedirectIOToConsole] Allocated new console.");
    }

    if (consoleAttached) {
        FILE* fp = nullptr;
        if (freopen_s(&fp, "CONOUT$", "w", stdout) == 0 && fp != nullptr) setvbuf(stdout, NULL, _IONBF, 0);
        else LogToFile("[RedirectIOToConsole] Failed to redirect stdout.");
        if (freopen_s(&fp, "CONIN$", "r", stdin) == 0 && fp != nullptr) setvbuf(stdin, NULL, _IONBF, 0);
        else LogToFile("[RedirectIOToConsole] Failed to redirect stdin.");
        if (freopen_s(&fp, "CONOUT$", "w", stderr) == 0 && fp != nullptr) setvbuf(stderr, NULL, _IONBF, 0);
        else LogToFile("[RedirectIOToConsole] Failed to redirect stderr.");
        
        std::ios::sync_with_stdio(true);
        fprintf(stderr, "[RedirectIOToConsole_EARLY] Console IO redirection complete (fprintf).\n"); fflush(stderr);
        std::cout << "[RedirectIOToConsole] Console IO redirection attempted (cout)." << std::endl;
        std::cerr << "[RedirectIOToConsole] Test: stderr output after redirection (cerr)." << std::endl;
    } else {
        fprintf(stderr, "[RedirectIOToConsole_EARLY] Failed to attach or allocate console.\n"); fflush(stderr);
        LogToFile("[RedirectIOToConsole] Failed to attach or allocate console.");
    }
}
#endif

#if defined(__APPLE__)
void changeCwdToExecutableDirectory_MacOS() {
    fprintf(stderr, "[CWD_TRICK_MACOS] Entered function.\n"); fflush(stderr);
    char exePath[PATH_MAX];
    uint32_t exePathSize = sizeof(exePath);

    if (_NSGetExecutablePath(exePath, &exePathSize) == 0) {
        fprintf(stderr, "[CWD_TRICK_MACOS] _NSGetExecutablePath success: %s\n", exePath); fflush(stderr);
        char* exePathMutable = strdup(exePath);
        if (exePathMutable) {
            fprintf(stderr, "[CWD_TRICK_MACOS] strdup success.\n"); fflush(stderr);
            char* dir = dirname(exePathMutable);
            if (dir) {
                fprintf(stderr, "[CWD_TRICK_MACOS] dirname success, attempting chdir to: %s\n", dir); fflush(stderr);
                if (chdir(dir) == 0) {
                    fprintf(stderr, "[CWD_TRICK_MACOS] chdir success. New CWD should be: %s\n", dir); fflush(stderr);
                    // Attempt to log to file now that CWD might be set, but still use stderr as primary
                    LogToFile(std::string("[CWD_TRICK_MACOS_LogToFile] chdir success. New CWD: ") + dir);
                } else {
                    perror("[CWD_TRICK_MACOS] chdir failed (perror)"); // prints to stderr with system error message
                    fprintf(stderr, "[CWD_TRICK_MACOS] chdir failed for dir: %s\n", dir); fflush(stderr);
                    LogToFile(std::string("[CWD_TRICK_MACOS_LogToFile] chdir FAILED for dir: ") + dir);
                }
            } else {
                 fprintf(stderr, "[CWD_TRICK_MACOS] dirname failed for path: %s\n", exePathMutable); fflush(stderr);
                 LogToFile(std::string("[CWD_TRICK_MACOS_LogToFile] dirname FAILED for path: ") + exePathMutable);
            }
            free(exePathMutable);
        } else {
            fprintf(stderr, "[CWD_TRICK_MACOS] strdup failed.\n"); fflush(stderr);
            LogToFile("[CWD_TRICK_MACOS_LogToFile] strdup FAILED.");
        }
    } else {
        fprintf(stderr, "[CWD_TRICK_MACOS] _NSGetExecutablePath failed.\n"); fflush(stderr);
        LogToFile("[CWD_TRICK_MACOS_LogToFile] _NSGetExecutablePath FAILED.");
    }
    fprintf(stderr, "[CWD_TRICK_MACOS] Exiting function.\n"); fflush(stderr);
}
#endif


static std::string OpenMcrawDialog() {
    fprintf(stderr, "[OpenMcrawDialog_EARLY] Entered function.\n"); fflush(stderr);
    LogToFile("[OpenMcrawDialog] Called (new cross-platform version).");
#if defined(_WIN32)
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    std::string path;
    if (SUCCEEDED(hr)) {
        IFileOpenDialog* dlg = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, (void**)&dlg))) {
            const COMDLG_FILTERSPEC filter[] = {{ L"MotionCam RAW", L"*.mcraw" }};
            dlg->SetFileTypes(1, filter);
            if (SUCCEEDED(dlg->Show(NULL))) {
                IShellItem* item = nullptr;
                if (SUCCEEDED(dlg->GetResult(&item))) {
                    PWSTR wpath = nullptr;
                    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &wpath))) {
                        int n = WideCharToMultiByte(CP_UTF8, 0, wpath, -1, nullptr, 0, nullptr, nullptr);
                        if (n > 0) {
                            std::vector<char> buf(n);
                            WideCharToMultiByte(CP_UTF8, 0, wpath, -1, buf.data(), n, nullptr, nullptr);
                            path = buf.data();
                        }
                        CoTaskMemFree(wpath);
                    }
                    if(item) item->Release();
                }
            }
            if(dlg) dlg->Release();
        }
        CoUninitialize();
    }
    if (!path.empty()) LogToFile(std::string("[OpenMcrawDialog] File selected (Windows): ") + path);
    else LogToFile("[OpenMcrawDialog] Dialog cancelled or no file selected (Windows).");
    fprintf(stderr, "[OpenMcrawDialog_EARLY] Exiting function (Windows).\n"); fflush(stderr);
    return path;

#elif defined(__APPLE__)
    LogToFile("[OpenMcrawDialog] Using AppleScript for file dialog.");
    const char* cmd = "osascript -e 'set f to choose file of type {\"mcraw\"} with prompt \"Select MotionCam RAW file\"' -e 'POSIX path of f'";
    FILE* pipe = popen(cmd, "r");
    if (!pipe) {
        fprintf(stderr, "[OpenMcrawDialog_EARLY] popen failed for AppleScript\n"); fflush(stderr);
        LogToFile("[OpenMcrawDialog] popen failed for AppleScript.");
        return {};
    }
    char buf[1024];
    std::string path;
    if (fgets(buf, sizeof(buf), pipe)) {
        path = buf;
        if (!path.empty() && path.back() == '\n') path.pop_back();
    }
    int pclose_status = pclose(pipe);
    if (pclose_status == -1) {
        fprintf(stderr, "[OpenMcrawDialog_EARLY] pclose failed for AppleScript\n"); fflush(stderr);
        LogToFile("[OpenMcrawDialog] pclose failed for AppleScript.");
    } else if (WIFEXITED(pclose_status) && WEXITSTATUS(pclose_status) != 0) {
        fprintf(stderr, "[OpenMcrawDialog_EARLY] AppleScript exited with status: %d. Assuming cancelled.\n", WEXITSTATUS(pclose_status)); fflush(stderr);
        LogToFile(std::string("[OpenMcrawDialog] AppleScript exited with status: ") + std::to_string(WEXITSTATUS(pclose_status)) + ". Assuming dialog cancelled.");
        path.clear();
    }

    if (!path.empty()) LogToFile(std::string("[OpenMcrawDialog] File selected (macOS): ") + path);
    else LogToFile("[OpenMcrawDialog] Dialog cancelled or no file selected (macOS).");
    fprintf(stderr, "[OpenMcrawDialog_EARLY] Exiting function (macOS).\n"); fflush(stderr);
    return path;
#else
    LogToFile("[OpenMcrawDialog] File dialog not implemented for this platform.");
    fprintf(stderr, "[OpenMcrawDialog_EARLY] File dialog not implemented. Exiting function.\n"); fflush(stderr);
    return {};
#endif
}

int main(int argc, char* argv[]) {
    fprintf(stderr, "\n[MAIN_VERY_EARLY] Application main() entered. Argc: %d\n", argc);
    if (argc > 0) {
        fprintf(stderr, "[MAIN_VERY_EARLY] argv[0]: %s\n", argv[0]);
    }
    fflush(stderr);

#if defined(__APPLE__)
    fprintf(stderr, "[MAIN_VERY_EARLY] Attempting macOS CWD trick.\n"); fflush(stderr);
    changeCwdToExecutableDirectory_MacOS();
    fprintf(stderr, "[MAIN_VERY_EARLY] After macOS CWD trick attempt.\n"); fflush(stderr);
#endif

    SingleInstanceGuard instanceGuard;
    if (!instanceGuard.acquired) {
        fprintf(stderr, "[main] Another instance is already running. Exiting.\n");
        return 0;
    }

#if defined(_WIN32) && !defined(NDEBUG)
    fprintf(stderr, "[MAIN_VERY_EARLY] Attempting Windows IO redirection.\n"); fflush(stderr);
    RedirectIOToConsole();
    fprintf(stderr, "[MAIN_VERY_EARLY] After Windows IO redirection attempt.\n"); fflush(stderr);
#endif

    fprintf(stderr, "[MAIN_VERY_EARLY] Attempting to log CWD.\n"); fflush(stderr);
    try {
        std::string currentCwdStr = fs::current_path().string();
        fprintf(stderr, "[MAIN_VERY_EARLY] Current CWD (from fs::current_path): %s\n", currentCwdStr.c_str()); fflush(stderr);
        LogToFile(std::string("[main] Current Working Directory (after potential changes): ") + currentCwdStr);
        // std::cout is redirected on Windows, might not be visible on macOS Finder launch immediately
        // std::cout << "[main] Current Working Directory (after potential changes): " << currentCwdStr << std::endl;
    }
    catch (const fs::filesystem_error& e) {
        fprintf(stderr, "[MAIN_VERY_EARLY] Filesystem error getting CWD: %s\n", e.what()); fflush(stderr);
        LogToFile(std::string("[main] Error getting CWD (fs::filesystem_error): ") + e.what());
    }
    catch (const std::exception& e_gen) {
        fprintf(stderr, "[MAIN_VERY_EARLY] Generic exception getting CWD: %s\n", e_gen.what()); fflush(stderr);
        LogToFile(std::string("[main] Error getting CWD (std::exception): ") + e_gen.what());
    }
    fflush(stderr); // Ensure all early stderr is out

    // Initialize LogToFile properly if it has a dedicated init function or relies on CWD
    // For now, we assume it's usable after the CWD trick.
    LogToFile("--------------------------------------------------");
    LogToFile(std::string("[main] main() processing arguments. argc: ") + std::to_string(argc));
    if (argc > 0) LogToFile(std::string("[main] argv[0]: ") + argv[0]);

    std::string inPath;
#if defined(__APPLE__)
    auto openFiles = GetStartupOpenFiles();
    if (!openFiles.empty()) {
        inPath = openFiles.front();
        LogToFile(std::string("[main] File from macOS open event: ") + inPath);
    }
#endif
    if (inPath.empty() && argc >= 2 && strncmp(argv[1], "-psn", 4) != 0) {
        inPath = argv[1];
        LogToFile(std::string("[main] Input file from command line: ") + inPath);
        fprintf(stderr, "[main] Input file from command line: %s\n", inPath.c_str()); fflush(stderr);
    }
    if (!inPath.empty() && (!fs::exists(inPath) || !fs::is_regular_file(inPath))) {
        std::string errorMsg = "[main] Input file not found: " + inPath;
        LogToFile(errorMsg); fprintf(stderr, "%s\n", errorMsg.c_str()); fflush(stderr);
#ifdef _WIN32
        MessageBoxA(NULL, errorMsg.c_str(), "Error", MB_OK | MB_ICONERROR);
#endif
        return 1;
    }
    if (!inPath.empty() && fs::path(inPath).extension() != ".mcraw") {
        std::string errorMsg = "[main] Input must have .mcraw extension: " + inPath;
        LogToFile(errorMsg); fprintf(stderr, "%s\n", errorMsg.c_str()); fflush(stderr);
#ifdef _WIN32
        MessageBoxA(NULL, errorMsg.c_str(), "Error", MB_OK | MB_ICONERROR);
#endif
        return 1;
    }

    LogToFile(std::string("[main] Initializing App with file: ") + inPath);
    if(!inPath.empty()) fprintf(stderr, "[main] Initializing App with file: %s\n", inPath.c_str());
    try {
        App app(inPath);
        LogToFile("[main] App object created. Calling app.run()...");
        fprintf(stderr, "[main] App object created. Calling app.run()...\n"); fflush(stderr);
        if (!app.run()) {
            LogToFile("[main] App::run() returned false. Application will exit.");
            fprintf(stderr, "[main] App::run() returned false. Application will exit.\n"); fflush(stderr);
#ifdef _WIN32
            MessageBoxA(NULL, "Application run failed. See log.", "Runtime Error", MB_OK | MB_ICONERROR);
#endif
            return 1;
        }
        LogToFile("[main] App::run() finished successfully.");
        fprintf(stderr, "[main] App::run() finished successfully.\n"); fflush(stderr);
    }
    catch (const std::exception& e) {
        std::string errorMsg = "[main] FATAL STD EXCEPTION: " + std::string(e.what());
        LogToFile(errorMsg); fprintf(stderr, "%s\n", errorMsg.c_str()); fflush(stderr);
#ifdef _WIN32
        MessageBoxA(NULL, errorMsg.c_str(), "Runtime Error", MB_OK | MB_ICONERROR);
#endif
        return 1;
    }
    catch (...) {
        std::string errorMsg = "[main] FATAL UNKNOWN EXCEPTION occurred.";
        LogToFile(errorMsg); fprintf(stderr, "%s\n", errorMsg.c_str()); fflush(stderr);
#ifdef _WIN32
        MessageBoxA(NULL, errorMsg.c_str(), "Runtime Error", MB_OK | MB_ICONERROR);
#endif
        return 1;
    }

    LogToFile("[main] Application exiting normally.");
    fprintf(stderr, "[main] Application exiting normally.\n"); fflush(stderr);
    return 0;
}