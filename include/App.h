// --- START OF FILE include/App.h ---
#ifndef APP_H
#define APP_H

// 1. Standard C++ Library Headers First
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <cstdint>
#include <chrono>
#include <set>
#include <algorithm>
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>

// 2. Third-party library preprocessor definitions that might affect other headers
#define GLFW_INCLUDE_VULKAN
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE

// 3. Third-party library headers
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <SDL.h>

// 4. Your Project's Headers (Forward declarations first, then includes)
class AudioController;
class DecoderWrapper;
class PlaybackController;
class Renderer_VK;
class App; // Forward declare App for GuiOverlay

#include "GuiOverlay.h"
#include "DebugLog.h"

const int MAX_FRAMES_IN_FLIGHT = 2;

class App {
public:
    explicit App(const std::string& filePath = "");
    ~App();
    bool run();

    friend GuiOverlay::UIData GuiOverlay::gatherData(App* appInstance);
    friend void GuiOverlay::render(App* appInstance);
    friend void GuiOverlay::setup(GLFWwindow* window, App* appInstance);

    GLFWwindow* m_window;
    int m_windowWidth;
    int m_windowHeight;
    int m_storedWindowedPosX;
    int m_storedWindowedPosY;
    int m_storedWindowedWidth;
    int m_storedWindowedHeight;
    bool m_framebufferResized;

    VkInstance m_vkInstance;
    VkDebugUtilsMessengerEXT m_debugMessenger;
    VkSurfaceKHR m_surface;
    VkPhysicalDevice m_physicalDevice;
    VkDevice m_device;
    VkQueue m_graphicsQueue;
    VkQueue m_presentQueue;
    VkSwapchainKHR m_swapChain;
    std::vector<VkImage> m_swapChainImages;
    VkFormat m_swapChainImageFormat;
    VkExtent2D m_swapChainExtent;
    std::vector<VkImageView> m_swapChainImageViews;
    std::vector<VkFramebuffer> m_swapChainFramebuffers;
    VkRenderPass m_renderPass;
    VkCommandPool m_commandPool;
    std::vector<VkCommandBuffer> m_commandBuffers;
    std::vector<VkSemaphore> m_imageAvailableSemaphores;
    std::vector<VkSemaphore> m_renderFinishedSemaphores;
    std::vector<VkFence> m_inFlightFences;
    uint32_t m_currentFrame;
    VkDescriptorPool m_imguiDescriptorPool;

    std::unique_ptr<AudioController> m_audio;
    std::unique_ptr<DecoderWrapper> m_decoderWrapper;
    std::unique_ptr<Renderer_VK> m_rendererVk;
    std::unique_ptr<PlaybackController> m_playbackController;

    std::optional<int> m_cfaOverride;
    std::string m_cfaStringFromMetadata;
    int m_cfaTypeFromMetadata;
    double m_staticBlack;
    double m_staticWhite;
    bool m_containerFlipped;
    int m_containerOrientationTag;
    bool m_dumpMetadata;
    std::vector<std::string> m_fileList;
    int m_currentFileIndex;
    std::string m_filePath;
    std::chrono::steady_clock::time_point m_playbackStartTime;
    std::chrono::steady_clock::time_point m_pauseBegan;
    bool m_showMetrics;
    bool m_showHelpPage;
    double m_decodingTimeMs;
    double m_renderSubmitTimeMs;
    double m_gpuWaitTimeMs;
    double m_sleepTimeMs;
    double m_totalLoopTimeMs;
    bool m_showUI;
    bool m_isPanning;
    double m_lastMouseX;
    double m_lastMouseY;
    bool m_firstFileLoaded;
    bool m_isFullscreen;

    std::string m_lastWindowTitle;
    std::chrono::steady_clock::time_point m_lastTitleUpdate;

    void handleKey(int key, int mods);
    void handleDrop(int count, const char** paths);
    void loadFileAtIndex(int index);
    void softDeleteCurrentFile();
    void anchorPlaybackTimeForResume();
    void recordPauseTime();
    void toggleHelpPage() { m_showHelpPage = !m_showHelpPage; }
    void handleMouseButton(int button, int action, int mods);
    void handleCursorPos(double xpos, double ypos);
    void saveCurrentFrameAsDng();
    void convertCurrentFileToDngs();
    void sendCurrentFileToMotionCamFuse();

    static void framebuffer_size_callback_static(GLFWwindow* window, int w, int h);
    static void key_callback_static(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void drop_callback_static(GLFWwindow* window, int count, const char** paths);
    static void mouse_button_callback_static(GLFWwindow* window, int button, int action, int mods);
    static void cursor_pos_callback_static(GLFWwindow* window, double xpos, double ypos);

    struct QueueFamilyIndices {
        std::optional<uint32_t> graphicsFamily;
        std::optional<uint32_t> presentFamily;
        bool isComplete() {
            return graphicsFamily.has_value() && presentFamily.has_value();
        }
    };
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice physicalDevice);

    VkCommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(VkCommandBuffer commandBuffer);

private:
    bool initVulkan();
    void createInstance();
    void setupDebugMessenger();
    void createSurface();
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createSwapChain();
    void createImageViews();
    void createRenderPass();
    void createFramebuffers();
    void createCommandPool();
    void createCommandBuffers();
    void createSyncObjects();
    void cleanupVulkan();
    void cleanupSwapChain();
    void recreateSwapChain();
    void initImGuiVulkan();
    void drawFrame();
    static std::string openMcrawDialog();
    void triggerOpenFileViaDialog();
    struct SwapChainSupportDetails {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };
    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice queryDevice);
    bool isDeviceSuitable(VkPhysicalDevice queryDevice);
    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);
    std::vector<const char*> getRequiredExtensions();
    bool checkDeviceExtensionSupport(VkPhysicalDevice queryDevice);
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData);
};

#endif // APP_H
