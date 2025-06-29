// --- START OF FILE src/App.cpp ---

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define GLFW_EXPOSE_NATIVE_WIN32
#include <windows.h>
#include <shlwapi.h>
#include <dwmapi.h>
#include <commdlg.h>
#include <shobjidl.h>
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "comdlg32.lib")
#endif

#include "App.h"

#ifdef _WIN32
#include <GLFW/glfw3native.h>
#elif defined(__APPLE__)
#include <sys/wait.h>
#endif

#include "AudioController.h"
#include "DecoderWrapper.h"
#include "PlaybackController.h"
#include "Renderer_VK.h"
#include "DebugLog.h"
// GuiOverlay.h is included via App.h
#include <imgui_impl_vulkan.h> // For ImGui_ImplVulkan_Shutdown

#if defined(__APPLE__)
#include "MacOpenFile.h"
#endif

#include <imgui.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <algorithm>
#include <iomanip>
#include <thread>
#include <chrono>
#include <cmath>
#include <set>

#define TINY_DNG_WRITER_IMPLEMENTATION
#include <tinydng/tiny_dng_writer.h>

namespace fs = std::filesystem;

namespace {
    int computeOrientationTag(const nlohmann::json& j, bool flipped, int defTag) {
        if (!j.is_null()) {
            if (j.is_number_integer()) {
                int v = j.get<int>();
                if (v >= 1 && v <= 8) return v;
                switch (v) {
                    case 0: return flipped ? tinydngwriter::ORIENTATION_LEFTTOP : tinydngwriter::ORIENTATION_RIGHTTOP; // PORTRAIT
                    case 1: return flipped ? tinydngwriter::ORIENTATION_RIGHTBOT : tinydngwriter::ORIENTATION_LEFTBOT; // REVERSE_PORTRAIT
                    case 2: return flipped ? tinydngwriter::ORIENTATION_BOTLEFT : tinydngwriter::ORIENTATION_BOTRIGHT; // REVERSE_LANDSCAPE
                    case 3: return flipped ? tinydngwriter::ORIENTATION_TOPRIGHT : tinydngwriter::ORIENTATION_TOPLEFT; // LANDSCAPE
                }
            } else if (j.is_string()) {
                std::string o = j.get<std::string>();
                std::transform(o.begin(), o.end(), o.begin(), ::toupper);
                if (o == "PORTRAIT") return flipped ? tinydngwriter::ORIENTATION_LEFTTOP : tinydngwriter::ORIENTATION_RIGHTTOP;
                if (o == "REVERSE_PORTRAIT") return flipped ? tinydngwriter::ORIENTATION_RIGHTBOT : tinydngwriter::ORIENTATION_LEFTBOT;
                if (o == "REVERSE_LANDSCAPE") return flipped ? tinydngwriter::ORIENTATION_BOTLEFT : tinydngwriter::ORIENTATION_BOTRIGHT;
                if (o == "LANDSCAPE") return flipped ? tinydngwriter::ORIENTATION_TOPRIGHT : tinydngwriter::ORIENTATION_TOPLEFT;
            }
        }
        return defTag;
    }

    bool writeDngInternal(
        const std::string& outputPath,
        const std::vector<uint16_t>& data,
        const nlohmann::json& frameMetadata,
        const nlohmann::json& containerMetadata,
        std::string& errorMsg)
    {
        const unsigned int width = frameMetadata.value("width", 0);
        const unsigned int height = frameMetadata.value("height", 0);

        if (width == 0 || height == 0) {
            errorMsg = "Invalid frame dimensions (width or height is zero).";
            return false;
        }
        if (data.size() < static_cast<size_t>(width) * height) {
            errorMsg = "Insufficient image data for given dimensions.";
            return false;
        }
        std::vector<double> asShotNeutralDouble = frameMetadata.value("asShotNeutral", std::vector<double>{1.0, 1.0, 1.0});
        std::vector<float> asShotNeutral;
        asShotNeutral.reserve(asShotNeutralDouble.size());
        for (double val : asShotNeutralDouble) {
            asShotNeutral.push_back(static_cast<float>(val));
        }

        std::vector<double> blackLevelDouble = containerMetadata.value("blackLevel", std::vector<double>{0.0, 0.0, 0.0, 0.0});
        if (blackLevelDouble.empty()) {
            blackLevelDouble = { 0.0, 0.0, 0.0, 0.0 };
        }
        else if (blackLevelDouble.size() == 1) {
            double val = blackLevelDouble[0];
            blackLevelDouble = { val, val, val, val };
        }
        else if (blackLevelDouble.size() != 4) {
            std::cerr << "Warning: Unexpected number of black levels (" << blackLevelDouble.size() << ") in metadata. Adjusting to 4." << std::endl;
            double fill_val = blackLevelDouble.empty() ? 0.0 : blackLevelDouble[0];
            blackLevelDouble.resize(4, fill_val);
        }
        std::vector<uint16_t> blackLevelUint16;
        blackLevelUint16.reserve(blackLevelDouble.size());
        for (double d_val : blackLevelDouble) {
            uint16_t u_val = 0;
            if (d_val < 0.0) u_val = 0;
            else if (d_val > 65535.0) u_val = 65535;
            else u_val = static_cast<uint16_t>(std::round(d_val));
            blackLevelUint16.push_back(u_val);
        }
        double whiteLevel = containerMetadata.value("whiteLevel", 65535.0);
        std::string sensorArrangement = containerMetadata.value("sensorArrangement",
            containerMetadata.value("sensorArrangment", "BGGR"));
        nlohmann::json ccm1_json = containerMetadata.value("ColorMatrix", containerMetadata.value("colorMatrix1", nlohmann::json::array({ 1,0,0,0,1,0,0,0,1 })));
        nlohmann::json ccm2_json = containerMetadata.value("ColorMatrix2", containerMetadata.value("colorMatrix2", nlohmann::json::array({ 1,0,0,0,1,0,0,0,1 })));
        std::vector<float> colorMatrix1 = { 1,0,0, 0,1,0, 0,0,1 };
        std::vector<float> colorMatrix2 = { 1,0,0, 0,1,0, 0,0,1 };
        if (ccm1_json.is_array() && ccm1_json.size() == 9) {
            for (size_t i = 0; i < 9; ++i) colorMatrix1[i] = ccm1_json[i].get<float>();
        }
        if (ccm2_json.is_array() && ccm2_json.size() == 9) {
            for (size_t i = 0; i < 9; ++i) colorMatrix2[i] = ccm2_json[i].get<float>();
        }
        nlohmann::json fwd1_json = containerMetadata.value("ForwardMatrix1", containerMetadata.value("forwardMatrix1", nlohmann::json::array({ 1,0,0,0,1,0,0,0,1 })));
        nlohmann::json fwd2_json = containerMetadata.value("ForwardMatrix2", containerMetadata.value("forwardMatrix2", nlohmann::json::array({ 1,0,0,0,1,0,0,0,1 })));
        std::vector<float> forwardMatrix1 = { 1,0,0, 0,1,0, 0,0,1 };
        std::vector<float> forwardMatrix2 = { 1,0,0, 0,1,0, 0,0,1 };
        if (fwd1_json.is_array() && fwd1_json.size() == 9) {
            for (size_t i = 0; i < 9; ++i) forwardMatrix1[i] = fwd1_json[i].get<float>();
        }
        if (fwd2_json.is_array() && fwd2_json.size() == 9) {
            for (size_t i = 0; i < 9; ++i) forwardMatrix2[i] = fwd2_json[i].get<float>();
        }

        bool isFlipped = false;
        if (containerMetadata.contains("extraData") &&
            containerMetadata["extraData"].contains("postProcessSettings") &&
            containerMetadata["extraData"]["postProcessSettings"].contains("flipped")) {
            isFlipped = containerMetadata["extraData"]["postProcessSettings"]["flipped"].get<bool>();
        }

        int containerTag = computeOrientationTag(
            containerMetadata.contains("orientation") ? containerMetadata["orientation"] : nlohmann::json(),
            isFlipped,
            tinydngwriter::ORIENTATION_TOPLEFT);
        unsigned short dngOrientation = static_cast<unsigned short>(computeOrientationTag(
            frameMetadata.contains("orientation") ? frameMetadata["orientation"] : nlohmann::json(),
            isFlipped,
            containerTag));
        tinydngwriter::DNGImage dng;
        dng.SetBigEndian(false);
        dng.SetDNGVersion(1, 4, 0, 0);
        dng.SetDNGBackwardVersion(1, 1, 0, 0);
        dng.SetOrientation(dngOrientation);
        dng.SetImageData(reinterpret_cast<const unsigned char*>(data.data()), static_cast<size_t>(width) * height * sizeof(uint16_t));
        dng.SetImageWidth(width);
        dng.SetImageLength(height);
        dng.SetPlanarConfig(tinydngwriter::PLANARCONFIG_CONTIG);
        dng.SetPhotometric(tinydngwriter::PHOTOMETRIC_CFA);
        dng.SetRowsPerStrip(height);
        dng.SetSamplesPerPixel(1);
        dng.SetCFARepeatPatternDim(2, 2);
        dng.SetBlackLevelRepeatDim(2, 2);
        dng.SetBlackLevel(blackLevelUint16.size(), blackLevelUint16.data());
        dng.SetWhiteLevel(static_cast<float>(whiteLevel));
        dng.SetCompression(tinydngwriter::COMPRESSION_NONE);
        std::vector<unsigned char> cfa_pattern_values;
        std::string cfa_upper = sensorArrangement;
        std::transform(cfa_upper.begin(), cfa_upper.end(), cfa_upper.begin(), ::toupper);
        if (cfa_upper == "RGGB")        cfa_pattern_values = { 0, 1, 1, 2 };
        else if (cfa_upper == "BGGR")   cfa_pattern_values = { 2, 1, 1, 0 };
        else if (cfa_upper == "GRBG")   cfa_pattern_values = { 1, 0, 2, 1 };
        else if (cfa_upper == "GBRG")   cfa_pattern_values = { 1, 2, 0, 1 };
        else {
            errorMsg = "Invalid or unsupported sensor arrangement: " + sensorArrangement;
            return false;
        }
        dng.SetCFAPattern(cfa_pattern_values.size(), cfa_pattern_values.data());
        dng.SetCFALayout(1);
        const uint16_t bps[1] = { 16 };
        dng.SetBitsPerSample(1, bps);
        dng.SetColorMatrix1(3, colorMatrix1.data());
        dng.SetColorMatrix2(3, colorMatrix2.data());
        dng.SetForwardMatrix1(3, forwardMatrix1.data());
        dng.SetForwardMatrix2(3, forwardMatrix2.data());
        dng.SetAsShotNeutral(asShotNeutral.size(), asShotNeutral.data());
        dng.SetCalibrationIlluminant1(21);
        dng.SetCalibrationIlluminant2(17);
        dng.SetUniqueCameraModel("MotionCam");
        dng.SetSubfileType(false, false, false);
        const uint32_t activeArea[4] = { 0, 0, height, width };
        dng.SetActiveArea(activeArea);
        tinydngwriter::DNGWriter writer(false);
        writer.AddImage(&dng);
        if (!writer.WriteToFile(outputPath.c_str(), &errorMsg)) {
            return false;
        }
        return true;
    }
}

const std::vector<const char*> validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};
const std::vector<const char*> deviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
    #ifdef __APPLE__
    , "VK_KHR_portability_subset"
    #endif
};

#ifndef NDEBUG
const bool enableValidationLayers = true;
#else
const bool enableValidationLayers = false;
#endif

#define VK_APP_CHECK(x)                                                 \
    do {                                                                \
        VkResult err = x;                                               \
        if (err) {                                                      \
            std::string error_str = std::string("[VULKAN CHECK FAILED] Error: ") + std::to_string(err) + " (" #x ") at " __FILE__ ":" + std::to_string(__LINE__); \
            LogToFile(error_str);                                       \
            std::cerr << error_str << std::endl;                        \
            throw std::runtime_error("Vulkan API call failed!");        \
        }                                                               \
    } while (0)

static void glfw_error_callback(int error, const char* description) {
    std::string msg = "[GLFW Error] " + std::to_string(error) + ": " + description;
    LogToFile(msg);
    std::cerr << msg << std::endl;
}

VKAPI_ATTR VkBool32 VKAPI_CALL App::debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {
    std::string severity_str;
    if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) severity_str += "VERBOSE ";
    if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)    severity_str += "INFO ";
    if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) severity_str += "WARNING ";
    if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)   severity_str += "ERROR ";

    std::string type_str;
    if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT)     type_str += "GENERAL ";
    if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)  type_str += "VALIDATION ";
    if (messageType & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) type_str += "PERFORMANCE ";

    std::string log_msg = "[ValidationLayer] " + severity_str + type_str + "- " + pCallbackData->pMessage;
    LogToFile(log_msg);
    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        std::cerr << log_msg << std::endl;
    }
    return VK_FALSE;
}

