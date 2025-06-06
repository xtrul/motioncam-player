// FILE: src/App/AppCleanup.cpp
#include "App/App.h"
#include "Audio/AudioController.h"
#include "Decoder/DecoderWrapper.h"
#include "Playback/PlaybackController.h"
#include "Graphics/Renderer_VK.h"
#include "Utils/DebugLog.h"
#include "Gui/GuiOverlay.h"

#include <iostream>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#endif

// Ensure DestroyDebugUtilsMessengerEXT_Helper is declared if defined in AppInit.cpp
// This should ideally be in a shared Vulkan utility header.
#ifdef _WIN32 // Or more general if used on other platforms
extern void DestroyDebugUtilsMessengerEXT_Helper(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator);
#else 
// For non-Windows, if this helper is Windows-specific, this extern might not be needed or should be guarded.
// However, Vulkan debug messenger is cross-platform.
extern void DestroyDebugUtilsMessengerEXT_Helper(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator);
#endif


App::~App() {
    LogToFile("[App::~App] Destructor called.");
#ifndef NDEBUG
    std::cout << "[App::~App] Destructor called." << std::endl;
#endif

    if (m_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);
    }

    LogToFile("[App::~App] Signalling I/O and Decode threads to stop...");
    m_threadsShouldStop.store(true);
    m_ioThreadFileCv.notify_all();
    m_decodeQueue.stop_operations();
    m_gpuUploadQueue.stop_operations();
    m_availableStagingBufferIndices.stop_operations();

    if (m_ioThread.joinable()) {
        LogToFile("[App::~App] Joining I/O thread...");
        m_ioThread.join();
        LogToFile("[App::~App] I/O thread joined.");
    }
    if (m_decodeThread.joinable()) {
        LogToFile("[App::~App] Joining Decode thread...");
        m_decodeThread.join();
        LogToFile("[App::~App] Decode thread joined.");
    }

    destroyPersistentStagingBuffers();

    cleanupVulkan();

    if (m_audio) {
        LogToFile("[App::~App] Shutting down audio.");
#ifndef NDEBUG
        std::cout << "[App::~App] Shutting down audio." << std::endl;
#endif
        m_audio->shutdown();
    } m_audio.reset();
    m_decoderWrapper.reset();
    m_playbackController.reset();

#ifdef _WIN32
    if (_ipcWnd) {
        LogToFile("[App::~App] Destroying IPC window.");
#ifndef NDEBUG
        std::cout << "[App::~App] Destroying IPC window." << std::endl;
#endif
        DestroyWindow(_ipcWnd);
        _ipcWnd = nullptr;
    }
    const wchar_t* ipcClassName = L"MCRAW_PLAYER_IPC_WND_CLASS";
    UnregisterClassW(ipcClassName, GetModuleHandleW(nullptr));
    LogToFile("[App::~App] Unregistered IPC window class (attempted).");
#ifndef NDEBUG
    std::cout << "[App::~App] Unregistered IPC window class (attempted)." << std::endl;
#endif
#endif

    if (m_window) {
        LogToFile("[App::~App] Destroying GLFW window.");
#ifndef NDEBUG
        std::cout << "[App::~App] Destroying GLFW window." << std::endl;
#endif
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }
    LogToFile("[App::~App] Terminating GLFW.");
#ifndef NDEBUG
    std::cout << "[App::~App] Terminating GLFW." << std::endl;
#endif
    glfwTerminate();
    LogToFile("[App::~App] Destructor finished.");
#ifndef NDEBUG
    std::cout << "[App::~App] Destructor finished." << std::endl;
#endif
}

void App::destroyPersistentStagingBuffers() {
    LogToFile("[App::destroyPersistentStagingBuffers] Destroying persistent staging buffers.");
    if (m_vmaAllocator == VK_NULL_HANDLE) return;

    for (size_t i = 0; i < m_persistentStagingBuffers.size(); ++i) {
        if (m_persistentStagingBuffers[i].buffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(m_vmaAllocator, m_persistentStagingBuffers[i].buffer, m_persistentStagingBuffers[i].allocation);
            m_persistentStagingBuffers[i].buffer = VK_NULL_HANDLE;
            m_persistentStagingBuffers[i].allocation = VK_NULL_HANDLE;
        }
    }
    m_persistentStagingBuffers.clear();
    m_persistentStagingBuffersMappedPtrs.clear();
    m_availableStagingBufferIndices.clear();
    LogToFile("[App::destroyPersistentStagingBuffers] Persistent staging buffers destroyed.");
}


void App::cleanupSwapChain() {
    LogToFile("[App::cleanupSwapChain] Cleaning up swapchain resources...");
    for (auto framebuffer : m_swapChainFramebuffers) {
        if (framebuffer != VK_NULL_HANDLE) vkDestroyFramebuffer(m_device, framebuffer, nullptr);
    }
    m_swapChainFramebuffers.clear();

    if (m_rendererVk) {
    }

    for (auto imageView : m_swapChainImageViews) {
        if (imageView != VK_NULL_HANDLE) vkDestroyImageView(m_device, imageView, nullptr);
    }
    m_swapChainImageViews.clear();

    if (m_swapChain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(m_device, m_swapChain, nullptr);
        m_swapChain = VK_NULL_HANDLE;
    }
    LogToFile("[App::cleanupSwapChain] Swapchain resources cleaned.");
}

