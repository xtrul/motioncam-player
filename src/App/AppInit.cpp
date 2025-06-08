// FILE: src/App/AppInit.cpp
#include "App/App.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define GLFW_EXPOSE_NATIVE_WIN32
#include <windows.h>
#include <GLFW/glfw3native.h> 
#include <shlwapi.h>
#include <dwmapi.h>
#include <commdlg.h>
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "comdlg32.lib")
#endif

#include "Audio/AudioController.h"
#include "Decoder/DecoderWrapper.h"
#include <motioncam/Decoder.hpp>

#include "Playback/PlaybackController.h"
#include "Graphics/Renderer_VK.h"
#include "Utils/DebugLog.h"
#include "Utils/RawFrameBuffer.h"

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
#include <atomic>
#include <condition_variable>
#include <optional>

namespace fs = std::filesystem;

#ifdef _WIN32
namespace DebugLogHelper {
    std::string wstring_to_utf8(const std::wstring& wstr) {
        if (wstr.empty()) return std::string();
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
        if (size_needed <= 0) return std::string();
        std::string strTo(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
        return strTo;
    }
}
#endif

const std::vector<const char*> g_validationLayers_AppInit = {
    "VK_LAYER_KHRONOS_validation"
};
const std::vector<const char*> g_deviceExtensions_AppInit = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
    #ifdef __APPLE__
    , "VK_KHR_portability_subset"
    #endif
};

#define VK_APP_CHECK(x)                                                 \
    do {                                                                \
        VkResult err = x;                                               \
        if (err) {                                                      \
            std::string error_str = std::string("[VULKAN CHECK FAILED IN APP INIT] Error: ") + std::to_string(err) + " (" #x ") at " __FILE__ ":" + std::to_string(__LINE__); \
            LogToFile(error_str);                                       \
            std::cerr << error_str << std::endl;                        \
            throw std::runtime_error("Vulkan API call failed in AppInit context!"); \
        }                                                               \
    } while (0)