App::App(const std::string& filePath) : m_filePath(filePath) {
    m_window = nullptr;
    m_windowWidth = 1280; m_windowHeight = 720;
    m_storedWindowedPosX = 100; m_storedWindowedPosY = 100;
    m_storedWindowedWidth = 1280; m_storedWindowedHeight = 720;
    m_framebufferResized = false;
    m_vkInstance = VK_NULL_HANDLE; m_debugMessenger = VK_NULL_HANDLE; m_surface = VK_NULL_HANDLE;
    m_physicalDevice = VK_NULL_HANDLE; m_device = VK_NULL_HANDLE;
    m_graphicsQueue = VK_NULL_HANDLE; m_presentQueue = VK_NULL_HANDLE;
    m_swapChain = VK_NULL_HANDLE; m_swapChainImageFormat = VK_FORMAT_UNDEFINED;
    m_renderPass = VK_NULL_HANDLE; m_commandPool = VK_NULL_HANDLE;
    m_currentFrame = 0; m_imguiDescriptorPool = VK_NULL_HANDLE;
    m_isFullscreen = false; m_cfaTypeFromMetadata = 0; m_staticBlack = 0.0; m_staticWhite = 65535.0; m_containerFlipped = false; m_containerOrientationTag = tinydngwriter::ORIENTATION_TOPLEFT;
    m_dumpMetadata = false; m_currentFileIndex = 0; m_showMetrics = false; m_showHelpPage = false;
    m_decodingTimeMs = 0.0; m_renderSubmitTimeMs = 0.0; m_gpuWaitTimeMs = 0.0;
    m_sleepTimeMs = 0.0; m_totalLoopTimeMs = 0.0; m_showUI = true;
    m_isPanning = false; m_lastMouseX = 0.0; m_lastMouseY = 0.0;
    m_firstFileLoaded = false;
    m_lastWindowTitle = "";
    m_lastTitleUpdate = std::chrono::steady_clock::now();

    LogToFile(std::string("[App::App] Constructor called for file: ") + this->m_filePath);
    std::cout << "[App::App] Constructor called for file: " << this->m_filePath << std::endl;
    if (!m_filePath.empty()) {
        if (!fs::exists(this->m_filePath)) {
            LogToFile(std::string("[App::App] ERROR: File does not exist: ") + this->m_filePath);
            throw std::runtime_error("[App::App] File does not exist: " + this->m_filePath);
        }
        auto target = fs::absolute(this->m_filePath);
        auto folder = target.parent_path();
        for (const auto& e : fs::directory_iterator(folder)) {
            if (e.is_regular_file() && e.path().extension() == ".mcraw") {
                m_fileList.push_back(e.path().string());
            }
        }
        std::sort(m_fileList.begin(), m_fileList.end());
        auto it = std::find(m_fileList.begin(), m_fileList.end(), target.string());
        if (it == m_fileList.end()) {
            m_fileList.push_back(target.string());
            std::sort(m_fileList.begin(), m_fileList.end());
            it = std::find(m_fileList.begin(), m_fileList.end(), target.string());
            if (it == m_fileList.end()) {
                LogToFile(std::string("[App::App] ERROR: Catastrophic: Initial file not in playlist: ") + this->m_filePath);
                throw std::runtime_error("[App::App] Catastrophic: Initial file not in playlist: " + this->m_filePath);
            }
        }
        m_currentFileIndex = static_cast<int>(std::distance(m_fileList.begin(), it));
    }
    LogToFile(std::string("[App::App] Constructor finished. Current file index: ") + std::to_string(m_currentFileIndex));
    std::cout << "[App::App] Constructor finished. Current file index: " << m_currentFileIndex << std::endl;
}

App::~App() {
    LogToFile("[App::~App] Destructor called.");
    std::cout << "[App::~App] Destructor called." << std::endl;
    cleanupVulkan();

    if (m_audio) { LogToFile("[App::~App] Shutting down audio."); std::cout << "[App::~App] Shutting down audio." << std::endl; m_audio->shutdown(); } m_audio.reset();
    m_decoderWrapper.reset();
    m_playbackController.reset();

    if (m_window) {
        LogToFile("[App::~App] Destroying GLFW window.");
        std::cout << "[App::~App] Destroying GLFW window." << std::endl;
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }
    LogToFile("[App::~App] Terminating GLFW.");
    std::cout << "[App::~App] Terminating GLFW." << std::endl;
    glfwTerminate();
    LogToFile("[App::~App] Destructor finished.");
    std::cout << "[App::~App] Destructor finished." << std::endl;
}

void App::framebuffer_size_callback_static(GLFWwindow* window, int width, int height) {
    auto app = static_cast<App*>(glfwGetWindowUserPointer(window));
    std::string msg = "[GLFW Callback] Framebuffer resized to " + std::to_string(width) + "x" + std::to_string(height);
    LogToFile(msg);
    std::cout << msg << std::endl;
    app->m_framebufferResized = true;
}
void App::key_callback_static(GLFWwindow* window, int key, int scancode, int action, int mods) {
    auto app = static_cast<App*>(glfwGetWindowUserPointer(window));
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        if (app) {
            if (key == GLFW_KEY_O && (mods & GLFW_MOD_CONTROL)) { app->triggerOpenFileViaDialog(); return; }
            if (key == GLFW_KEY_Q && (mods & GLFW_MOD_CONTROL)) { glfwSetWindowShouldClose(window, GLFW_TRUE); return; }
        }
    }
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureKeyboard && key != GLFW_KEY_TAB) { return; }
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        if (app) {
            app->handleKey(key, mods);
        }
    }
}
void App::drop_callback_static(GLFWwindow* window, int count, const char** paths) {
    if (auto app = static_cast<App*>(glfwGetWindowUserPointer(window)))
        app->handleDrop(count, paths);
}
void App::mouse_button_callback_static(GLFWwindow* window, int button, int action, int mods) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) { return; }
    if (auto app = static_cast<App*>(glfwGetWindowUserPointer(window))) {
        app->handleMouseButton(button, action, mods);
    }
}
void App::cursor_pos_callback_static(GLFWwindow* window, double xpos, double ypos) {
    if (auto app = static_cast<App*>(glfwGetWindowUserPointer(window))) {
        app->handleCursorPos(xpos, ypos);
    }
}

VkCommandBuffer App::beginSingleTimeCommands() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    VK_APP_CHECK(vkAllocateCommandBuffers(m_device, &allocInfo, &commandBuffer));

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_APP_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));
    return commandBuffer;
}

void App::endSingleTimeCommands(VkCommandBuffer commandBuffer) {
    VK_APP_CHECK(vkEndCommandBuffer(commandBuffer));

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    VK_APP_CHECK(vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE));
    VK_APP_CHECK(vkQueueWaitIdle(m_graphicsQueue));

    vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
}


bool App::run() {
    LogToFile("[App::run] App::run() called.");
    std::cout << "[App::run] App::run() called." << std::endl;
    m_audio = std::make_unique<AudioController>();
    LogToFile("[App::run] AudioController created. Initializing audio...");
    std::cout << "[App::run] AudioController created. Initializing audio..." << std::endl;
    if (!m_audio->init()) {
        LogToFile("[App::run] ERROR: Failed to initialize audio!");
        std::cerr << "[App::run] Failed to initialize audio!" << std::endl;
    } else {
        LogToFile("[App::run] Audio initialized.");
        std::cout << "[App::run] Audio initialized." << std::endl;
    }

    LogToFile("[App::run] Initializing Vulkan...");
    std::cout << "[App::run] Initializing Vulkan..." << std::endl;
    if (!initVulkan()) {
        LogToFile("[App::run] ERROR: initVulkan() failed. Exiting App::run().");
        std::cerr << "[App::run] initVulkan() failed. Exiting App::run()." << std::endl;
        return false;
    }
    LogToFile("[App::run] Vulkan initialized.");
    std::cout << "[App::run] Vulkan initialized." << std::endl;

    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_1;
    allocatorInfo.physicalDevice = m_physicalDevice;
    allocatorInfo.device = m_device;
    allocatorInfo.instance = m_vkInstance;
    VmaVulkanFunctions vulkanFunctions = {};
    vulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vulkanFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    allocatorInfo.pVulkanFunctions = &vulkanFunctions;
    VmaAllocator vmaAllocator;
    LogToFile("[App::run] Creating VMA Allocator...");
    std::cout << "[App::run] Creating VMA Allocator..." << std::endl;
    VK_APP_CHECK(vmaCreateAllocator(&allocatorInfo, &vmaAllocator));
    LogToFile("[App::run] VMA Allocator created.");
    std::cout << "[App::run] VMA Allocator created." << std::endl;

    LogToFile("[App::run] Creating Renderer_VK...");
    std::cout << "[App::run] Creating Renderer_VK..." << std::endl;
    m_rendererVk = std::make_unique<Renderer_VK>(m_physicalDevice, m_device, vmaAllocator, m_graphicsQueue, m_commandPool);
    if (!m_rendererVk->init(m_renderPass, static_cast<uint32_t>(m_swapChainImages.size()))) {
        LogToFile("[App::run] ERROR: Failed to initialize Renderer_VK. Exiting App::run().");
        std::cerr << "[App::run] Failed to initialize Renderer_VK. Exiting App::run()." << std::endl;
        vmaDestroyAllocator(vmaAllocator);
        return false;
    }
    LogToFile("[App::run] Renderer_VK initialized.");
    std::cout << "[App::run] Renderer_VK initialized." << std::endl;

    LogToFile("[App::run] Initializing ImGui Vulkan...");
    std::cout << "[App::run] Initializing ImGui Vulkan..." << std::endl;
    initImGuiVulkan();
    LogToFile("[App::run] ImGui Vulkan initialized.");
    std::cout << "[App::run] ImGui Vulkan initialized." << std::endl;

    if (!m_fileList.empty()) {
        LogToFile("[App::run] Loading initial file...");
        std::cout << "[App::run] Loading initial file..." << std::endl;
        loadFileAtIndex(m_currentFileIndex);
        m_firstFileLoaded = true;
        LogToFile("[App::run] Initial file loaded.");
        std::cout << "[App::run] Initial file loaded." << std::endl;
        if (!m_decoderWrapper || !m_playbackController) {
            LogToFile("[App::run] ERROR: Critical component missing after initial load. Exiting App::run().");
            std::cerr << "[App::run] Critical component missing after initial load. Exiting App::run()." << std::endl;
            if(m_rendererVk) m_rendererVk->cleanup();
            vmaDestroyAllocator(vmaAllocator);
            return false;
        }
    }

    LogToFile("[App::run] Entering main loop...");
    std::cout << "[App::run] Entering main loop..." << std::endl;
    using namespace std::chrono_literals;
    using steady_clock = std::chrono::steady_clock;

    while (!glfwWindowShouldClose(m_window)) {
        glfwPollEvents();

#if defined(__APPLE__)
        auto newlyOpened = GetPendingOpenFiles();
        for (const std::string& p : newlyOpened) {
            const char* paths[1] = { p.c_str() };
            handleDrop(1, paths);
        }
#endif

        bool paused = true;
        bool segment_looped_or_ended = false;
        if (m_playbackController) {
            paused = m_playbackController->isPaused();
            if (!paused) {
                if (m_decoderWrapper && m_decoderWrapper->getDecoder()) {
                    const auto& frames = m_decoderWrapper->getDecoder()->getFrames();
                    segment_looped_or_ended = m_playbackController->updatePlayhead(steady_clock::now(), frames);
                } else {
                    m_playbackController->updatePlayhead(steady_clock::now(), {});
                }
            } else {
                if (m_decoderWrapper && m_decoderWrapper->getDecoder()) {
                    m_playbackController->updatePlayhead(steady_clock::now(), m_decoderWrapper->getDecoder()->getFrames());
                } else {
                    m_playbackController->updatePlayhead(steady_clock::now(), {});
                }
            }
        }
        m_sleepTimeMs = 0.0;
        if ((!m_decoderWrapper || !m_decoderWrapper->getDecoder()) && !m_fileList.empty()) {
            loadFileAtIndex(m_currentFileIndex);
            if (!m_decoderWrapper || !m_decoderWrapper->getDecoder()) {
                 LogToFile("[App::run] ERROR: Decoder became invalid in main loop. Breaking.");
                 std::cerr << "[App::run] Decoder became invalid in main loop. Breaking." << std::endl;
                 break;
            }
        }

        auto t0_render_submit = steady_clock::now();
        drawFrame();
        auto t1_render_submit = steady_clock::now();
        m_renderSubmitTimeMs = std::chrono::duration<double, std::milli>(t1_render_submit - t0_render_submit).count();

        if (m_audio && m_playbackController && !paused && m_playbackController->getFirstFrameMediaTimestampOfSegment()) {
            auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(steady_clock::now() - m_playbackStartTime).count();
            m_audio->updatePlayback(elapsed);
        }

        if (m_playbackController && !paused && segment_looped_or_ended) {
            if (m_fileList.size() > 1) {
                loadFileAtIndex((m_currentFileIndex + 1) % (int)m_fileList.size());
            } else {
                m_playbackStartTime = steady_clock::now();
                if (m_decoderWrapper->getDecoder()){
                    const auto& frames = m_decoderWrapper->getDecoder()->getFrames();
                    if (!frames.empty()) {
                        nlohmann::json meta; std::vector<uint16_t> dummy;
                        try { m_decoderWrapper->getDecoder()->loadFrame(frames.front(), dummy, meta); }
                        catch (const std::exception& ) { meta["timestamp"] = std::to_string(frames.front()); }
                        m_playbackController->processNewSegment(meta, frames.size(), m_playbackStartTime);
                    }
                }
                if (m_audio && m_decoderWrapper->getDecoder()) {
                    auto& loader = m_decoderWrapper->getDecoder()->loadAudio();
                    const auto& frames = m_decoderWrapper->getDecoder()->getFrames();
                    m_audio->reset(&loader, frames.empty() ? 0 : frames.front());
                    m_audio->setForceMute(false);
                }
            }
        }
        if (paused) { std::this_thread::sleep_for(16ms); m_sleepTimeMs = 16.0;}
        m_totalLoopTimeMs = m_decodingTimeMs + m_renderSubmitTimeMs + m_gpuWaitTimeMs + m_sleepTimeMs;

        {
            std::ostringstream ss; ss << "MotionCam Player - ";
            if (m_currentFileIndex >= 0 && static_cast<size_t>(m_currentFileIndex) < m_fileList.size()) {
                ss << fs::path(m_fileList[m_currentFileIndex]).filename().string();
                if (m_playbackController && m_decoderWrapper && m_decoderWrapper->getDecoder()) {
                    size_t cur = m_playbackController->getCurrentFrameIndex() + 1;
                    size_t tot = m_decoderWrapper->getDecoder()->getFrames().size();
                    if (tot > 0) {
                        size_t width = std::max<size_t>(6, std::to_string(tot).size());
                        ss << " (" << std::setfill('0') << std::setw(static_cast<int>(width)) << cur
                           << "/" << std::setfill('0') << std::setw(static_cast<int>(width)) << tot << ")";
                    } else {
                        ss << " (0 frames)";
                    }
                }
            } else { ss << "(no file)"; }

            auto newTitle = ss.str();
            auto now = steady_clock::now();
            if (newTitle != m_lastWindowTitle &&
                std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastTitleUpdate).count() > 200) {
                glfwSetWindowTitle(m_window, newTitle.c_str());
                m_lastWindowTitle = std::move(newTitle);
                m_lastTitleUpdate = now;
            }
        }
    }
    LogToFile("[App::run] Exited main loop.");
    std::cout << "[App::run] Exited main loop." << std::endl;

    if (m_device != VK_NULL_HANDLE) {
        LogToFile("[App::run] Waiting for device idle before final cleanup...");
        std::cout << "[App::run] Waiting for device idle before final cleanup..." << std::endl;
        vkDeviceWaitIdle(m_device);
        LogToFile("[App::run] Device idle.");
        std::cout << "[App::run] Device idle." << std::endl;
    }

    if (m_rendererVk) { LogToFile("[App::run] Cleaning up Renderer_VK."); std::cout << "[App::run] Cleaning up Renderer_VK." << std::endl; m_rendererVk->cleanup(); }
    LogToFile("[App::run] Destroying VMA Allocator.");
    std::cout << "[App::run] Destroying VMA Allocator." << std::endl;
    vmaDestroyAllocator(vmaAllocator);
    LogToFile("[App::run] App::run() returning true.");
    std::cout << "[App::run] App::run() returning true." << std::endl;
    return true;
}

