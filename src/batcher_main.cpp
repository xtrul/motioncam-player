#include <iostream>
#include <filesystem>
#include "App/App.h"
#include "Utils/DebugLog.h"

extern void determineAppBasePath(const char* argv0);

int main(int argc, char* argv[]) {
    determineAppBasePath(argc > 0 ? argv[0] : "");
    LogProRes("[Startup] ProRes log initialized");
    LogFFmpegStatus();
    std::string inPath;
    if (argc >= 2) inPath = argv[1];
    App app(inPath);
    return app.runBatcher() ? 0 : 1;
}
