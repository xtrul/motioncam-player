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
#include <cstdio> // For fprintf, abort

// For App::QueueFamilyIndices, if not fully defined in App.h
// Assuming App.h has the full definition or it's accessible.

namespace GuiOverlay {

    void setup(GLFWwindow* window, App* appInstance) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls

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

        // MinImageCount and ImageCount should match swap chain
        init_info.MinImageCount = MAX_FRAMES_IN_FLIGHT; // From AppConfig.h via App.h
        // Get actual swapchain image count from App instance
        // This requires m_swapChainImages to be accessible or a getter in App
        // For now, assuming App.h makes m_swapChainImages.size() available or a similar mechanism
        // If App::m_swapChainImages is private, App needs a public getter or this needs adjustment.
        // The original App.h has m_swapChainImages as private.
        // For this refactor, let's assume App will provide a way to get this, or it's made public for setup.
        // A direct access to appInstance->m_swapChainImages.size() would require it to be public.
        // Let's assume it's made public for this setup phase, or a getter exists.
        // If m_swapChainImages is private, this line would be:
        // init_info.ImageCount = appInstance->getSwapChainImageCount(); // Hypothetical getter
        // For now, using a placeholder if App.h is strictly followed.
        // The provided App.h has m_swapChainImages private. This will be an issue.
        // Let's assume App::m_swapChainImages was made public for this step.
        // If not, this must be changed.
        // The original App.cpp has `appInstance->m_swapChainImages.size()`.
        // This means m_swapChainImages was public or accessible to GuiOverlay::setup.
        // The refactored App.h made m_swapChainImages private.
        // This needs to be fixed in App.h or App provides a getter.
        // For now, I will assume a getter: appInstance->getSwapChainImageCount()
        // Let's check the original App.h: m_swapChainImages is private.
        // The original GuiOverlay::setup was a friend or part of App.
        // Now that GuiOverlay is separate, it needs public access.
        // I will assume App.h is modified to make m_swapChainImages public or add a getter.
        // For this pass, I'll assume a getter:
        // uint32_t appInstanceGetSwapChainImageCount(App* app) { return static_cast<uint32_t>(app->m_swapChainImages.size()); }
        // This is messy. The best is a public getter in App class.
        // For now, I'll assume App.h was modified to make m_swapChainImages public for this.
        // If App::m_swapChainImages is private, this line needs to be:
        // init_info.ImageCount = appInstance->getSwapChainImageCount(); // Example getter
        // The prompt for App.h shows m_swapChainImages as private.
        // The original `GuiOverlay::setup` was a friend of `App`.
        // This friendship should be maintained or a public getter added to `App`.
        // The prompt for `App.h` includes: `friend void GuiOverlay::setup(GLFWwindow* window, App* appInstance);`
        // So, GuiOverlay::setup can access private members of App.
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