bool App::initVulkan() {
    LogToFile("[App::initVulkan] Starting Vulkan initialization.");
    std::cout << "[App::initVulkan] Starting Vulkan initialization." << std::endl;
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        LogToFile("[App::initVulkan] ERROR: Failed to initialize GLFW");
        std::cerr << "[App::initVulkan] Failed to initialize GLFW" << std::endl;
        return false;
    }
    LogToFile("[App::initVulkan] GLFW initialized.");
    std::cout << "[App::initVulkan] GLFW initialized." << std::endl;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    m_window = glfwCreateWindow(m_windowWidth, m_windowHeight, "MotionCam Player", nullptr, nullptr);
    if (!m_window) {
        LogToFile("[App::initVulkan] ERROR: Failed to create GLFW window");
        std::cerr << "[App::initVulkan] Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return false;
    }
    LogToFile("[App::initVulkan] GLFW window created.");
    std::cout << "[App::initVulkan] GLFW window created." << std::endl;
    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, framebuffer_size_callback_static);
    glfwSetKeyCallback(m_window, key_callback_static);
    glfwSetDropCallback(m_window, drop_callback_static);
    glfwSetMouseButtonCallback(m_window, mouse_button_callback_static);
    glfwSetCursorPosCallback(m_window, cursor_pos_callback_static);

#ifdef _WIN32
    {
        HWND hwnd = glfwGetWin32Window(m_window);
        BOOL useDark = TRUE; DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(useDark));
        COLORREF barCol = RGB(15,15,18); DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &barCol, sizeof(barCol));
        COLORREF textCol = RGB(230,230,230); DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR, &textCol, sizeof(textCol));
        HICON hIcon = (HICON)LoadImageW(GetModuleHandleW(NULL), L"IDI_APPICON", IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
        if(hIcon) SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
        HICON hIconSmall = (HICON)LoadImageW(GetModuleHandleW(NULL), L"IDI_APPICON", IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);
        if(hIconSmall) SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSmall);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }
#endif

    try {
        LogToFile("[App::initVulkan] Creating Vulkan instance..."); std::cout << "[App::initVulkan] Creating Vulkan instance..." << std::endl; createInstance(); LogToFile("[App::initVulkan] Instance created."); std::cout << "[App::initVulkan] Instance created." << std::endl;
        LogToFile("[App::initVulkan] Setting up debug messenger..."); std::cout << "[App::initVulkan] Setting up debug messenger..." << std::endl; setupDebugMessenger(); LogToFile("[App::initVulkan] Debug messenger set up."); std::cout << "[App::initVulkan] Debug messenger set up." << std::endl;
        LogToFile("[App::initVulkan] Creating surface..."); std::cout << "[App::initVulkan] Creating surface..." << std::endl; createSurface(); LogToFile("[App::initVulkan] Surface created."); std::cout << "[App::initVulkan] Surface created." << std::endl;
        LogToFile("[App::initVulkan] Picking physical device..."); std::cout << "[App::initVulkan] Picking physical device..." << std::endl; pickPhysicalDevice(); LogToFile("[App::initVulkan] Physical device picked."); std::cout << "[App::initVulkan] Physical device picked." << std::endl;
        LogToFile("[App::initVulkan] Creating logical device..."); std::cout << "[App::initVulkan] Creating logical device..." << std::endl; createLogicalDevice(); LogToFile("[App::initVulkan] Logical device created."); std::cout << "[App::initVulkan] Logical device created." << std::endl;
        LogToFile("[App::initVulkan] Creating swapchain..."); std::cout << "[App::initVulkan] Creating swapchain..." << std::endl; createSwapChain(); LogToFile("[App::initVulkan] Swapchain created."); std::cout << "[App::initVulkan] Swapchain created." << std::endl;
        LogToFile("[App::initVulkan] Creating image views..."); std::cout << "[App::initVulkan] Creating image views..." << std::endl; createImageViews(); LogToFile("[App::initVulkan] Image views created."); std::cout << "[App::initVulkan] Image views created." << std::endl;
        LogToFile("[App::initVulkan] Creating render pass..."); std::cout << "[App::initVulkan] Creating render pass..." << std::endl; createRenderPass(); LogToFile("[App::initVulkan] Render pass created."); std::cout << "[App::initVulkan] Render pass created." << std::endl;
        LogToFile("[App::initVulkan] Creating command pool..."); std::cout << "[App::initVulkan] Creating command pool..." << std::endl; createCommandPool(); LogToFile("[App::initVulkan] Command pool created."); std::cout << "[App::initVulkan] Command pool created." << std::endl;
        LogToFile("[App::initVulkan] Creating framebuffers..."); std::cout << "[App::initVulkan] Creating framebuffers..." << std::endl; createFramebuffers(); LogToFile("[App::initVulkan] Framebuffers created."); std::cout << "[App::initVulkan] Framebuffers created." << std::endl;
        LogToFile("[App::initVulkan] Creating command buffers..."); std::cout << "[App::initVulkan] Creating command buffers..." << std::endl; createCommandBuffers(); LogToFile("[App::initVulkan] Command buffers created."); std::cout << "[App::initVulkan] Command buffers created." << std::endl;
        LogToFile("[App::initVulkan] Creating sync objects..."); std::cout << "[App::initVulkan] Creating sync objects..." << std::endl; createSyncObjects(); LogToFile("[App::initVulkan] Sync objects created."); std::cout << "[App::initVulkan] Sync objects created." << std::endl;
    } catch (const std::exception& e) {
        LogToFile(std::string("[App::initVulkan] ERROR: Vulkan initialization failed during setup: ") + e.what());
        std::cerr << "[App::initVulkan] Vulkan initialization failed during setup: " << e.what() << std::endl;
        return false;
    }
    LogToFile("[App::initVulkan] Vulkan initialization complete.");
    std::cout << "[App::initVulkan] Vulkan initialization complete." << std::endl;
    return true;
}

void App::createInstance() {
    LogToFile("[App::createInstance] Start.");
    std::cout << "[App::createInstance] Start." << std::endl;
    if (enableValidationLayers) {
        uint32_t layerCount;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
        std::vector<VkLayerProperties> availableLayers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());
        LogToFile("[App::createInstance] Available validation layers:");
        std::cout << "[App::createInstance] Available validation layers:" << std::endl;
        for (const auto& layer : availableLayers) {
            LogToFile(std::string("\t") + layer.layerName);
            std::cout << "\t" << layer.layerName << std::endl;
        }
        for (const char* layerName : validationLayers) {
            bool layerFound = false;
            for (const auto& layerProperties : availableLayers) {
                if (strcmp(layerName, layerProperties.layerName) == 0) {
                    layerFound = true;
                    LogToFile(std::string("[App::createInstance] Requested validation layer found: ") + layerName);
                    std::cout << "[App::createInstance] Requested validation layer found: " << layerName << std::endl;
                    break;
                }
            }
            if (!layerFound) {
                LogToFile(std::string("[App::createInstance] ERROR: Requested validation layer NOT FOUND: ") + layerName);
                std::cerr << "[App::createInstance] Requested validation layer NOT FOUND: " << layerName << std::endl;
                throw std::runtime_error(std::string("Validation layer requested, but not available: ") + layerName);
            }
        }
        LogToFile("[App::createInstance] All requested validation layers found.");
        std::cout << "[App::createInstance] All requested validation layers found." << std::endl;
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "MotionCam Player";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 2, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    auto extensions = getRequiredExtensions();
    LogToFile("[App::createInstance] Required instance extensions:");
    std::cout << "[App::createInstance] Required instance extensions:" << std::endl;
    for(const char* ext_name : extensions) { LogToFile(std::string("\t") + ext_name); std::cout << "\t" << ext_name << std::endl; }
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();
        debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugCreateInfo.pfnUserCallback = debugCallback;
        createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;
        LogToFile("[App::createInstance] Validation layers enabled for instance creation.");
        std::cout << "[App::createInstance] Validation layers enabled for instance creation." << std::endl;
    } else {
        createInfo.enabledLayerCount = 0;
        createInfo.pNext = nullptr;
        LogToFile("[App::createInstance] Validation layers disabled for instance creation.");
        std::cout << "[App::createInstance] Validation layers disabled for instance creation." << std::endl;
    }

    #ifdef __APPLE__
    createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    LogToFile("[App::createInstance] VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR flag set for Apple.");
    std::cout << "[App::createInstance] VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR flag set for Apple." << std::endl;
    #endif

    LogToFile("[App::createInstance] Calling vkCreateInstance...");
    std::cout << "[App::createInstance] Calling vkCreateInstance..." << std::endl;
    VK_APP_CHECK(vkCreateInstance(&createInfo, nullptr, &m_vkInstance));
    LogToFile("[App::createInstance] vkCreateInstance successful.");
    std::cout << "[App::createInstance] vkCreateInstance successful." << std::endl;
}

std::vector<const char*> App::getRequiredExtensions() {
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions;
    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
    if (enableValidationLayers) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    #ifdef __APPLE__
    extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    #endif
    return extensions;
}