static void glfw_error_callback_AppInit(int error, const char* description) {
    std::string msg = "[GLFW Error CB - AppInit] " + std::to_string(error) + ": " + description;
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


App::App(const std::string& filePath) :
    m_filePath(filePath),
    m_window(nullptr),
    m_playbackController_ptr(nullptr),
    m_decoderWrapper_ptr(nullptr),
    m_windowWidth(1280), m_windowHeight(720),
    m_storedWindowedPosX(100), m_storedWindowedPosY(100),
    m_storedWindowedWidth(1280), m_storedWindowedHeight(720),
    m_framebufferResized(false),
    m_vkInstance(VK_NULL_HANDLE), m_debugMessenger(VK_NULL_HANDLE), m_surface(VK_NULL_HANDLE),
    m_physicalDevice(VK_NULL_HANDLE), m_device(VK_NULL_HANDLE), m_vmaAllocator(VK_NULL_HANDLE),
    m_graphicsQueue(VK_NULL_HANDLE), m_presentQueue(VK_NULL_HANDLE),
    m_swapChain(VK_NULL_HANDLE),
    m_renderPass(VK_NULL_HANDLE), m_commandPool(VK_NULL_HANDLE),
    m_currentFrame(0), m_imguiDescriptorPool(VK_NULL_HANDLE),
    m_isFullscreen(false), m_cfaTypeFromMetadata(0), m_staticBlack(0.0), m_staticWhite(65535.0),
    m_dumpMetadata(false), m_currentFileIndex(-1),
    m_showMetrics(false), m_showHelpPage(false),
    m_gpuWaitTimeMs(0.0), m_decodeTimeMs(0.0),
    m_renderPrepTimeMs(0.0), m_guiRenderTimeMs(0.0), m_vkSubmitPresentTimeMs(0.0),
    m_appLogicTimeMs(0.0), m_sleepTimeMs(0.0), m_totalLoopTimeMs(0.0),
    m_decodedWidth(0), m_decodedHeight(0),
    m_showUI(true), m_isPanning(false), m_lastMouseX(0.0), m_lastMouseY(0.0),
    m_firstFileLoaded(false),
    m_lastTitleUpdateTime(std::chrono::steady_clock::time_point::min()),
#ifdef _WIN32
    _ipcWnd(nullptr),
#endif
    m_threadsShouldStop(false),
    m_ioThreadFileChanged(false)
{
    LogToFile(std::string("App::App Constructor called for file: ") + this->m_filePath);
#ifndef NDEBUG
    std::cout << "App::App Constructor called for file: " << this->m_filePath << std::endl;
#endif

    LogToFile(std::string("App::App Effective kNumPersistentStagingBuffers: ") + std::to_string(kNumPersistentStagingBuffers));
    LogToFile(std::string("App::App GpuUploadQueueCapacity (static const): ") + std::to_string(GpuUploadQueueCapacity));
    LogToFile(std::string("App::App Decode Queue OLD CALC (kNumPersistentStagingBuffers * DecodeQueueCapacityMultiplier): ") + std::to_string(kNumPersistentStagingBuffers * DecodeQueueCapacityMultiplier));
    LogToFile(std::string("App::App Available Staging Indices OLD CALC (kNumPersistentStagingBuffers + Slack): ") + std::to_string(kNumPersistentStagingBuffers + AvailableStagingIndicesQueueSlack));

    LogToFile(std::string("App::App GPU Upload Queue MaxSize (actual from queue): ") + std::to_string(m_gpuUploadQueue.get_max_size_debug()));
    LogToFile(std::string("App::App Decode Queue MaxSize (actual from queue): ") + std::to_string(m_decodeQueue.get_max_size_debug()));
    LogToFile(std::string("App::App Available Staging Buffer Indices Queue MaxSize (actual from queue): ") + std::to_string(m_availableStagingBufferIndices.get_max_size_debug()));


    if (!fs::exists(this->m_filePath)) {
        LogToFile(std::string("App::App ERROR: File does not exist: ") + this->m_filePath);
        throw std::runtime_error("App::App File does not exist: " + this->m_filePath);
    }
    auto target = fs::absolute(this->m_filePath);
    auto folder = target.parent_path();
    for (const auto& e : fs::directory_iterator(folder)) {
        if (e.is_regular_file() && e.path().extension() == ".mcraw") {
            m_fileList.push_back(e.path().string());
        }
    }
    if (!m_fileList.empty()) {
        std::sort(m_fileList.begin(), m_fileList.end());
    }

    auto it = std::find(m_fileList.begin(), m_fileList.end(), target.string());
    if (it == m_fileList.end()) {
        LogToFile("App::App Initial file not found in directory scan, adding it to list: " + target.string());
        m_fileList.push_back(target.string());
        std::sort(m_fileList.begin(), m_fileList.end());
        it = std::find(m_fileList.begin(), m_fileList.end(), target.string());
        if (it == m_fileList.end()) {
            LogToFile(std::string("App::App ERROR: Catastrophic: Initial file still not in playlist after adding: ") + this->m_filePath);
            throw std::runtime_error("App::App Catastrophic: Initial file not in playlist: " + this->m_filePath);
        }
    }
    m_currentFileIndex = static_cast<int>(std::distance(m_fileList.begin(), it));

    m_inFlightStagingBufferIndices.resize(MAX_FRAMES_IN_FLIGHT, std::nullopt);

    LogToFile(std::string("App::App Constructor section 1 finished. Current file index: ") + std::to_string(m_currentFileIndex) +
        ". Persistent Staging Buffers planned: " + std::to_string(kNumPersistentStagingBuffers));

    m_audio = std::make_unique<AudioController>();
    LogToFile("App::App constr AudioController created. Initializing audio...");
    if (!m_audio->init()) {
        LogToFile("App::App constr ERROR: Failed to initialize audio!");
        std::cerr << "App::App constr Failed to initialize audio!" << std::endl;
    }
    else {
        LogToFile("App::App constr Audio initialized.");
    }

    LogToFile("App::App constr Initializing Vulkan...");
    if (!this->initVulkan()) {
        LogToFile("App::App constr ERROR: initVulkan() failed. Aborting constructor.");
        throw std::runtime_error("Vulkan initialization failed in App constructor.");
    }
    LogToFile("App::App constr Vulkan initialized by initVulkan().");

    if (m_fileList.empty()) {
        LogToFile("App::App constr ERROR: File list empty after init. Aborting constructor.");
        throw std::runtime_error("File list empty after App initialization.");
    }

    LogToFile("App::App constr Creating Renderer_VK...");
    m_rendererVk = std::make_unique<Renderer_VK>(m_physicalDevice, m_device, m_vmaAllocator, m_graphicsQueue, m_commandPool);
    if (!m_rendererVk->init(m_renderPass, static_cast<uint32_t>(m_swapChainImages.size()))) {
        LogToFile("App::App constr ERROR: Failed to initialize Renderer_VK. Aborting constructor.");
        throw std::runtime_error("Failed to initialize Renderer_VK in App constructor.");
    }
    LogToFile("App::App constr Renderer_VK initialized.");

    LogToFile("App::App constr Initializing ImGui Vulkan...");
    this->initImGuiVulkan();
    LogToFile("App::App constr ImGui Vulkan initialized.");

    m_playbackController = std::make_unique<PlaybackController>();
    m_playbackController_ptr = m_playbackController.get();
    LogToFile("App::App constr PlaybackController created.");

    LogToFile("App::App constr Loading initial file...");
    this->loadFileAtIndex(m_currentFileIndex);
    m_firstFileLoaded = true;
    LogToFile("App::App constr Initial file load process initiated.");
    LogToFile(std::string("App::App Constructor fully finished. Current file index: ") + std::to_string(m_currentFileIndex));

#ifndef NDEBUG
    std::cout << "App::App Constructor finished. Current file index: " << m_currentFileIndex
        << ". Staging Buffers: " << kNumPersistentStagingBuffers << std::endl;
#endif
}

VkResult CreateDebugUtilsMessengerEXT_Helper(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger) {
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr) {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    }
    else {
        LogToFile("CreateDebugUtilsMessengerEXT_Helper ERROR: vkCreateDebugUtilsMessengerEXT function not found!");
        std::cerr << "CreateDebugUtilsMessengerEXT_Helper vkCreateDebugUtilsMessengerEXT function not found!" << std::endl;
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}
void DestroyDebugUtilsMessengerEXT_Helper(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator) {
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr) {
        func(instance, debugMessenger, pAllocator);
    }
    else {
        LogToFile("DestroyDebugUtilsMessengerEXT_Helper ERROR: vkDestroyDebugUtilsMessengerEXT function not found!");
        std::cerr << "DestroyDebugUtilsMessengerEXT_Helper vkDestroyDebugUtilsMessengerEXT function not found!" << std::endl;
    }
}

