// --- START OF FILE include/GuiOverlay.h ---
#pragma once 

#include <string>
#include <vector>
#include <optional>
#include <cstdint>

struct GLFWwindow;
class App;
#include <vulkan/vulkan.h> 

namespace GuiOverlay {
    extern bool show_playlist_aux;
    void requestContextMenu(float x, float y);

    struct UIData {
        std::string currentFileName;
        size_t currentFrameIndex;
        size_t totalFramesInFile;
        double currentVideoTimeSec;
        double totalDurationSec;
        double capturedFps;
        double actualDisplayFps;
        std::string audioTimestampStr;
        std::string videoTimestampStr;
        std::string avSyncDeltaStr;
        std::optional<int> cfaOverride;
        std::string cfaFromMetadataStr;
        bool isFullscreen;
        bool showMetrics;
        bool showHelpPage;
        bool isPaused;
        bool isZoomedToNative;
    };

    void setup(GLFWwindow* window, App* appInstance);
    void cleanup();
    void beginFrame();
    void render(App* appInstance);
    UIData gatherData(App* appInstance);
    void endFrame(VkCommandBuffer commandBuffer);
}
// --- END OF FILE include/GuiOverlay.h ---