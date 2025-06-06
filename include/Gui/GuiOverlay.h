#ifndef GUI_OVERLAY_H
#define GUI_OVERLAY_H

#include <string>
#include <vector>
#include <optional>
#include <cstdint> // For standard integer types

// Forward declarations
struct GLFWwindow;
class App; // App class is needed for parameters

// Required for ImGui Vulkan backend function signatures
#include <vulkan/vulkan.h>

namespace GuiOverlay {
    // Flag to control the visibility of the auxiliary playlist window.
    extern bool show_playlist_aux;

    // Structure to hold data gathered from the App for UI display.
    struct UIData {
        std::string currentFileName;
        size_t currentFrameIndex = 0;
        size_t totalFramesInFile = 0;
        double currentVideoTimeSec = 0.0;
        double totalDurationSec = 0.0;
        double capturedFps = 0.0;
        double actualDisplayFps = 0.0;
        std::string audioTimestampStr;
        std::string videoTimestampStr;
        std::string avSyncDeltaStr;
        std::optional<int> cfaOverride;
        std::string cfaFromMetadataStr;
        bool isFullscreen = false;
        bool showMetrics = false;
        bool showHelpPage = false;
        bool isPaused = true;
        bool isZoomedToNative = false;

        // New/Updated fields
        int decodedWidth = 0;
        int decodedHeight = 0;

        double totalLoopTimeMs = 0.0;
        double gpuWaitTimeMs = 0.0;
        double decodeTimeMs = 0.0;
        double renderPrepTimeMs = 0.0;
        double guiRenderTimeMs = 0.0;
        double vkSubmitPresentTimeMs = 0.0;
        double appLogicTimeMs = 0.0;
        double sleepTimeMs = 0.0;
    };

    // Sets up ImGui for use with GLFW and Vulkan.
    void setup(GLFWwindow* window, App* appInstance);

    // Shuts down ImGui and cleans up its resources.
    void cleanup();

    // Starts a new ImGui frame.
    void beginFrame();

    // Gathers necessary data from the App instance to populate the UI.
    UIData gatherData(App* appInstance);

    // Defines and renders the ImGui interface.
    void render(App* appInstance);

    // Ends the ImGui frame and records its draw data into the Vulkan command buffer.
    void endFrame(VkCommandBuffer commandBuffer);

} // namespace GuiOverlay

#endif // GUI_OVERLAY_H