bool App::initVulkan() {
    LogToFile("App::initVulkan Starting Vulkan initialization (core).");
    glfwSetErrorCallback(glfw_error_callback_AppInit);
    if (!glfwInit()) {
        LogToFile("App::initVulkan ERROR: Failed to initialize GLFW");
        return false;
    }
    LogToFile("App::initVulkan GLFW initialized.");

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    m_window = glfwCreateWindow(m_windowWidth, m_windowHeight, "MotionCam Player", nullptr, nullptr);
    if (!m_window) {
        LogToFile("App::initVulkan ERROR: Failed to create GLFW window");
        glfwTerminate();
        return false;
    }
    LogToFile("App::initVulkan GLFW window created.");
    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, framebuffer_size_callback_static);
    glfwSetKeyCallback(m_window, key_callback_static);
    glfwSetDropCallback(m_window, drop_callback_static);
    glfwSetMouseButtonCallback(m_window, mouse_button_callback_static);
    glfwSetCursorPosCallback(m_window, cursor_pos_callback_static);

#ifdef _WIN32
    {
        HWND hwnd = glfwGetWin32Window(m_window);
        BOOL useDark = TRUE;
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(useDark));
        HICON hIcon = (HICON)LoadImageW(GetModuleHandleW(NULL), L"IDI_APPICON", IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);
        if (hIcon) SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
        HICON hIconSmall = (HICON)LoadImageW(GetModuleHandleW(NULL), L"IDI_APPICON", IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);
        if (hIconSmall) SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSmall);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }
#endif

#ifdef _WIN32
    {
        LogToFile("App::initVulkan Creating IPC message-only window.");
        const wchar_t* ipcClassName = L"MOTIONCAM_PLAYER_IPC_WND_CLASS";
        WNDCLASSW wc{};
        wc.lpfnWndProc = App::IpcWndProcStatic;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = ipcClassName;
        if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            LogToFile(std::string("App::initVulkan ERROR: RegisterClassW for IPC window failed. Error: ") + std::to_string(GetLastError()));
        }
        else {
            LogToFile("App::initVulkan IPC Window class registered or already exists.");
        }
        _ipcWnd = CreateWindowExW(0, ipcClassName, L"MOTIONCAM_PLAYER_IPC_HIDDEN_WINDOW", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), this);
        if (!_ipcWnd) LogToFile(std::string("App::initVulkan ERROR: CreateWindowExW for IPC window failed. Error: ") + std::to_string(GetLastError()));
        else LogToFile("App::initVulkan IPC message-only window created successfully.");
    }