void App::recreateSwapChain() {
    LogToFile("[App::recreateSwapChain] Starting swapchain recreation...");
    int fbWidth = 0, fbHeight = 0;
    glfwGetFramebufferSize(m_window, &fbWidth, &fbHeight);
    while (fbWidth == 0 || fbHeight == 0) {
        LogToFile("[App::recreateSwapChain] Window minimized, waiting for events...");
        glfwWaitEvents();
        glfwGetFramebufferSize(m_window, &fbWidth, &fbHeight);
    }
    if (!m_isFullscreen) {
        glfwGetWindowSize(m_window, &m_windowWidth, &m_windowHeight);
        m_storedWindowedWidth = m_windowWidth;
        m_storedWindowedHeight = m_windowHeight;
    }

    LogToFile(std::string("[App::recreateSwapChain] New framebuffer size: ") + std::to_string(fbWidth) + "x" + std::to_string(fbHeight));

    if (m_device != VK_NULL_HANDLE) vkDeviceWaitIdle(m_device);
    LogToFile("[App::recreateSwapChain] Device idle.");

    cleanupSwapChain();
    LogToFile("[App::recreateSwapChain] Old swapchain cleaned.");

    createSwapChain();
    createImageViews();
    createFramebuffers();

    if (m_rendererVk) {
        LogToFile("[App::recreateSwapChain] Notifying Renderer_VK about swapchain recreation.");
        m_rendererVk->onSwapChainRecreated(m_renderPass, static_cast<uint32_t>(m_swapChainImages.size()));
    }

    LogToFile("[App::recreateSwapChain] Swapchain recreation complete.");
}


void App::cleanupVulkan() {
    LogToFile("[App::cleanupVulkan] Starting Vulkan cleanup...");

    cleanupSwapChain();

    if (m_rendererVk) {
        LogToFile("[App::cleanupVulkan] Cleaning up Renderer_VK (main resources)...");
        m_rendererVk->cleanup();
        m_rendererVk.reset();
    }

    LogToFile("[App::cleanupVulkan] Cleaning up GuiOverlay (ImGui shutdown)...");
    GuiOverlay::cleanup();

    if (m_imguiDescriptorPool != VK_NULL_HANDLE) {
        LogToFile("[App::cleanupVulkan] Destroying ImGui descriptor pool...");
        vkDestroyDescriptorPool(m_device, m_imguiDescriptorPool, nullptr);
        m_imguiDescriptorPool = VK_NULL_HANDLE;
    }


    LogToFile("[App::cleanupVulkan] Destroying sync objects...");
    for (size_t i = 0; i < m_imageAvailableSemaphores.size(); i++) {
        if (m_imageAvailableSemaphores[i] != VK_NULL_HANDLE)
            vkDestroySemaphore(m_device, m_imageAvailableSemaphores[i], nullptr);
    }
    for (size_t i = 0; i < m_renderFinishedSemaphores.size(); i++) {
        if (m_renderFinishedSemaphores[i] != VK_NULL_HANDLE)
            vkDestroySemaphore(m_device, m_renderFinishedSemaphores[i], nullptr);
    }
    for (size_t i = 0; i < m_inFlightFences.size(); i++) {
        if (m_inFlightFences[i] != VK_NULL_HANDLE)
            vkDestroyFence(m_device, m_inFlightFences[i], nullptr);
    }
    m_imageAvailableSemaphores.clear();
    m_renderFinishedSemaphores.clear();
    m_inFlightFences.clear();
    m_commandBuffers.clear();

    if (m_commandPool != VK_NULL_HANDLE) {
        LogToFile("[App::cleanupVulkan] Destroying command pool...");
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;
    }
    if (m_renderPass != VK_NULL_HANDLE) {
        LogToFile("[App::cleanupVulkan] Destroying render pass...");
        vkDestroyRenderPass(m_device, m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }

    if (m_vmaAllocator != VK_NULL_HANDLE) {
        LogToFile("[App::cleanupVulkan] Destroying VMA Allocator...");
        vmaDestroyAllocator(m_vmaAllocator);
        m_vmaAllocator = VK_NULL_HANDLE;
    }


    if (m_device != VK_NULL_HANDLE) {
        LogToFile("[App::cleanupVulkan] Destroying logical device...");
        vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
    }
    if (enableValidationLayers && m_debugMessenger != VK_NULL_HANDLE) {
        LogToFile("[App::cleanupVulkan] Destroying debug messenger...");
        DestroyDebugUtilsMessengerEXT_Helper(m_vkInstance, m_debugMessenger, nullptr);
        m_debugMessenger = VK_NULL_HANDLE;
    }
    if (m_surface != VK_NULL_HANDLE) {
        LogToFile("[App::cleanupVulkan] Destroying surface...");
        vkDestroySurfaceKHR(m_vkInstance, m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }
    if (m_vkInstance != VK_NULL_HANDLE) {
        LogToFile("[App::cleanupVulkan] Destroying Vulkan instance...");
        vkDestroyInstance(m_vkInstance, nullptr);
        m_vkInstance = VK_NULL_HANDLE;
    }
    LogToFile("[App::cleanupVulkan] Vulkan cleanup complete.");
}