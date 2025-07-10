#include "Graphics/VulkanHelpers.h"
#include "Utils/DebugLog.h" // For LogToFile
#include <fstream>
#include <stdexcept> // For std::runtime_error

namespace VulkanHelpers {

    std::vector<char> readFile(const std::string& filename) {
        std::string fullPath = filename;
        LogToFile(std::string("[VulkanHelpers::readFile] Attempting to read shader file: ") + fullPath);
        std::ifstream file(fullPath, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            std::string errorMsg = "[VulkanHelpers::readFile] ERROR: FAILED to open shader file: " + fullPath;
            LogToFile(errorMsg);
            throw std::runtime_error(errorMsg);
        }
        size_t fileSize = (size_t)file.tellg();
        LogToFile(std::string("[VulkanHelpers::readFile] Shader file ") + fullPath + " size: " + std::to_string(fileSize) + " bytes.");
        if (fileSize == 0) {
            std::string errorMsg = "[VulkanHelpers::readFile] ERROR: Shader file is EMPTY: " + fullPath;
            LogToFile(errorMsg);
            throw std::runtime_error(errorMsg);
        }
        std::vector<char> buffer(fileSize);
        file.seekg(0);
        file.read(buffer.data(), fileSize);
        file.close();
        LogToFile(std::string("[VulkanHelpers::readFile] Successfully read shader file: ") + fullPath);
        return buffer;
    }

    VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code) {
        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
        VkShaderModule shaderModule;
        VK_CHECK_RENDERER(vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule)); // Use the renderer-context macro
        return shaderModule;
    }

    VkCommandBuffer beginSingleTimeCommands(VkDevice device, VkCommandPool commandPool) {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = commandPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer commandBuffer;
        VK_CHECK_RENDERER(vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer));

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK_RENDERER(vkBeginCommandBuffer(commandBuffer, &beginInfo));
        return commandBuffer;
    }

    void endSingleTimeCommands(VkDevice device, VkCommandPool commandPool, VkQueue queue, VkCommandBuffer commandBuffer) {
        VK_CHECK_RENDERER(vkEndCommandBuffer(commandBuffer));

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        VK_CHECK_RENDERER(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
        VK_CHECK_RENDERER(vkQueueWaitIdle(queue));

        vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    }

} // namespace VulkanHelpers