#endif

    try {
        createInstance();
        setupDebugMessenger();
        createSurface();
        pickPhysicalDevice();
        createLogicalDevice();
        createSwapChain();
        createImageViews();
        createRenderPass();
        createCommandPool();
        createFramebuffers();
        createCommandBuffers();
        createSyncObjects();
        createPersistentStagingBuffers();
    }
    catch (const std::exception& e) {
        LogToFile(std::string("App::initVulkan ERROR: Vulkan initialization failed during setup: ") + e.what());
        return false;
    }
    LogToFile("App::initVulkan Vulkan core initialization complete.");
    return true;
}

void App::createInstance() {
    LogToFile("App::createInstance Start.");
    if (enableValidationLayers) {
        uint32_t layerCount;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
        std::vector<VkLayerProperties> availableLayers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());
        for (const char* layerName : g_validationLayers_AppInit) {
            bool layerFound = false;
            for (const auto& layerProperties : availableLayers) {
                if (strcmp(layerName, layerProperties.layerName) == 0) {
                    layerFound = true; break;
                }
            }
            if (!layerFound) throw std::runtime_error(std::string("Validation layer requested, but not available: ") + layerName);
        }
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
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(g_validationLayers_AppInit.size());
        createInfo.ppEnabledLayerNames = g_validationLayers_AppInit.data();
        debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugCreateInfo.pfnUserCallback = App::debugCallback;
        createInfo.pNext = &debugCreateInfo;
    }
    else {
        createInfo.enabledLayerCount = 0;
        createInfo.pNext = nullptr;
    }
#ifdef __APPLE__
    createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif
    VK_APP_CHECK(vkCreateInstance(&createInfo, nullptr, &m_vkInstance));
    LogToFile("App::createInstance vkCreateInstance successful.");
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

void App::setupDebugMessenger() {
    if (!enableValidationLayers) {
        return;
    }
    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = App::debugCallback;
    VK_APP_CHECK(CreateDebugUtilsMessengerEXT_Helper(m_vkInstance, &createInfo, nullptr, &m_debugMessenger));
    LogToFile("App::setupDebugMessenger Setup complete.");
}

void App::createSurface() {
    LogToFile("App::createSurface Creating window surface...");
    VK_APP_CHECK(glfwCreateWindowSurface(m_vkInstance, m_window, nullptr, &m_surface));
    LogToFile("App::createSurface Window surface created.");
}

void App::pickPhysicalDevice() {
    LogToFile("App::pickPhysicalDevice Enumerating physical devices...");
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_vkInstance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        throw std::runtime_error("Failed to find GPUs with Vulkan support!");
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_vkInstance, &deviceCount, devices.data());
    LogToFile(std::string("App::pickPhysicalDevice Found ") + std::to_string(deviceCount) + " physical device(s).");

    for (const auto& device : devices) {
        if (this->isDeviceSuitable(device)) {
            m_physicalDevice = device;
            VkPhysicalDeviceProperties deviceProperties;
            vkGetPhysicalDeviceProperties(device, &deviceProperties);
            LogToFile(std::string("App::pickPhysicalDevice Suitable device found: ") + deviceProperties.deviceName);
            break;
        }
    }
    if (m_physicalDevice == VK_NULL_HANDLE) {
        throw std::runtime_error("Failed to find a suitable GPU!");
    }
}

bool App::isDeviceSuitable(VkPhysicalDevice queryDevice) {
    App::QueueFamilyIndices indices = this->findQueueFamilies(queryDevice);
    bool extensionsSupported = this->checkDeviceExtensionSupport(queryDevice);
    bool swapChainAdequate = false;
    if (extensionsSupported) {
        App::SwapChainSupportDetails swapChainSupport = this->querySwapChainSupport(queryDevice);
        swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
    }
    VkPhysicalDeviceFeatures supportedFeatures;
    vkGetPhysicalDeviceFeatures(queryDevice, &supportedFeatures);
    return indices.isComplete() && extensionsSupported && swapChainAdequate && supportedFeatures.samplerAnisotropy;
}

