#include "Utils/StringUtils.h"

#ifdef _WIN32
// windows.h is already included via StringUtils.h when _WIN32 is defined
namespace DebugLogHelper {
    std::string wstring_to_utf8(const std::wstring& wstr) {
        if (wstr.empty()) return std::string();
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.length(), NULL, 0, NULL, NULL);
        if (size_needed <= 0) return std::string(); // Error or empty string
        std::string strTo(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.length(), &strTo[0], size_needed, NULL, NULL);
        return strTo;
    }
}
#else
// Provide a stub or platform-specific version for non-Windows if ever needed by other code
namespace DebugLogHelper {
    std::string wstring_to_utf8(const std::wstring& wstr) {
        // This function is primarily for Windows GetOpenFileNameW.
        // On other platforms, paths are typically UTF-8 char strings.
        // If wstring is used elsewhere on non-Windows, a proper conversion would be needed.
        return {}; // Placeholder
    }
}
#endif