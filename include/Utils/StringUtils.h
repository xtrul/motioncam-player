#ifndef STRING_UTILS_H
#define STRING_UTILS_H

#include <string>
#ifdef _WIN32
#include <windows.h> // For WCHAR, MultiByteToWideChar etc.
#endif

namespace DebugLogHelper { // Keep the namespace for now
    std::string wstring_to_utf8(const std::wstring& wstr);
    // Add other string utilities here if needed in the future
}

#endif // STRING_UTILS_H