bool App::checkDeviceExtensionSupport(VkPhysicalDevice queryDevice) {
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(queryDevice, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(queryDevice, nullptr, &extensionCount, availableExtensions.data());
    std::set<std::string> requiredExtensionsSet(g_deviceExtensions_AppInit.begin(), g_deviceExtensions_AppInit.end());
    for (const auto& extension : availableExtensions) {
        requiredExtensionsSet.erase(extension.extensionName);
    }
    return requiredExtensionsSet.empty();
}

App::QueueFamilyIndices App::findQueueFamilies(VkPhysicalDevice queryDevice) {
    App::QueueFamilyIndices indices;
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
    LogToFile("App::createLogicalDevice Creating logical device...");
    App::QueueFamilyIndices indices = this->findQueueFamilies(m_physicalDevice);
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = { indices.graphicsFamily.value(), indices.presentFamily.value() };
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
    createInfo.enabledExtensionCount = static_cast<uint32_t>(g_deviceExtensions_AppInit.size());
    createInfo.ppEnabledExtensionNames = g_deviceExtensions_AppInit.data();

    if (enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(g_validationLayers_AppInit.size());
        createInfo.ppEnabledLayerNames = g_validationLayers_AppInit.data();
    }
    else {
        createInfo.enabledLayerCount = 0;
    }
    VK_APP_CHECK(vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device));
    vkGetDeviceQueue(m_device, indices.graphicsFamily.value(), 0, &m_graphicsQueue);
    vkGetDeviceQueue(m_device, indices.presentFamily.value(), 0, &m_presentQueue);
    LogToFile("App::createLogicalDevice Logical device created.");

    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_1;
    allocatorInfo.physicalDevice = m_physicalDevice;
    allocatorInfo.device = m_device;
    allocatorInfo.instance = m_vkInstance;
    VmaVulkanFunctions vulkanFunctions = {};
    vulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vulkanFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    allocatorInfo.pVulkanFunctions = &vulkanFunctions;
    LogToFile("App::createLogicalDevice Creating VMA Allocator...");
    VK_APP_CHECK(vmaCreateAllocator(&allocatorInfo, &m_vmaAllocator));
    LogToFile("App::createLogicalDevice VMA Allocator created.");
}