VkResult CreateDebugUtilsMessengerEXT_Helper(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger) {
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr) {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    } else {
        LogToFile("[CreateDebugUtilsMessengerEXT_Helper] ERROR: vkCreateDebugUtilsMessengerEXT function not found!");
        std::cerr << "[CreateDebugUtilsMessengerEXT_Helper] vkCreateDebugUtilsMessengerEXT function not found!" << std::endl;
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}
void DestroyDebugUtilsMessengerEXT_Helper(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator) {
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr) {
        func(instance, debugMessenger, pAllocator);
    } else {
        LogToFile("[DestroyDebugUtilsMessengerEXT_Helper] ERROR: vkDestroyDebugUtilsMessengerEXT function not found!");
        std::cerr << "[DestroyDebugUtilsMessengerEXT_Helper] vkDestroyDebugUtilsMessengerEXT function not found!" << std::endl;
    }
}

void App::setupDebugMessenger() {
    if (!enableValidationLayers) {
        LogToFile("[App::setupDebugMessenger] Validation layers disabled, skipping.");
        std::cout << "[App::setupDebugMessenger] Validation layers disabled, skipping." << std::endl;
        return;
    }
    LogToFile("[App::setupDebugMessenger] Setting up...");
    std::cout << "[App::setupDebugMessenger] Setting up..." << std::endl;
    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;
    VK_APP_CHECK(CreateDebugUtilsMessengerEXT_Helper(m_vkInstance, &createInfo, nullptr, &m_debugMessenger));
    LogToFile("[App::setupDebugMessenger] Setup complete.");
    std::cout << "[App::setupDebugMessenger] Setup complete." << std::endl;
}

void App::createSurface() {
    LogToFile("[App::createSurface] Creating window surface...");
    std::cout << "[App::createSurface] Creating window surface..." << std::endl;
    VK_APP_CHECK(glfwCreateWindowSurface(m_vkInstance, m_window, nullptr, &m_surface));
    LogToFile("[App::createSurface] Window surface created.");
    std::cout << "[App::createSurface] Window surface created." << std::endl;
}

void App::pickPhysicalDevice() {
    LogToFile("[App::pickPhysicalDevice] Enumerating physical devices...");
    std::cout << "[App::pickPhysicalDevice] Enumerating physical devices..." << std::endl;
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_vkInstance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        LogToFile("[App::pickPhysicalDevice] ERROR: Failed to find GPUs with Vulkan support!");
        std::cerr << "[App::pickPhysicalDevice] Failed to find GPUs with Vulkan support!" << std::endl;
        throw std::runtime_error("Failed to find GPUs with Vulkan support!");
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_vkInstance, &deviceCount, devices.data());
    LogToFile(std::string("[App::pickPhysicalDevice] Found ") + std::to_string(deviceCount) + " physical device(s).");
    std::cout << "[App::pickPhysicalDevice] Found " << deviceCount << " physical device(s)." << std::endl;

    for (const auto& device : devices) {
        VkPhysicalDeviceProperties deviceProperties;
        vkGetPhysicalDeviceProperties(device, &deviceProperties);
        LogToFile(std::string("[App::pickPhysicalDevice] Checking device: ") + deviceProperties.deviceName);
        std::cout << "[App::pickPhysicalDevice] Checking device: " << deviceProperties.deviceName << std::endl;
        if (isDeviceSuitable(device)) {
            m_physicalDevice = device;
            LogToFile(std::string("[App::pickPhysicalDevice] Suitable device found: ") + deviceProperties.deviceName);
            std::cout << "[App::pickPhysicalDevice] Suitable device found: " << deviceProperties.deviceName << std::endl;
            break;
        }
    }
    if (m_physicalDevice == VK_NULL_HANDLE) {
        LogToFile("[App::pickPhysicalDevice] ERROR: Failed to find a suitable GPU!");
        std::cerr << "[App::pickPhysicalDevice] Failed to find a suitable GPU!" << std::endl;
        throw std::runtime_error("Failed to find a suitable GPU!");
    }
}

bool App::isDeviceSuitable(VkPhysicalDevice queryDevice) {
    LogToFile("[App::isDeviceSuitable] Checking suitability...");
    std::cout << "[App::isDeviceSuitable] Checking suitability..." << std::endl;
    QueueFamilyIndices indices = findQueueFamilies(queryDevice);
    bool extensionsSupported = checkDeviceExtensionSupport(queryDevice);
    bool swapChainAdequate = false;
    if (extensionsSupported) {
        SwapChainSupportDetails swapChainSupport = querySwapChainSupport(queryDevice);
        swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
    }
    VkPhysicalDeviceFeatures supportedFeatures;
    vkGetPhysicalDeviceFeatures(queryDevice, &supportedFeatures);

    LogToFile(std::string("[App::isDeviceSuitable] Queue families complete: ") + (indices.isComplete() ? "Yes" : "No"));
    LogToFile(std::string("[App::isDeviceSuitable] Extensions supported: ") + (extensionsSupported ? "Yes" : "No"));
    LogToFile(std::string("[App::isDeviceSuitable] Swapchain adequate: ") + (swapChainAdequate ? "Yes" : "No"));
    LogToFile(std::string("[App::isDeviceSuitable] Sampler anisotropy supported: ") + (supportedFeatures.samplerAnisotropy ? "Yes" : "No"));
    std::cout << "[App::isDeviceSuitable] Queue families complete: " << (indices.isComplete() ? "Yes" : "No") << std::endl;
    std::cout << "[App::isDeviceSuitable] Extensions supported: " << (extensionsSupported ? "Yes" : "No") << std::endl;
    std::cout << "[App::isDeviceSuitable] Swapchain adequate: " << (swapChainAdequate ? "Yes" : "No") << std::endl;
    std::cout << "[App::isDeviceSuitable] Sampler anisotropy supported: " << (supportedFeatures.samplerAnisotropy ? "Yes" : "No") << std::endl;

    return indices.isComplete() && extensionsSupported && swapChainAdequate && supportedFeatures.samplerAnisotropy;
}

bool App::checkDeviceExtensionSupport(VkPhysicalDevice queryDevice) {
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(queryDevice, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(queryDevice, nullptr, &extensionCount, availableExtensions.data());

    LogToFile("[App::checkDeviceExtensionSupport] Required device extensions:");
    std::cout << "[App::checkDeviceExtensionSupport] Required device extensions:" << std::endl;
    for (const char* req_ext : deviceExtensions) { LogToFile(std::string("\t") + req_ext); std::cout << "\t" << req_ext << std::endl;}

    std::set<std::string> requiredExtensionsSet(deviceExtensions.begin(), deviceExtensions.end());
    LogToFile("[App::checkDeviceExtensionSupport] Available device extensions:");
    std::cout << "[App::checkDeviceExtensionSupport] Available device extensions:" << std::endl;
    for (const auto& extension : availableExtensions) {
        LogToFile(std::string("\t") + extension.extensionName);
        std::cout << "\t" << extension.extensionName << std::endl;
        requiredExtensionsSet.erase(extension.extensionName);
    }
    if (!requiredExtensionsSet.empty()) {
        LogToFile("[App::checkDeviceExtensionSupport] ERROR: Missing required device extensions:");
        std::cerr << "[App::checkDeviceExtensionSupport] Missing required device extensions:" << std::endl;
        for(const auto& missing : requiredExtensionsSet) {
             LogToFile(std::string("\t") + missing);
             std::cerr << "\t" << missing << std::endl;
        }
    }
    return requiredExtensionsSet.empty();
}

App::QueueFamilyIndices App::findQueueFamilies(VkPhysicalDevice queryDevice) {
    QueueFamilyIndices indices;
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(queryDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(queryDevice, &queueFamilyCount, queueFamilies.data());
    int i = 0;
    for (const auto& queueFamily : queueFamilies) {
        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphicsFamily = i;
        }
        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(queryDevice, i, m_surface, &presentSupport);
        if (presentSupport) {
            indices.presentFamily = i;
        }
        if (indices.isComplete()) {
            break;
        }
        i++;
    }
    return indices;
}

void App::createLogicalDevice() {
    LogToFile("[App::createLogicalDevice] Creating logical device...");
    std::cout << "[App::createLogicalDevice] Creating logical device..." << std::endl;
    QueueFamilyIndices indices = findQueueFamilies(m_physicalDevice);
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = {indices.graphicsFamily.value(), indices.presentFamily.value()};
    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = VK_TRUE;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();

    if (enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();
    } else {
        createInfo.enabledLayerCount = 0;
    }
    VK_APP_CHECK(vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device));
    vkGetDeviceQueue(m_device, indices.graphicsFamily.value(), 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, indices.presentFamily.value(), 0, &m_presentQueue);
    LogToFile("[App::createLogicalDevice] Logical device created.");
    std::cout << "[App::createLogicalDevice] Logical device created." << std::endl;
}

App::SwapChainSupportDetails App::querySwapChainSupport(VkPhysicalDevice queryDevice) {
    SwapChainSupportDetails details;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(queryDevice, m_surface, &details.capabilities);
    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(queryDevice, m_surface, &formatCount, nullptr);
    if (formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(queryDevice, m_surface, &formatCount, details.formats.data());
    }
    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(queryDevice, m_surface, &presentModeCount, nullptr);
    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(queryDevice, m_surface, &presentModeCount, details.presentModes.data());
    }
    return details;
}

VkSurfaceFormatKHR App::chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
    for (const auto& availableFormat : availableFormats) {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_UNORM && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            LogToFile("[App::chooseSwapSurfaceFormat] Choosing B8G8R8A8_UNORM with VK_COLOR_SPACE_SRGB_NONLINEAR_KHR.");
            return availableFormat;
        }
    }
    for (const auto& availableFormat : availableFormats) {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            LogToFile("[App::chooseSwapSurfaceFormat] Choosing B8G8R8A8_SRGB with VK_COLOR_SPACE_SRGB_NONLINEAR_KHR.");
            return availableFormat;
        }
    }
    LogToFile("[App::chooseSwapSurfaceFormat] Preferred UNORM/SRGB formats not found, using first available. This might cause color issues if shader output is not sRGB.");
    if (availableFormats.empty()) throw std::runtime_error("No swapchain formats available!");
    LogToFile(std::string("[App::chooseSwapSurfaceFormat] Fallback to: format ") + std::to_string(availableFormats[0].format) + " colorspace " + std::to_string(availableFormats[0].colorSpace) );
    return availableFormats[0];
}

VkPresentModeKHR App::chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
    for (const auto& availablePresentMode : availablePresentModes) {
        if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return availablePresentMode;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D App::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
    if (capabilities.currentExtent.width != UINT32_MAX) {
        return capabilities.currentExtent;
    } else {
        int width, height;
        glfwGetFramebufferSize(m_window, &width, &height);
        VkExtent2D actualExtent = {
            static_cast<uint32_t>(width),
            static_cast<uint32_t>(height)
        };
        actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
        return actualExtent;
    }
}

void App::createSwapChain() {
    LogToFile("[App::createSwapChain] Creating swapchain...");
    std::cout << "[App::createSwapChain] Creating swapchain..." << std::endl;
    SwapChainSupportDetails swapChainSupport = querySwapChainSupport(m_physicalDevice);
    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
    VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);

    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
    if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) {
        imageCount = swapChainSupport.capabilities.maxImageCount;
    }
    imageCount = std::max(imageCount, (uint32_t)MAX_FRAMES_IN_FLIGHT);
    imageCount = std::max(imageCount, swapChainSupport.capabilities.minImageCount);


    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = m_surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    QueueFamilyIndices indices = findQueueFamilies(m_physicalDevice);
    uint32_t queueFamilyIndices[] = {indices.graphicsFamily.value(), indices.presentFamily.value()};
    if (indices.graphicsFamily != indices.presentFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    VK_APP_CHECK(vkCreateSwapchainKHR(m_device, &createInfo, nullptr, &m_swapChain));
    vkGetSwapchainImagesKHR(m_device, m_swapChain, &imageCount, nullptr);
    m_swapChainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(m_device, m_swapChain, &imageCount, m_swapChainImages.data());
    m_swapChainImageFormat = surfaceFormat.format;
    m_swapChainExtent = extent;
    LogToFile(std::string("[App::createSwapChain] Swapchain created with ") + std::to_string(imageCount) + " images. Format: " + std::to_string(m_swapChainImageFormat) + " Extent: " + std::to_string(m_swapChainExtent.width) + "x" + std::to_string(m_swapChainExtent.height));
    std::cout << "[App::createSwapChain] Swapchain created with " << imageCount << " images." << std::endl;
}

void App::createImageViews() {
    LogToFile(std::string("[App::createImageViews] Creating ") + std::to_string(m_swapChainImages.size()) + " image views...");
    std::cout << "[App::createImageViews] Creating " << m_swapChainImages.size() << " image views..." << std::endl;
    m_swapChainImageViews.resize(m_swapChainImages.size());
    for (size_t i = 0; i < m_swapChainImages.size(); i++) {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = m_swapChainImages[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = m_swapChainImageFormat;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;
        VK_APP_CHECK(vkCreateImageView(m_device, &createInfo, nullptr, &m_swapChainImageViews[i]));
    }
    LogToFile("[App::createImageViews] Image views created.");
    std::cout << "[App::createImageViews] Image views created." << std::endl;
}

void App::createRenderPass() {
    LogToFile("[App::createRenderPass] Creating render pass...");
    std::cout << "[App::createRenderPass] Creating render pass..." << std::endl;
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = m_swapChainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    VK_APP_CHECK(vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_renderPass));
    LogToFile("[App::createRenderPass] Render pass created.");
    std::cout << "[App::createRenderPass] Render pass created." << std::endl;
}

