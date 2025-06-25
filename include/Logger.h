// --- START OF FILE Logger.h ---
#ifndef LOGGER_H
#define LOGGER_H

#include <string>

class Logger {
public:
    // Keep the function signature, but the implementation can be empty or removed
    static void log(const std::string& s);
private:
    Logger() = delete;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
};

#endif // LOGGER_H
// --- END OF FILE Logger.h ---