App::SwapChainSupportDetails App::querySwapChainSupport(VkPhysicalDevice queryDevice) {
    App::SwapChainSupportDetails details;
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
            return availableFormat;
        }
    }
    for (const auto& availableFormat : availableFormats) {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_UNORM) {
            return availableFormat;
        }
    }
    for (const auto& availableFormat : availableFormats) {
        if (availableFormat.format == VK_FORMAT_R8G8B8A8_UNORM && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return availableFormat;
        }
    }
    for (const auto& availableFormat : availableFormats) {
        if (availableFormat.format == VK_FORMAT_R8G8B8A8_UNORM) {
            return availableFormat;
        }
    }
    if (availableFormats.empty()) throw std::runtime_error("No swapchain formats available!");
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
    }
    else {
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
    LogToFile("App::createSwapChain Creating swapchain...");
    App::SwapChainSupportDetails swapChainSupport = this->querySwapChainSupport(m_physicalDevice);
    VkSurfaceFormatKHR surfaceFormat = this->chooseSwapSurfaceFormat(swapChainSupport.formats);
    VkPresentModeKHR presentMode = this->chooseSwapPresentMode(swapChainSupport.presentModes);
    VkExtent2D extent = this->chooseSwapExtent(swapChainSupport.capabilities);

    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
    if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) {
        imageCount = swapChainSupport.capabilities.maxImageCount;
    }
    imageCount = std::max(imageCount, (uint32_t)MAX_FRAMES_IN_FLIGHT);
    if (swapChainSupport.capabilities.maxImageCount > 0) {
        imageCount = std::min(imageCount, swapChainSupport.capabilities.maxImageCount);
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = m_surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    App::QueueFamilyIndices indices = this->findQueueFamilies(m_physicalDevice);
    uint32_t queueFamilyIndicesValue[] = { indices.graphicsFamily.value(), indices.presentFamily.value() };
    if (indices.graphicsFamily != indices.presentFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndicesValue;
    }
    else {
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
    LogToFile(std::string("App::createSwapChain Swapchain created with ") + std::to_string(imageCount) + " images.");
}

void App::createImageViews() {
    LogToFile(std::string("App::createImageViews Creating ") + std::to_string(m_swapChainImages.size()) + " image views...");
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
    LogToFile("App::createImageViews Image views created.");
}

void App::createRenderPass() {
    LogToFile("App::createRenderPass Creating render pass...");
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
    LogToFile("App::createRenderPass Render pass created.");
}

void App::createFramebuffers() {
    LogToFile(std::string("App::createFramebuffers Creating ") + std::to_string(m_swapChainImageViews.size()) + " framebuffers...");
    m_swapChainFramebuffers.resize(m_swapChainImageViews.size());
    for (size_t i = 0; i < m_swapChainImageViews.size(); i++) {
        VkImageView attachments[] = { m_swapChainImageViews[i] };
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
    LogToFile("App::createFramebuffers Framebuffers created.");
}

void App::createCommandPool() {
    LogToFile("App::createCommandPool Creating command pool...");
    App::QueueFamilyIndices queueFamilyIndices = this->findQueueFamilies(m_physicalDevice);
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();
    VK_APP_CHECK(vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool));
    LogToFile("App::createCommandPool Command pool created.");
}

void App::createCommandBuffers() {
    LogToFile(std::string("App::createCommandBuffers Creating ") + std::to_string(MAX_FRAMES_IN_FLIGHT) + " command buffers...");
    m_commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = (uint32_t)m_commandBuffers.size();
    VK_APP_CHECK(vkAllocateCommandBuffers(m_device, &allocInfo, m_commandBuffers.data()));
    LogToFile("App::createCommandBuffers Command buffers created.");
}

void App::createSyncObjects() {
    LogToFile(std::string("App::createSyncObjects Creating sync objects (") + std::to_string(MAX_FRAMES_IN_FLIGHT) + " sets)...");
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
    LogToFile("App::createSyncObjects Sync objects created.");
}

void App::initImGuiVulkan() {
    LogToFile("App::initImGuiVulkan Initializing ImGui for Vulkan...");
    VkDescriptorPoolSize pool_sizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    };
    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000 * IM_ARRAYSIZE(pool_sizes);
    pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;
    VK_APP_CHECK(vkCreateDescriptorPool(m_device, &pool_info, nullptr, &m_imguiDescriptorPool));
    LogToFile("App::initImGuiVulkan ImGui descriptor pool created.");

    GuiOverlay::setup(m_window, this);
    LogToFile("App::initImGuiVulkan GuiOverlay::setup() called.");
}

void App::createPersistentStagingBuffers() {
    LogToFile(std::string("App::createPersistentStagingBuffers Creating ") + std::to_string(kNumPersistentStagingBuffers) + " persistent staging buffers.");
    m_persistentStagingBuffers.resize(kNumPersistentStagingBuffers);
    m_persistentStagingBuffersMappedPtrs.resize(kNumPersistentStagingBuffers);
    m_availableStagingBufferIndices.clear();

    const uint32_t MAX_EXPECTED_WIDTH = 8192;
    const uint32_t MAX_EXPECTED_HEIGHT = 4608;
    VkDeviceSize bufferSize = static_cast<VkDeviceSize>(MAX_EXPECTED_WIDTH) * MAX_EXPECTED_HEIGHT * sizeof(uint16_t);

#ifndef NDEBUG
    LogToFile(std::string("App::createPersistentStagingBuffers Staging buffer individual size: ") + std::to_string(bufferSize) +
        " bytes (for max " + std::to_string(MAX_EXPECTED_WIDTH) + "x" + std::to_string(MAX_EXPECTED_HEIGHT) + " R16_UINT images).");
#endif

    if (kNumPersistentStagingBuffers == 0) {
        LogToFile("App::createPersistentStagingBuffers kNumPersistentStagingBuffers is 0. No buffers will be created.");
        return;
    }

    for (size_t i = 0; i < kNumPersistentStagingBuffers; ++i) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
            VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo allocationDetails;
        VkResult result = vmaCreateBuffer(m_vmaAllocator, &bufferInfo, &allocInfo,
            &m_persistentStagingBuffers[i].buffer,
            &m_persistentStagingBuffers[i].allocation,
            &allocationDetails);
        if (result != VK_SUCCESS) {
            LogToFile(std::string("App::createPersistentStagingBuffers FAILED to create persistent staging buffer ") + std::to_string(i) + ". Error: " + std::to_string(result));
            for (size_t k = 0; k < i; ++k) {
                if (m_persistentStagingBuffers[k].buffer != VK_NULL_HANDLE) {
                    vmaDestroyBuffer(m_vmaAllocator, m_persistentStagingBuffers[k].buffer, m_persistentStagingBuffers[k].allocation);
                }
            }
            m_persistentStagingBuffers.clear();
            m_persistentStagingBuffersMappedPtrs.clear();
            throw std::runtime_error("Failed to create persistent staging buffer " + std::to_string(i));
        }

        m_persistentStagingBuffersMappedPtrs[i] = allocationDetails.pMappedData;
        if (!m_persistentStagingBuffersMappedPtrs[i]) {
            LogToFile(std::string("App::createPersistentStagingBuffers ERROR: Failed to map persistent staging buffer ") + std::to_string(i));
            for (size_t k = 0; k <= i; ++k) {
                if (m_persistentStagingBuffers[k].buffer != VK_NULL_HANDLE) {
                    vmaDestroyBuffer(m_vmaAllocator, m_persistentStagingBuffers[k].buffer, m_persistentStagingBuffers[k].allocation);
                }
            }
            m_persistentStagingBuffers.clear();
            m_persistentStagingBuffersMappedPtrs.clear();
            throw std::runtime_error("Failed to map persistent staging buffer (pMappedData is null) " + std::to_string(i));
        }
        m_availableStagingBufferIndices.push(i);
#ifndef NDEBUG
        LogToFile(std::string("App::createPersistentStagingBuffers Created and mapped buffer ") + std::to_string(i) + " with size " + std::to_string(bufferSize) + ". Mapped ptr: " + (allocationDetails.pMappedData ? "VALID" : "NULL"));
#endif
    }
    LogToFile("App::createPersistentStagingBuffers All persistent staging buffers created and mapped.");
}

