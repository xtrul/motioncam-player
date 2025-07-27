#define IMGUI_DEFINE_MATH_OPERATORS

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#endif

#include "Gui/GuiOverlay.h" 
#include "Gui/GuiStyles.h"
#include "App/App.h" // For App instance type and Vulkan members

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>
#include <filesystem>
#include <cstdio> // For fprintf, abort

// Provide fallbacks if the bundled ImGui doesn't expose docking/viewports flags
#ifndef ImGuiConfigFlags_DockingEnable
#define ImGuiConfigFlags_DockingEnable 0
#endif
#ifndef ImGuiConfigFlags_ViewportsEnable
#define ImGuiConfigFlags_ViewportsEnable 0
#endif

// For App::QueueFamilyIndices, if not fully defined in App.h
// Assuming App.h has the full definition or it's accessible.

// Application base path defined in main.cpp
extern std::string g_AppBasePath;

namespace GuiOverlay {

    // Hold persistent path for ImGui ini file
    static std::string g_ImGuiIniPath;

    void setup(GLFWwindow* window, App* appInstance) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;    // Enable Docking
        io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable; // Ensure Multi-Viewports are disabled

        // Explicitly set ini file location so it stays in the application folder
        g_ImGuiIniPath = (std::filesystem::path(g_AppBasePath) / "imgui.ini").string();
        io.IniFilename = g_ImGuiIniPath.c_str();

        // Load fonts and apply custom style
        GuiStyles::LoadFonts(io); // This will populate GuiStyles::G_TextFont etc.
        GuiStyles::ApplyCustomStyle();

        ImGui_ImplGlfw_InitForVulkan(window, true); // true for install_callbacks

        ImGui_ImplVulkan_InitInfo init_info = {};
        init_info.Instance = appInstance->m_vkInstance;
        init_info.PhysicalDevice = appInstance->m_physicalDevice;
        init_info.Device = appInstance->m_device;

        // Get queue family index
        App::QueueFamilyIndices indices = appInstance->findQueueFamilies(appInstance->m_physicalDevice);
        if (!indices.graphicsFamily.has_value()) {
            // This should not happen if App initialization was successful
            fprintf(stderr, "[GuiOverlay::setup] Critical error: No graphics queue family found for ImGui setup.\n");
            abort();
        }
        init_info.QueueFamily = indices.graphicsFamily.value();
        init_info.Queue = appInstance->m_graphicsQueue;
        init_info.PipelineCache = VK_NULL_HANDLE; // Optional
        init_info.DescriptorPool = appInstance->m_imguiDescriptorPool; // App creates and owns this
        init_info.Subpass = 0; // Assuming ImGui renders in the first subpass

        // MinImageCount and ImageCount should match the swap chain
        init_info.MinImageCount = MAX_FRAMES_IN_FLIGHT;
        // GuiOverlay::setup is a friend of App and can read the swapchain size directly
        init_info.ImageCount = static_cast<uint32_t>(appInstance->m_swapChainImages.size());


        init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT; // No MSAA for ImGui UI itself
        init_info.Allocator = nullptr; // Using default allocator
        init_info.CheckVkResultFn = [](VkResult err) { // Optional error checking
            if (err == 0) return;
            fprintf(stderr, "[ImGui Vulkan] Error: VkResult = %d\n", err);
            if (err < 0) abort(); // Or throw
            };
        init_info.RenderPass = appInstance->m_renderPass; // App creates and owns this

        ImGui_ImplVulkan_Init(&init_info);

        // Upload Fonts
        // This needs a command buffer. App must provide one or a way to get one.
        // Original App.cpp used m_commandBuffers[0].
        // Assuming App makes this accessible or provides a helper.
        // Since GuiOverlay::setup is a friend, it can access m_commandBuffers.
        VkCommandBuffer command_buffer = appInstance->m_commandBuffers[0];

        VkCommandBufferBeginInfo begin_info = {};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(command_buffer, &begin_info);

        ImGui_ImplVulkan_CreateFontsTexture(); // Uses the command buffer implicitly passed at init or this one

        VkSubmitInfo end_info = {};
        end_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        end_info.commandBufferCount = 1;
        end_info.pCommandBuffers = &command_buffer;
        vkEndCommandBuffer(command_buffer);

        vkQueueSubmit(appInstance->m_graphicsQueue, 1, &end_info, VK_NULL_HANDLE);
        vkQueueWaitIdle(appInstance->m_graphicsQueue); // Ensure fonts are uploaded

        ImGui_ImplVulkan_DestroyFontsTexture(); // Device Staging Bufs are no longer needed
    }

    void cleanup() {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
    }

} // namespace GuiOverlay