void App::createFramebuffers() {
    LogToFile(std::string("[App::createFramebuffers] Creating ") + std::to_string(m_swapChainImageViews.size()) + " framebuffers...");
    std::cout << "[App::createFramebuffers] Creating " << m_swapChainImageViews.size() << " framebuffers..." << std::endl;
    m_swapChainFramebuffers.resize(m_swapChainImageViews.size());
    for (size_t i = 0; i < m_swapChainImageViews.size(); i++) {
        VkImageView attachments[] = {m_swapChainImageViews[i]};
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = m_renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = m_swapChainExtent.width;
        framebufferInfo.height = m_swapChainExtent.height;
        framebufferInfo.layers = 1;
        VK_APP_CHECK(vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &m_swapChainFramebuffers[i]));
    }
    LogToFile("[App::createFramebuffers] Framebuffers created.");
    std::cout << "[App::createFramebuffers] Framebuffers created." << std::endl;
}

void App::createCommandPool() {
    LogToFile("[App::createCommandPool] Creating command pool...");
    std::cout << "[App::createCommandPool] Creating command pool..." << std::endl;
    QueueFamilyIndices queueFamilyIndices = findQueueFamilies(m_physicalDevice);
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();
    VK_APP_CHECK(vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool));
    LogToFile("[App::createCommandPool] Command pool created.");
    std::cout << "[App::createCommandPool] Command pool created." << std::endl;
}

void App::createCommandBuffers() {
    LogToFile(std::string("[App::createCommandBuffers] Creating ") + std::to_string(MAX_FRAMES_IN_FLIGHT) + " command buffers...");
    std::cout << "[App::createCommandBuffers] Creating " << MAX_FRAMES_IN_FLIGHT << " command buffers..." << std::endl;
    m_commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = (uint32_t)m_commandBuffers.size();
    VK_APP_CHECK(vkAllocateCommandBuffers(m_device, &allocInfo, m_commandBuffers.data()));
    LogToFile("[App::createCommandBuffers] Command buffers created.");
    std::cout << "[App::createCommandBuffers] Command buffers created." << std::endl;
}

void App::createSyncObjects() {
    LogToFile(std::string("[App::createSyncObjects] Creating sync objects (") + std::to_string(MAX_FRAMES_IN_FLIGHT) + " sets)...");
    std::cout << "[App::createSyncObjects] Creating sync objects (" << MAX_FRAMES_IN_FLIGHT << " sets)..." << std::endl;
    m_imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    m_inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VK_APP_CHECK(vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_imageAvailableSemaphores[i]));
        VK_APP_CHECK(vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_renderFinishedSemaphores[i]));
        VK_APP_CHECK(vkCreateFence(m_device, &fenceInfo, nullptr, &m_inFlightFences[i]));
    }
    LogToFile("[App::createSyncObjects] Sync objects created.");
    std::cout << "[App::createSyncObjects] Sync objects created." << std::endl;
}

void App::initImGuiVulkan() {
    LogToFile("[App::initImGuiVulkan] Initializing ImGui for Vulkan...");
    std::cout << "[App::initImGuiVulkan] Initializing ImGui for Vulkan..." << std::endl;
    VkDescriptorPoolSize pool_sizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 }, { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 }, { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 }, { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 }, { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 }, { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    };
    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000 * IM_ARRAYSIZE(pool_sizes);
    pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;
    VK_APP_CHECK(vkCreateDescriptorPool(m_device, &pool_info, nullptr, &m_imguiDescriptorPool));
    LogToFile("[App::initImGuiVulkan] ImGui descriptor pool created.");
    std::cout << "[App::initImGuiVulkan] ImGui descriptor pool created." << std::endl;

    GuiOverlay::setup(m_window, this);
    LogToFile("[App::initImGuiVulkan] GuiOverlay::setup() called.");
    std::cout << "[App::initImGuiVulkan] GuiOverlay::setup() called." << std::endl;
}