void App::launchWorkerThreads() {
    LogToFile("App::launchWorkerThreads Launching worker threads.");
    if (m_ioThread.joinable()) m_ioThread.join();
    if (m_decodeThread.joinable()) m_decodeThread.join();

    m_threadsShouldStop.store(false);

    m_ioThread = std::thread(&App::ioWorkerLoop, this);
    m_decodeThread = std::thread(&App::decodeWorkerLoop, this);
    LogToFile("App::launchWorkerThreads Worker threads launched.");
}

#ifdef _WIN32
LRESULT CALLBACK App::IpcWndProcStatic(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* app = static_cast<App*>(cs->lpCreateParams);
        SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        return TRUE;
    }
    auto* self = reinterpret_cast<App*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    return self ? self->IpcWndProc(hWnd, msg, wParam, lParam) : DefWindowProcW(hWnd, msg, wParam, lParam);
}

LRESULT App::IpcWndProc(HWND currentHwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_COPYDATA) {
        auto* cds = reinterpret_cast<COPYDATASTRUCT*>(lParam);
        if (cds && cds->dwData == 0x4D435257 && cds->lpData && cds->cbData > 0) {
            size_t num_wchars_with_null = cds->cbData / sizeof(wchar_t);
            std::wstring wpath_raw(static_cast<wchar_t*>(cds->lpData), num_wchars_with_null);
            size_t first_null = wpath_raw.find(L'\0');
            std::wstring wpath = (first_null == std::wstring::npos) ? wpath_raw : wpath_raw.substr(0, first_null);
            std::string path_utf8 = DebugLogHelper::wstring_to_utf8(wpath);

            if (!path_utf8.empty()) {
                LogToFile(std::string("App::IpcWndProc Received WM_COPYDATA with path: ") + path_utf8);
                const char* c_path_utf8_arr[] = { path_utf8.c_str() };
                handleDrop(1, c_path_utf8_arr);
                if (m_window) {
                    HWND mainGlfwHwnd = glfwGetWin32Window(m_window);
                    if (mainGlfwHwnd) {
                        if (IsIconic(mainGlfwHwnd)) ShowWindow(mainGlfwHwnd, SW_RESTORE);
                        SetForegroundWindow(mainGlfwHwnd);
                    }
                }
            }
            else {
                LogToFile("App::IpcWndProc WM_COPYDATA path conversion failed or empty.");
            }
        }
        return TRUE;
    }
    return DefWindowProcW(this->_ipcWnd ? this->_ipcWnd : currentHwnd, msg, wParam, lParam);
}
#endif