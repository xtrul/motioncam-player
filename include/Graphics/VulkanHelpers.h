#ifndef VULKAN_HELPERS_H
#define VULKAN_HELPERS_H

#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <iostream> // For std::cerr in VK_CHECK_RENDERER
#include "Utils/DebugLog.h" // For LogToFile in VK_CHECK_RENDERER

// Macro for Vulkan error checking, specific to Renderer context or general Vulkan calls.
// Note: Original Renderer_VK.cpp used VK_CHECK. This is a similar macro.
// If VK_APP_CHECK from App.cpp is more general, it could be moved here too.
// For now, defining a renderer-specific one.
#define VK_CHECK_RENDERER(x)                                        \
    do {                                                            \
        VkResult err = x;                                           \
        if (err) {                                                  \
            std::string error_msg = std::string("[VULKAN CHECK FAILED IN RENDERER CONTEXT] Error: ") + std::to_string(err) + " (" #x ") at " __FILE__ ":" + std::to_string(__LINE__); \
            LogToFile(error_msg);                                   \
            std::cerr << error_msg << std::endl;                    \
            /* Consider rethrowing or a more graceful shutdown */   \
            /* For now, matches original Renderer_VK.cpp behavior */ \
            abort();                                                \
        }                                                           \
    } while (0)


namespace VulkanHelpers {

    std::vector<char> readFile(const std::string& filename);
    VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code);

    // For single-time commands, they need access to device, command pool, and queue.
    // These could be passed as parameters or the functions could be members of a helper class
    // that holds these Vulkan objects. For now, make them free functions requiring device context.
    VkCommandBuffer beginSingleTimeCommands(VkDevice device, VkCommandPool commandPool);
    void endSingleTimeCommands(VkDevice device, VkCommandPool commandPool, VkQueue queue, VkCommandBuffer commandBuffer);

} // namespace VulkanHelpers

#endif // VULKAN_HELPERS_H