void App::drawFrame() {
    VK_APP_CHECK(vkWaitForFences(m_device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE, UINT64_MAX));

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(m_device, m_swapChain, UINT64_MAX, m_imageAvailableSemaphores[m_currentFrame], VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        LogToFile("[App::drawFrame] Swapchain out of date (VK_ERROR_OUT_OF_DATE_KHR on acquire), recreating.");
        std::cout << "[App::drawFrame] Swapchain out of date (VK_ERROR_OUT_OF_DATE_KHR on acquire), recreating." << std::endl;
        recreateSwapChain();
        return;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        LogToFile(std::string("[App::drawFrame] ERROR: Failed to acquire swap chain image! Result: ") + std::to_string(result));
        std::cerr << "[App::drawFrame] Failed to acquire swap chain image! Result: " << result << std::endl;
        throw std::runtime_error("Failed to acquire swap chain image!");
    }

    VK_APP_CHECK(vkResetFences(m_device, 1, &m_inFlightFences[m_currentFrame]));
    
    VkCommandBuffer currentCommandBuffer = m_commandBuffers[m_currentFrame];
    VK_APP_CHECK(vkResetCommandBuffer(currentCommandBuffer, 0));

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    VK_APP_CHECK(vkBeginCommandBuffer(currentCommandBuffer, &beginInfo));

    std::vector<uint16_t> frame_data_for_render;
    nlohmann::json frame_meta_for_render;
    int local_raw_width = 0;
    int local_raw_height = 0;

    auto t0_decode_in_draw = std::chrono::steady_clock::now();
    if (m_decoderWrapper && m_decoderWrapper->getDecoder()) {
        const auto& dec_frames = m_decoderWrapper->getDecoder()->getFrames();
        if (!dec_frames.empty()) {
            size_t idx = m_playbackController->getCurrentFrameIndex();
            if (idx >= dec_frames.size()) idx = dec_frames.size() > 0 ? dec_frames.size() - 1 : 0;
            try {
                m_decoderWrapper->getDecoder()->loadFrame(dec_frames[idx], frame_data_for_render, frame_meta_for_render);
                local_raw_width = frame_meta_for_render.value("width", 0);
                local_raw_height = frame_meta_for_render.value("height", 0);
            } catch (const std::exception& e) {
                LogToFile(std::string("[App::drawFrame] Error loading frame ") + std::to_string(idx) + ": " + e.what());
                std::cerr << "[App::drawFrame] Error loading frame " << idx << ": " << e.what() << std::endl;
                frame_data_for_render.clear();
            }
        }
    }
    auto t1_decode_in_draw = std::chrono::steady_clock::now();
    m_decodingTimeMs = std::chrono::duration<double, std::milli>(t1_decode_in_draw - t0_decode_in_draw).count();

    if (m_rendererVk && !frame_data_for_render.empty() && local_raw_width > 0 && local_raw_height > 0) {
        m_rendererVk->ensureRawImageReadyAndUpload(currentCommandBuffer,
                                                   frame_data_for_render,
                                                   local_raw_width, local_raw_height,
                                                   m_currentFrame);
    }

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_renderPass;
    renderPassInfo.framebuffer = m_swapChainFramebuffers[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = m_swapChainExtent;
    VkClearValue clearColor = { {{0.05f, 0.05f, 0.05f, 1.0f}} };
    if (!m_firstFileLoaded) {
        clearColor = { {{40.0f/255.0f, 40.0f/255.0f, 40.0f/255.0f, 1.0f}} };
    }
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;

    vkCmdBeginRenderPass(currentCommandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    if (m_rendererVk && !frame_data_for_render.empty() && local_raw_width > 0 && local_raw_height > 0) {
        int cfa = m_cfaOverride.value_or(m_cfaTypeFromMetadata);
        glfwGetFramebufferSize(m_window, &m_windowWidth, &m_windowHeight);
        
        int orientationTag = m_containerOrientationTag;
        if (frame_meta_for_render.contains("orientation")) {
            orientationTag = computeOrientationTag(frame_meta_for_render["orientation"], m_containerFlipped, m_containerOrientationTag);
        }

        m_rendererVk->recordRenderCommands(currentCommandBuffer, m_currentFrame,
                                           frame_meta_for_render,
                                           m_staticBlack, m_staticWhite, cfa,
                                           m_windowWidth, m_windowHeight,
                                           orientationTag);
    }

    if (m_showUI) {
        GuiOverlay::beginFrame();
        GuiOverlay::render(this);
        GuiOverlay::endFrame(currentCommandBuffer);
    }

    vkCmdEndRenderPass(currentCommandBuffer);
    VK_APP_CHECK(vkEndCommandBuffer(currentCommandBuffer));

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    VkSemaphore waitSemaphores[] = {m_imageAvailableSemaphores[m_currentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &currentCommandBuffer;
    VkSemaphore signalSemaphores[] = {m_renderFinishedSemaphores[m_currentFrame]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    VK_APP_CHECK(vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, m_inFlightFences[m_currentFrame]));

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    VkSwapchainKHR swapChains[] = {m_swapChain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;

    auto t0_present_wait = std::chrono::steady_clock::now();
    result = vkQueuePresentKHR(m_presentQueue, &presentInfo);
    auto t1_present_wait = std::chrono::steady_clock::now();
    m_gpuWaitTimeMs = std::chrono::duration<double, std::milli>(t1_present_wait - t0_present_wait).count();

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_framebufferResized) {
        if (result == VK_ERROR_OUT_OF_DATE_KHR) { LogToFile("[App::drawFrame] Swapchain out of date (VK_ERROR_OUT_OF_DATE_KHR on present), recreating."); std::cout << "[App::drawFrame] Swapchain out of date (VK_ERROR_OUT_OF_DATE_KHR on present), recreating." << std::endl;}
        if (result == VK_SUBOPTIMAL_KHR) { LogToFile("[App::drawFrame] Swapchain suboptimal, recreating."); std::cout << "[App::drawFrame] Swapchain suboptimal, recreating." << std::endl; }
        if (m_framebufferResized) { LogToFile("[App::drawFrame] Framebuffer resized, recreating swapchain."); std::cout << "[App::drawFrame] Framebuffer resized, recreating swapchain." << std::endl; }
        m_framebufferResized = false;
        recreateSwapChain();
    } else if (result != VK_SUCCESS) {
        LogToFile(std::string("[App::drawFrame] ERROR: Failed to present swap chain image! Result: ") + std::to_string(result));
        std::cerr << "[App::drawFrame] Failed to present swap chain image! Result: " << result << std::endl;
        throw std::runtime_error("Failed to present swap chain image!");
    }
    m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void App::cleanupSwapChain() {
    LogToFile("[App::cleanupSwapChain] Cleaning up swapchain resources...");
    std::cout << "[App::cleanupSwapChain] Cleaning up swapchain resources..." << std::endl;
    for (auto framebuffer : m_swapChainFramebuffers) {
        if (framebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(m_device, framebuffer, nullptr);
    }
    m_swapChainFramebuffers.clear();

    if (m_rendererVk) {
        m_rendererVk->cleanupSwapChainResources();
    }
    
    // GuiOverlay::cleanup() handles ImGui_ImplVulkan_Shutdown()
    // It should be safe to call even if ImGui wasn't fully initialized,
    // as ImGui's shutdown functions usually check for null.
    GuiOverlay::cleanup();


    for (auto imageView : m_swapChainImageViews) {
        if (imageView != VK_NULL_HANDLE) vkDestroyImageView(m_device, imageView, nullptr);
    }
    m_swapChainImageViews.clear();

    if (m_swapChain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_device, m_swapChain, nullptr);
        m_swapChain = VK_NULL_HANDLE;
    }
    LogToFile("[App::cleanupSwapChain] Swapchain resources cleaned.");
    std::cout << "[App::cleanupSwapChain] Swapchain resources cleaned." << std::endl;
}

void App::recreateSwapChain() {
    LogToFile("[App::recreateSwapChain] Starting swapchain recreation...");
    std::cout << "[App::recreateSwapChain] Starting swapchain recreation..." << std::endl;
    int width = 0, height = 0;
    glfwGetFramebufferSize(m_window, &width, &height);
    while (width == 0 || height == 0) {
        LogToFile("[App::recreateSwapChain] Window minimized, waiting for events...");
        std::cout << "[App::recreateSwapChain] Window minimized, waiting for events..." << std::endl;
        glfwGetFramebufferSize(m_window, &width, &height);
        glfwWaitEvents();
    }
    LogToFile(std::string("[App::recreateSwapChain] New window size: ") + std::to_string(width) + "x" + std::to_string(height));
    std::cout << "[App::recreateSwapChain] New window size: " << width << "x" << height << std::endl;
    VK_APP_CHECK(vkDeviceWaitIdle(m_device));
    LogToFile("[App::recreateSwapChain] Device idle.");
    std::cout << "[App::recreateSwapChain] Device idle." << std::endl;

    cleanupSwapChain();
    LogToFile("[App::recreateSwapChain] Old swapchain cleaned.");
    std::cout << "[App::recreateSwapChain] Old swapchain cleaned." << std::endl;

    createSwapChain(); LogToFile("[App::recreateSwapChain] New swapchain created."); std::cout << "[App::recreateSwapChain] New swapchain created." << std::endl;
    createImageViews(); LogToFile("[App::recreateSwapChain] New image views created."); std::cout << "[App::recreateSwapChain] New image views created." << std::endl;
    createFramebuffers(); LogToFile("[App::recreateSwapChain] New framebuffers created."); std::cout << "[App::recreateSwapChain] New framebuffers created." << std::endl;

    if (m_rendererVk) {
        LogToFile("[App::recreateSwapChain] Notifying Renderer_VK about swapchain recreation.");
        std::cout << "[App::recreateSwapChain] Notifying Renderer_VK about swapchain recreation." << std::endl;
        m_rendererVk->onSwapChainRecreated(m_renderPass, static_cast<uint32_t>(m_swapChainImages.size()));
    }

    // GuiOverlay::cleanup() was called in cleanupSwapChain.
    // Now re-initialize ImGui fully. initImGuiVulkan() handles pool creation.
    LogToFile("[App::recreateSwapChain] Re-initializing ImGui for new swapchain.");
    if (m_imguiDescriptorPool != VK_NULL_HANDLE) { // Destroy App's own pool if it existed
        vkDestroyDescriptorPool(m_device, m_imguiDescriptorPool, nullptr);
        m_imguiDescriptorPool = VK_NULL_HANDLE;
    }
    initImGuiVulkan(); // This will create a new m_imguiDescriptorPool and call GuiOverlay::setup()

    LogToFile("[App::recreateSwapChain] Swapchain recreation complete.");
    std::cout << "[App::recreateSwapChain] Swapchain recreation complete." << std::endl;
}

void App::cleanupVulkan() {
    LogToFile("[App::cleanupVulkan] Starting Vulkan cleanup...");
    std::cout << "[App::cleanupVulkan] Starting Vulkan cleanup..." << std::endl;
    if (m_device != VK_NULL_HANDLE) {
        LogToFile("[App::cleanupVulkan] Waiting for device idle...");
        std::cout << "[App::cleanupVulkan] Waiting for device idle..." << std::endl;
        VK_APP_CHECK(vkDeviceWaitIdle(m_device));
        LogToFile("[App::cleanupVulkan] Device idle.");
        std::cout << "[App::cleanupVulkan] Device idle." << std::endl;
    }

    cleanupSwapChain();

    if (m_rendererVk) {
        LogToFile("[App::cleanupVulkan] Cleaning up Renderer_VK (general)...");
        std::cout << "[App::cleanupVulkan] Cleaning up Renderer_VK (general)..." << std::endl;
        m_rendererVk->cleanup();
        m_rendererVk.reset();
    }
    
    // GuiOverlay::cleanup() is called by cleanupSwapChain.
    // The App's m_imguiDescriptorPool is now managed by initImGuiVulkan creating it
    // and cleanupSwapChain via GuiOverlay::cleanup ultimately leading to its destruction
    // if ImGui_ImplVulkan_Shutdown destroys the pool it was given.
    // To be safe, if App owns it, App should destroy it here if not already done.
    if (m_imguiDescriptorPool != VK_NULL_HANDLE) {
        LogToFile("[App::cleanupVulkan] Destroying ImGui descriptor pool (if not already done).");
        vkDestroyDescriptorPool(m_device, m_imguiDescriptorPool, nullptr);
        m_imguiDescriptorPool = VK_NULL_HANDLE;
    }


    LogToFile("[App::cleanupVulkan] Destroying sync objects...");
    std::cout << "[App::cleanupVulkan] Destroying sync objects..." << std::endl;
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (m_renderFinishedSemaphores.size() > i && m_renderFinishedSemaphores[i] != VK_NULL_HANDLE)
            vkDestroySemaphore(m_device, m_renderFinishedSemaphores[i], nullptr);
        if (m_imageAvailableSemaphores.size() > i && m_imageAvailableSemaphores[i] != VK_NULL_HANDLE)
            vkDestroySemaphore(m_device, m_imageAvailableSemaphores[i], nullptr);
        if (m_inFlightFences.size() > i && m_inFlightFences[i] != VK_NULL_HANDLE)
            vkDestroyFence(m_device, m_inFlightFences[i], nullptr);
    }
    m_renderFinishedSemaphores.clear();
    m_imageAvailableSemaphores.clear();
    m_inFlightFences.clear();

    if (m_commandPool != VK_NULL_HANDLE) {
        LogToFile("[App::cleanupVulkan] Destroying command pool...");
        std::cout << "[App::cleanupVulkan] Destroying command pool..." << std::endl;
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;
    }
    if (m_renderPass != VK_NULL_HANDLE) {
        LogToFile("[App::cleanupVulkan] Destroying render pass...");
        std::cout << "[App::cleanupVulkan] Destroying render pass..." << std::endl;
        vkDestroyRenderPass(m_device, m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }
    if (m_device != VK_NULL_HANDLE) {
        LogToFile("[App::cleanupVulkan] Destroying logical device...");
        std::cout << "[App::cleanupVulkan] Destroying logical device..." << std::endl;
        vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
    }
    if (enableValidationLayers && m_debugMessenger != VK_NULL_HANDLE) {
        LogToFile("[App::cleanupVulkan] Destroying debug messenger...");
        std::cout << "[App::cleanupVulkan] Destroying debug messenger..." << std::endl;
        DestroyDebugUtilsMessengerEXT_Helper(m_vkInstance, m_debugMessenger, nullptr);
        m_debugMessenger = VK_NULL_HANDLE;
    }
    if (m_surface != VK_NULL_HANDLE) {
        LogToFile("[App::cleanupVulkan] Destroying surface...");
        std::cout << "[App::cleanupVulkan] Destroying surface..." << std::endl;
        vkDestroySurfaceKHR(m_vkInstance, m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }
    if (m_vkInstance != VK_NULL_HANDLE) {
        LogToFile("[App::cleanupVulkan] Destroying Vulkan instance...");
        std::cout << "[App::cleanupVulkan] Destroying Vulkan instance..." << std::endl;
        vkDestroyInstance(m_vkInstance, nullptr);
        m_vkInstance = VK_NULL_HANDLE;
    }
    LogToFile("[App::cleanupVulkan] Vulkan cleanup complete.");
    std::cout << "[App::cleanupVulkan] Vulkan cleanup complete." << std::endl;
}

void App::loadFileAtIndex(int index) {
    LogToFile(std::string("[App::loadFileAtIndex] Loading file at index: ") + std::to_string(index));
    std::cout << "[App::loadFileAtIndex] Loading file at index: " << index << std::endl;
    if (m_fileList.empty()) {
        LogToFile("[App::loadFileAtIndex] File list is empty, closing window.");
        std::cout << "[App::loadFileAtIndex] File list is empty, closing window." << std::endl;
        if (m_window) glfwSetWindowShouldClose(m_window, GLFW_TRUE); return;
    }
    if (index < 0 || static_cast<size_t>(index) >= m_fileList.size()) {
        LogToFile(std::string("[App::loadFileAtIndex] Index ") + std::to_string(index) + " out of bounds, defaulting to 0.");
        std::cout << "[App::loadFileAtIndex] Index " << index << " out of bounds, defaulting to 0." << std::endl;
        index = 0;
    }
    if (m_decoderWrapper && m_decoderWrapper->getDecoder() && m_currentFileIndex == index && m_firstFileLoaded) {
        LogToFile(std::string("[App::loadFileAtIndex] File at index ") + std::to_string(index) + " is already loaded, returning.");
        std::cout << "[App::loadFileAtIndex] File at index " << index << " is already loaded, returning." << std::endl;
        return;
    }

    m_currentFileIndex = index;
    LogToFile(std::string("[App::loadFileAtIndex] Current file: ") + m_fileList[m_currentFileIndex]);
    std::cout << "[App::loadFileAtIndex] Current file: " << m_fileList[m_currentFileIndex] << std::endl;
    if(m_audio) m_audio->setForceMute(true);
    m_decoderWrapper.reset();
    try {
        m_decoderWrapper = std::make_unique<DecoderWrapper>(m_fileList[m_currentFileIndex]);
        LogToFile("[App::loadFileAtIndex] DecoderWrapper created.");
        std::cout << "[App::loadFileAtIndex] DecoderWrapper created." << std::endl;
    } catch (const std::exception& e) {
        LogToFile(std::string("[App::loadFileAtIndex] ERROR loading file: ") + m_fileList[m_currentFileIndex] + " - " + e.what());
        std::cerr << "[App::loadFileAtIndex] Error loading file: " << m_fileList[m_currentFileIndex] << " - " << e.what() << std::endl;
        if (m_playbackController) { m_playbackController->processNewSegment({}, 0, std::chrono::steady_clock::now()); }
        return;
    }
    auto meta = m_decoderWrapper->getContainerMetadata();
    m_containerFlipped = false;
    if (meta.contains("extraData") && meta["extraData"].contains("postProcessSettings") &&
        meta["extraData"]["postProcessSettings"].contains("flipped")) {
        m_containerFlipped = meta["extraData"]["postProcessSettings"]["flipped"].get<bool>();
    }
    m_containerOrientationTag = tinydngwriter::ORIENTATION_TOPLEFT;
    if (meta.contains("orientation")) {
        m_containerOrientationTag = computeOrientationTag(meta["orientation"], m_containerFlipped, tinydngwriter::ORIENTATION_TOPLEFT);
    }
    auto blackLevelVec = meta.value("blackLevel", std::vector<double>{0.0});
    m_staticBlack = blackLevelVec.empty() ? 0.0 : std::accumulate(blackLevelVec.begin(), blackLevelVec.end(), 0.0) / blackLevelVec.size();
    m_staticWhite = meta.value("whiteLevel", 65535.0);
    m_cfaStringFromMetadata = meta.value("sensorArrangment", meta.value("sensorArrangement", "BGGR"));
    m_cfaTypeFromMetadata = Renderer_VK::getCfaType(m_cfaStringFromMetadata);
    LogToFile(std::string("[App::loadFileAtIndex] Metadata parsed: Black=") + std::to_string(m_staticBlack) + ", White=" + std::to_string(m_staticWhite) + ", CFA=" + m_cfaStringFromMetadata);
    std::cout << "[App::loadFileAtIndex] Metadata parsed: Black=" << m_staticBlack << ", White=" << m_staticWhite << ", CFA=" << m_cfaStringFromMetadata << std::endl;


    if (!m_firstFileLoaded && !m_isFullscreen && m_window) {
        int videoW = meta.value("width", 0);
        int videoH = meta.value("height", 0);
        if (videoW > 0 && videoH > 0) {
            LogToFile(std::string("[App::loadFileAtIndex] Initial window resize based on video ") + std::to_string(videoW) + "x" + std::to_string(videoH));
            std::cout << "[App::loadFileAtIndex] Initial window resize based on video " << videoW << "x" << videoH << std::endl;
            const int desiredInitialHeight = 720;
            int newInitialWidth = static_cast<int>(std::round(static_cast<float>(videoW) * desiredInitialHeight / static_cast<float>(videoH)));
            int newInitialHeight = desiredInitialHeight;
            GLFWmonitor* primary = glfwGetPrimaryMonitor();
            const GLFWvidmode* mode = glfwGetVideoMode(primary);
            if (mode) {
                int maxW = static_cast<int>(mode->width * 0.90);
                int maxH = static_cast<int>(mode->height * 0.90);
                float videoAspectRatio = static_cast<float>(videoW) / videoH;

                newInitialWidth = static_cast<int>(std::round(videoAspectRatio * newInitialHeight));
                if (newInitialWidth > maxW) { newInitialWidth = maxW; newInitialHeight = static_cast<int>(std::round(static_cast<float>(newInitialWidth) / videoAspectRatio)); }
                if (newInitialHeight > maxH) { newInitialHeight = maxH; newInitialWidth = static_cast<int>(std::round(static_cast<float>(newInitialHeight) * videoAspectRatio)); }
                if (newInitialWidth > maxW) { newInitialWidth = maxW; newInitialHeight = static_cast<int>(std::round(static_cast<float>(newInitialWidth) / videoAspectRatio)); }

                glfwSetWindowSize(m_window, newInitialWidth, newInitialHeight);
                m_storedWindowedPosX = (mode->width - newInitialWidth) / 2; m_storedWindowedPosY = (mode->height - newInitialHeight) / 2;
                glfwSetWindowPos(m_window, m_storedWindowedPosX, m_storedWindowedPosY);
                m_storedWindowedWidth = newInitialWidth; m_storedWindowedHeight = newInitialHeight;
                m_windowWidth = newInitialWidth; m_windowHeight = newInitialHeight;
                m_framebufferResized = true;
            } else {
                 if (newInitialWidth > 100 && newInitialHeight > 100) {
                    glfwSetWindowSize(m_window, newInitialWidth, newInitialHeight);
                    m_storedWindowedWidth = newInitialWidth; m_storedWindowedHeight = newInitialHeight;
                    m_windowWidth = newInitialWidth; m_windowHeight = newInitialHeight;
                    m_framebufferResized = true;
                 }
            }
            LogToFile(std::string("[App::loadFileAtIndex] Window resized to ") + std::to_string(newInitialWidth) + "x" + std::to_string(newInitialHeight));
            std::cout << "[App::loadFileAtIndex] Window resized to " << newInitialWidth << "x" << newInitialHeight << std::endl;
        }
    }

    if (!m_playbackController) { m_playbackController = std::make_unique<PlaybackController>(); }
    m_playbackStartTime = std::chrono::steady_clock::now();
    m_pauseBegan = m_playbackStartTime;
    if (m_decoderWrapper && m_decoderWrapper->getDecoder()) {
        const auto& frames = m_decoderWrapper->getDecoder()->getFrames();
        nlohmann::json firstFrameMetaForPB;
        if (!frames.empty()) {
            std::vector<uint16_t> dummyPixelData;
            try { m_decoderWrapper->getDecoder()->loadFrame(frames.front(), dummyPixelData, firstFrameMetaForPB); }
            catch(const std::exception&) { firstFrameMetaForPB["timestamp"] = std::to_string(frames.front()); }
        }
        m_playbackController->processNewSegment(firstFrameMetaForPB, frames.size(), m_playbackStartTime);
    } else {
        m_playbackController->processNewSegment({}, 0, m_playbackStartTime);
    }
    LogToFile("[App::loadFileAtIndex] PlaybackController processed new segment.");
    std::cout << "[App::loadFileAtIndex] PlaybackController processed new segment." << std::endl;


    if (m_decoderWrapper && m_decoderWrapper->getDecoder() && m_audio) {
        auto& decoder_ref = *m_decoderWrapper->getDecoder();
        auto& audio_loader_ref = decoder_ref.loadAudio();
        const auto& video_frames = decoder_ref.getFrames();
        int64_t firstVideoFrameTimestampNs = video_frames.empty() ? 0 : video_frames.front();
        m_audio->setForceMute(false);
        m_audio->reset(&audio_loader_ref, firstVideoFrameTimestampNs);
        LogToFile("[App::loadFileAtIndex] Audio reset.");
        std::cout << "[App::loadFileAtIndex] Audio reset." << std::endl;
    }

    if (m_rendererVk) {
        m_rendererVk->resetPanOffsets();
        m_rendererVk->resetDimensions();
        LogToFile("[App::loadFileAtIndex] Renderer_VK pan/dimensions reset.");
        std::cout << "[App::loadFileAtIndex] Renderer_VK pan/dimensions reset." << std::endl;
    }
    if (m_playbackController && m_audio) { m_audio->setPaused(m_playbackController->isPaused()); }
    LogToFile("[App::loadFileAtIndex] File loading complete.");
    std::cout << "[App::loadFileAtIndex] File loading complete." << std::endl;
}


void App::handleKey(int key, int mods) {
    if (!m_window || !m_playbackController) return;
    if (key == GLFW_KEY_TAB && mods == 0) { m_showUI = !m_showUI; return; }
    if (key == GLFW_KEY_M && mods == 0) { m_showMetrics = !m_showMetrics; return; }
    if ((key == GLFW_KEY_H && mods == 0) || key == GLFW_KEY_F1) { toggleHelpPage(); if (m_showHelpPage) GuiOverlay::show_playlist_aux = false; return; }
    if (key == GLFW_KEY_Q && (mods & GLFW_MOD_CONTROL)) { glfwSetWindowShouldClose(m_window, GLFW_TRUE); return; }
    if (key == GLFW_KEY_O && (mods & GLFW_MOD_CONTROL)) { triggerOpenFileViaDialog(); return; }
    bool keyHandledByApp = true; size_t totalFramesInCurrentFile = 0;
    if (m_decoderWrapper && m_decoderWrapper->getDecoder()) { totalFramesInCurrentFile = m_decoderWrapper->getDecoder()->getFrames().size(); }
    if (key == GLFW_KEY_LEFT_BRACKET) { if (!m_fileList.empty()) loadFileAtIndex((m_currentFileIndex - 1 + static_cast<int>(m_fileList.size())) % static_cast<int>(m_fileList.size())); }
    else if (key == GLFW_KEY_RIGHT_BRACKET) { if (!m_fileList.empty()) loadFileAtIndex((m_currentFileIndex + 1) % static_cast<int>(m_fileList.size())); }
    else if (key == GLFW_KEY_LEFT) { if (!m_playbackController->isPaused()) m_playbackController->togglePause(); m_playbackController->stepBackward(totalFramesInCurrentFile); anchorPlaybackTimeForResume(); }
    else if (key == GLFW_KEY_RIGHT) { if (!m_playbackController->isPaused()) m_playbackController->togglePause(); m_playbackController->stepForward(totalFramesInCurrentFile); anchorPlaybackTimeForResume(); }
    else if (key == GLFW_KEY_HOME) { if (totalFramesInCurrentFile > 0) { if (!m_playbackController->isPaused()) m_playbackController->togglePause(); m_playbackController->seekFrame(0, totalFramesInCurrentFile); anchorPlaybackTimeForResume(); }}
    else if (key == GLFW_KEY_END) { if (totalFramesInCurrentFile > 0) { if (!m_playbackController->isPaused()) m_playbackController->togglePause(); m_playbackController->seekFrame(totalFramesInCurrentFile - 1, totalFramesInCurrentFile); anchorPlaybackTimeForResume(); }}
    else if (key == GLFW_KEY_Z) {
        m_playbackController->toggleZoomNativePixels();
        if (m_rendererVk) {
            bool zoomOn = m_playbackController->isZoomNativePixels();
            m_rendererVk->setZoomNativePixels(zoomOn);
            if (!zoomOn) {
                m_rendererVk->resetPanOffsets();
                if (m_isPanning) { m_isPanning = false; }
            } else {
                int imgW = m_rendererVk->getImageWidth();
                int imgH = m_rendererVk->getImageHeight();
                int winW = 0, winH = 0;
                glfwGetFramebufferSize(m_window, &winW, &winH);
                float offsetX = (winW - imgW) / 2.0f;
                float offsetY = (winH - imgH) / 2.0f;
                m_rendererVk->setPanOffsets(offsetX, offsetY);
            }
        }
    }
    else if (key == GLFW_KEY_0 || key == GLFW_KEY_KP_0) { m_cfaOverride = std::nullopt; }
    else if (key >= GLFW_KEY_1 && key <= GLFW_KEY_4) { m_cfaOverride = key - GLFW_KEY_1; }
    else if (key == GLFW_KEY_F || key == GLFW_KEY_F11) {
        if (m_isFullscreen) {
            glfwSetWindowMonitor(m_window, nullptr, m_storedWindowedPosX, m_storedWindowedPosY, m_storedWindowedWidth, m_storedWindowedHeight, 0);
            m_isFullscreen = false;
        } else {
            GLFWmonitor* monitor = glfwGetPrimaryMonitor(); if (monitor) {
                const GLFWvidmode* mode = glfwGetVideoMode(monitor); if (mode) {
                    glfwGetWindowPos(m_window, &m_storedWindowedPosX, &m_storedWindowedPosY); glfwGetWindowSize(m_window, &m_storedWindowedWidth, &m_storedWindowedHeight);
                    glfwSetWindowMonitor(m_window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate); m_isFullscreen = true;
                }
            }
        }
        m_framebufferResized = true;
    }
    else if (key == GLFW_KEY_ESCAPE) { if (m_isFullscreen) { glfwSetWindowMonitor(m_window, nullptr, m_storedWindowedPosX, m_storedWindowedPosY, m_storedWindowedWidth, m_storedWindowedHeight, 0); m_isFullscreen = false; m_framebufferResized = true; } else if(m_showHelpPage) {m_showHelpPage = false;} else if (GuiOverlay::show_playlist_aux) {GuiOverlay::show_playlist_aux = false;} else { glfwSetWindowShouldClose(m_window, GLFW_TRUE); } }
    else if (key == GLFW_KEY_DELETE || key == GLFW_KEY_BACKSPACE) { softDeleteCurrentFile(); }
    else { keyHandledByApp = false; }
    bool wasPausedBeforePbController = m_playbackController->isPaused(); if (!keyHandledByApp) { m_playbackController->handleKey(key, m_window); }
    bool isPausedAfterPbController = m_playbackController->isPaused(); if (isPausedAfterPbController != wasPausedBeforePbController) { if (m_audio) m_audio->setPaused(isPausedAfterPbController); if (isPausedAfterPbController) { recordPauseTime(); } else { anchorPlaybackTimeForResume(); } }
}

void App::handleMouseButton(int button, int action, int mods) {
    if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) {
        if (m_firstFileLoaded) {
            double cx, cy; glfwGetCursorPos(m_window, &cx, &cy);
            GuiOverlay::requestContextMenu(static_cast<float>(cx), static_cast<float>(cy));
        }
        return;
    }
    if (!m_rendererVk || !m_playbackController || !m_playbackController->isZoomNativePixels()) { if(m_isPanning){ m_isPanning=false; } return; }
    if (button == GLFW_MOUSE_BUTTON_LEFT) { if (action == GLFW_PRESS) { m_isPanning = true; glfwGetCursorPos(m_window, &m_lastMouseX, &m_lastMouseY); } else if (action == GLFW_RELEASE) { if (m_isPanning) { m_isPanning = false; } } }
}
void App::handleCursorPos(double xpos, double ypos) {
    if (m_isPanning && m_rendererVk && m_playbackController && m_playbackController->isZoomNativePixels()) {
        double dx = xpos - m_lastMouseX; double dy = ypos - m_lastMouseY;
        m_rendererVk->setPanOffsets(m_rendererVk->getPanX() + static_cast<float>(dx), m_rendererVk->getPanY() + static_cast<float>(dy));
        m_lastMouseX = xpos; m_lastMouseY = ypos;
    } else if(m_isPanning) {
        m_isPanning = false;
    }
}
std::string App::openMcrawDialog() {
#if defined(_WIN32)
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    std::string path;
    if (SUCCEEDED(hr)) {
        IFileOpenDialog* dlg = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, (void**)&dlg))) {
            const COMDLG_FILTERSPEC filter[] = { { L"MotionCam RAW", L"*.mcraw" } };
            dlg->SetFileTypes(1, filter);
            HWND owner = NULL;
            if (m_window) owner = glfwGetWin32Window(m_window);
            if (SUCCEEDED(dlg->Show(owner))) {
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
                    if (item) item->Release();
                }
            }
            if (dlg) dlg->Release();
        }
        CoUninitialize();
    }
    return path;
#elif defined(__APPLE__)
    const char* cmd = "osascript -e 'set f to choose file of type {\"mcraw\"} with prompt \"Select MotionCam RAW file\"' -e 'POSIX path of f'";
    FILE* pipe = popen(cmd, "r");
    if (!pipe) return {};
    char buf[1024];
    std::string path;
    if (fgets(buf, sizeof(buf), pipe)) {
        path = buf;
        if (!path.empty() && path.back() == '\n') path.pop_back();
    }
    int status = pclose(pipe);
    if (status == -1 || (WIFEXITED(status) && WEXITSTATUS(status) != 0)) {
        path.clear();
    }
    return path;
#else
    std::cerr << "File dialog not implemented for this platform. Drag-and-drop or use command line." << std::endl;
    return {};
#endif
}
void App::triggerOpenFileViaDialog() {
    std::string newPath = openMcrawDialog();
    if (!newPath.empty()) {
        fs::path absPath = fs::absolute(newPath);
        fs::path dir = absPath.parent_path();
        bool modified = false;
        try {
            for (const auto& e : fs::directory_iterator(dir)) {
                if (e.is_regular_file() && e.path().extension() == ".mcraw") {
                    std::string s = e.path().string();
                    if (std::find(m_fileList.begin(), m_fileList.end(), s) == m_fileList.end()) {
                        m_fileList.push_back(s);
                        modified = true;
                    }
                }
            }
        } catch (const fs::filesystem_error&) {}
        if (modified) std::sort(m_fileList.begin(), m_fileList.end());
        auto it_existing = std::find(m_fileList.begin(), m_fileList.end(), absPath.string());
        if (it_existing != m_fileList.end()) {
            m_firstFileLoaded = false;
            loadFileAtIndex(static_cast<int>(std::distance(m_fileList.begin(), it_existing)));
            m_firstFileLoaded = true;
        }
    }
}
void App::recordPauseTime() { m_pauseBegan = std::chrono::steady_clock::now(); }
void App::anchorPlaybackTimeForResume() {
    const std::vector<int64_t>* pFrames = nullptr;
    if (m_decoderWrapper && m_decoderWrapper->getDecoder()) pFrames = &m_decoderWrapper->getDecoder()->getFrames();
    std::optional<int64_t> curVideoTs, firstVideoTs;
    if (m_playbackController && pFrames && !pFrames->empty()) {
        curVideoTs = m_playbackController->getCurrentFrameMediaTimestamp(*pFrames);
        firstVideoTs = m_playbackController->getFirstFrameMediaTimestampOfSegment();
    }
    if (curVideoTs && firstVideoTs) {
        int64_t deltaNs = *curVideoTs - *firstVideoTs; if (deltaNs < 0) deltaNs = 0;
        m_playbackStartTime = std::chrono::steady_clock::now() - std::chrono::nanoseconds(deltaNs);
    } else {
        m_playbackStartTime += (std::chrono::steady_clock::now() - m_pauseBegan);
    }
    if (m_playbackController) m_playbackController->setWallClockAnchorForSegment(m_playbackStartTime);
    if (m_audio && m_decoderWrapper && m_decoderWrapper->getDecoder() && curVideoTs) {
        auto* freshLoader = m_decoderWrapper->makeFreshAudioLoader();
        m_audio->reset(freshLoader, *curVideoTs);
        m_audio->setPaused(m_playbackController->isPaused());
    }
}
void App::handleDrop(int count, const char** paths) {
    if (count <= 0) return;
    std::string firstPath;
    fs::path scanDir;
    for (int i = 0; i < count; ++i) {
        if (!paths[i]) continue;
        try {
            fs::path p = fs::absolute(paths[i]);
            if (p.extension() == ".mcraw" && fs::is_regular_file(p)) {
                if (firstPath.empty()) {
                    firstPath = p.string();
                    scanDir = p.parent_path();
                }
            }
        } catch (const fs::filesystem_error&) {}
    }
    bool modified = false;
    if (!firstPath.empty()) {
        try {
            for (const auto& e : fs::directory_iterator(scanDir)) {
                if (e.is_regular_file() && e.path().extension() == ".mcraw") {
                    std::string s = e.path().string();
                    if (std::find(m_fileList.begin(), m_fileList.end(), s) == m_fileList.end()) {
                        m_fileList.push_back(s);
                        modified = true;
                    }
                }
            }
        } catch (const fs::filesystem_error&) {}
        if (modified) std::sort(m_fileList.begin(), m_fileList.end());
        auto it = std::find(m_fileList.begin(), m_fileList.end(), firstPath);
        if (it != m_fileList.end()) {
            m_firstFileLoaded = false;
            loadFileAtIndex(static_cast<int>(std::distance(m_fileList.begin(), it)));
            m_firstFileLoaded = true;
        }
    }
}

void App::softDeleteCurrentFile() {
    if (m_fileList.empty() || m_currentFileIndex < 0 || static_cast<size_t>(m_currentFileIndex) >= m_fileList.size()) return;
    fs::path currentFilePath = m_fileList[m_currentFileIndex];
    if(m_playbackController) { if(!m_playbackController->isPaused()) { handleKey(GLFW_KEY_SPACE, 0); } }
    m_decoderWrapper.reset(); if (m_audio) { m_audio->setForceMute(true); m_audio->reset(nullptr, 0); }
    fs::path folder = currentFilePath.parent_path(); fs::path deletedFolder = folder / "_deleted";
    try {
        if (!fs::exists(deletedFolder)) { fs::create_directory(deletedFolder); }
        fs::path destinationPath = deletedFolder / currentFilePath.filename();
        if (fs::exists(destinationPath)) { std::string base = destinationPath.stem().string(); std::string ext = destinationPath.extension().string(); int counter = 1; while(fs::exists(destinationPath)) { destinationPath = deletedFolder / (base + "_" + std::to_string(counter++) + ext); } }
        fs::rename(currentFilePath, destinationPath); std::cout << "Moved " << currentFilePath << " to " << destinationPath << std::endl;
        m_fileList.erase(m_fileList.begin() + m_currentFileIndex);
        if (m_fileList.empty()) { std::cout << "Playlist empty after delete." << std::endl; if(m_window) glfwSetWindowShouldClose(m_window, GLFW_TRUE); return; }
        if (static_cast<size_t>(m_currentFileIndex) >= m_fileList.size()) { m_currentFileIndex = static_cast<int>(m_fileList.size()) - 1; }
        if (m_currentFileIndex < 0 && !m_fileList.empty()) m_currentFileIndex = 0;
        m_firstFileLoaded = false; loadFileAtIndex(m_currentFileIndex); m_firstFileLoaded = true;
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Error during soft delete: " << e.what() << std::endl;
        std::string originalAnchorFilePath = this->m_filePath;
        if (m_currentFileIndex >= 0 && static_cast<size_t>(m_currentFileIndex) < m_fileList.size() && fs::exists(m_fileList[m_currentFileIndex])) {
             originalAnchorFilePath = m_fileList[m_currentFileIndex];
        } else if (!m_fileList.empty() && fs::exists(m_fileList[0])) {
             originalAnchorFilePath = m_fileList[0];
        }
        m_fileList.clear(); fs::path anchorPathFs = fs::absolute(originalAnchorFilePath); fs::path parent_folder_of_anchor = anchorPathFs.parent_path();
        if (!fs::exists(parent_folder_of_anchor)) parent_folder_of_anchor = fs::current_path();
        for (const auto& entry : fs::directory_iterator(parent_folder_of_anchor)) { if (entry.is_regular_file() && entry.path().extension() == ".mcraw") { m_fileList.push_back(entry.path().string()); } }
        std::sort(m_fileList.begin(), m_fileList.end()); auto it = std::find(m_fileList.begin(), m_fileList.end(), anchorPathFs.string());
        if (it != m_fileList.end()) { m_currentFileIndex = static_cast<int>(std::distance(m_fileList.begin(), it)); }
        else if (!m_fileList.empty()) { m_currentFileIndex = 0; }
        else { if(m_window) glfwSetWindowShouldClose(m_window, GLFW_TRUE); return; }
        m_firstFileLoaded = false; loadFileAtIndex(m_currentFileIndex); m_firstFileLoaded = true;
    }
}
void App::saveCurrentFrameAsDng() {
    if (!m_decoderWrapper || !m_decoderWrapper->getDecoder() || !m_playbackController || m_fileList.empty()) { std::cerr << "DNG Save: No file loaded, decoder not ready, or playback controller missing." << std::endl; return; }
    std::string currentMcrawPathStr = m_fileList[m_currentFileIndex]; fs::path currentMcrawPath = currentMcrawPathStr; fs::path dngOutputDir = currentMcrawPath.parent_path() / (currentMcrawPath.stem().string() + "_DNG_Current");
    try { fs::create_directories(dngOutputDir); }
    catch (const fs::filesystem_error& e) { std::cerr << "DNG Save: Failed to create output directory " << dngOutputDir << ": " << e.what() << std::endl; return; }
    size_t currentIndex = m_playbackController->getCurrentFrameIndex(); const auto& frameTimestamps = m_decoderWrapper->getDecoder()->getFrames();
    if (currentIndex >= frameTimestamps.size()) { std::cerr << "DNG Save: Current frame index out of bounds." << std::endl; return; }
    motioncam::Timestamp ts = frameTimestamps[currentIndex]; std::vector<uint16_t> rawFrameData; nlohmann::json frameMetadata;
    try { m_decoderWrapper->getDecoder()->loadFrame(ts, rawFrameData, frameMetadata); }
    catch (const std::exception& e) { std::cerr << "DNG Save: Failed to load frame " << currentIndex << ": " << e.what() << std::endl; return; }
    const auto& containerMetadata = m_decoderWrapper->getContainerMetadata(); char dngFilename[256];
    snprintf(dngFilename, sizeof(dngFilename), "frame_%06zu_ts_%lld.dng", currentIndex, static_cast<long long>(ts));
    fs::path outputDngPath = dngOutputDir / dngFilename; std::string errorMsg;
    std::cout << "DNG Save: Attempting to save current frame to " << outputDngPath << std::endl;
    if (writeDngInternal(outputDngPath.string(), rawFrameData, frameMetadata, containerMetadata, errorMsg)) { std::cout << "DNG Save: Successfully saved " << outputDngPath << std::endl; }
    else { std::cerr << "DNG Save: Failed to write DNG " << outputDngPath << ": " << errorMsg << std::endl; }
}
void App::convertCurrentFileToDngs() {
    if (!m_decoderWrapper || !m_decoderWrapper->getDecoder() || m_fileList.empty()) { std::cerr << "DNG Export All: No file loaded or decoder not ready." << std::endl; return; }
    std::string currentMcrawPathStr = m_fileList[m_currentFileIndex]; fs::path currentMcrawPath = currentMcrawPathStr; fs::path dngOutputDir = currentMcrawPath.parent_path() / (currentMcrawPath.stem().string() + "_DNG_All");
    try { fs::create_directories(dngOutputDir); }
    catch (const fs::filesystem_error& e) { std::cerr << "DNG Export All: Failed to create output directory " << dngOutputDir << ": " << e.what() << std::endl; return; }
    const auto& frameTimestamps = m_decoderWrapper->getDecoder()->getFrames(); const auto& containerMetadata = m_decoderWrapper->getContainerMetadata();
    std::cout << "DNG Export All: Starting DNG conversion for " << frameTimestamps.size() << " frames to " << dngOutputDir << std::endl;
    bool wasPausedOriginalState = m_playbackController ? m_playbackController->isPaused() : true;
    if (m_playbackController && !wasPausedOriginalState) { m_playbackController->togglePause(); if(m_playbackController->isPaused()) recordPauseTime(); }
    for (size_t i = 0; i < frameTimestamps.size(); ++i) {
        motioncam::Timestamp ts = frameTimestamps[i]; std::vector<uint16_t> rawFrameData; nlohmann::json frameMetadata;
        try {
            m_decoderWrapper->getDecoder()->loadFrame(ts, rawFrameData, frameMetadata); char dngFilename[256];
            snprintf(dngFilename, sizeof(dngFilename), "frame_%06zu_ts_%lld.dng", i, static_cast<long long>(ts));
            fs::path outputDngPath = dngOutputDir / dngFilename; std::string errorMsg;
            bool success = writeDngInternal(outputDngPath.string(), rawFrameData, frameMetadata, containerMetadata, errorMsg);
            if (!success) { std::cerr << "DNG Export All: Failed to write DNG " << outputDngPath << ": " << errorMsg << std::endl; }
            else { if ((i+1) % 20 == 0 || i == frameTimestamps.size() -1) { std::cout << "DNG Export All: Converted " << (i+1) << "/" << frameTimestamps.size() << " frames." << std::endl; } }
        }
        catch (const std::exception& e) { std::cerr << "DNG Export All: Error processing frame " << i << " (ts: " << ts << "): " << e.what() << std::endl; }
    }
    std::cout << "DNG Export All: Conversion complete." << std::endl;
    if (m_playbackController && m_playbackController->isPaused() && !wasPausedOriginalState) { m_playbackController->togglePause(); if(!m_playbackController->isPaused()) anchorPlaybackTimeForResume(); }
}

void App::sendCurrentFileToMotionCamFuse() {
#ifdef __APPLE__
    if (m_fileList.empty() || m_currentFileIndex < 0 || static_cast<size_t>(m_currentFileIndex) >= m_fileList.size()) {
        std::cerr << "MotionCam Fuse: No current file." << std::endl;
        return;
    }
    std::string cmd = std::string("/usr/bin/open -na \"MotionCam Fuse\" --args -f \"") + m_fileList[m_currentFileIndex] + "\"";
    int ret = std::system(cmd.c_str());
    if (ret != 0) {
        std::cerr << "MotionCam Fuse: launch failed." << std::endl;
    }
#else
    std::cerr << "MotionCam Fuse integration is only available on macOS." << std::endl;
#endif
}

void App::sendPlaylistToMotionCamFuse() {
#ifdef __APPLE__
    if (m_fileList.empty()) {
        std::cerr << "MotionCam Fuse: Playlist empty." << std::endl;
        return;
    }
    std::string cmd = "/usr/bin/open -na \"MotionCam Fuse\" --args";
    for (const auto& f : m_fileList) {
        cmd += " -f \"" + f + "\"";
    }
    int ret = std::system(cmd.c_str());
    if (ret != 0) {
        std::cerr << "MotionCam Fuse: launch failed." << std::endl;
    }
#else
    std::cerr << "MotionCam Fuse integration is only available on macOS." << std::endl;
#endif
}
