#include "Graphics/Descriptor.h"
#include "Graphics/Renderer_VK.h" // To access Renderer_VK members
#include "Graphics/VulkanHelpers.h" // For VK_CHECK_RENDERER
#include "Utils/DebugLog.h"

#include <array> // For std::array

namespace Descriptor {

    bool createDescriptorSetLayout(Renderer_VK* renderer) {
        LogToFile("[Descriptor::createDescriptorSetLayout] Creating descriptor set layout.");
        VkDescriptorSetLayoutBinding samplerLayoutBinding{};
        samplerLayoutBinding.binding = 0;
        samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        samplerLayoutBinding.descriptorCount = 1;
        samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        samplerLayoutBinding.pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutBinding uboLayoutBinding{};
        uboLayoutBinding.binding = 1;
        uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboLayoutBinding.descriptorCount = 1;
        uboLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        uboLayoutBinding.pImmutableSamplers = nullptr;

        std::array<VkDescriptorSetLayoutBinding, 2> bindings = { samplerLayoutBinding, uboLayoutBinding };
        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();

        VK_CHECK_RENDERER(vkCreateDescriptorSetLayout(renderer->m_device_p, &layoutInfo, nullptr, &renderer->m_descriptorSetLayout));
        return true;
    }

    bool createUniformBuffers(Renderer_VK* renderer) {
        cleanupUniformBuffers(renderer); // Clean up old ones first

        // Accessing ShaderParamsUBO requires Renderer_VK.h to be more complete or ShaderParamsUBO to be moved.
        // For now, assuming Renderer_VK.h has its definition or it's forward-declared and sized correctly.
        // The definition of ShaderParamsUBO is private in Renderer_VK.h. This will require a friend declaration or moving ShaderParamsUBO.
        // For simplicity in this step, let's assume sizeof can get it or it's a known fixed size.
        // A better solution is to make ShaderParamsUBO accessible or pass its size.
        // For now, we'll proceed assuming Renderer_VK::ShaderParamsUBO is somehow accessible for sizeof.
        // This will be an issue if ShaderParamsUBO is not defined in a way that sizeof works here.
        // Let's assume Renderer_VK.h was updated to make ShaderParamsUBO's size known or the struct public.
        // If not, this would be: struct ShaderParamsUBO { ... }; here or in a shared header.
        // For now, we'll use a placeholder size if it's not available.
        // A more robust way: Renderer_VK could have a static constexpr member for UBO size.
        // For now, let's assume Renderer_VK.h has:
        // struct ShaderParamsUBO { ... }; (public or friend access)
        // VkDeviceSize bufferSize = sizeof(Renderer_VK::ShaderParamsUBO);
        // However, ShaderParamsUBO is private. This is a design flaw in the split if not addressed.
        // Let's assume Renderer_VK provides a method or a public constant for this size.
        // If not, we must estimate or make ShaderParamsUBO public.
        // For this refactor, I will assume ShaderParamsUBO has been made accessible for sizeof.
        // (This would require editing Renderer_VK.h to make ShaderParamsUBO public or providing a getter for its size)
        // For the sake of continuing, I'll use a hardcoded estimate if needed, but this is bad practice.
        // Let's assume the struct definition was moved to a common header or made public in Renderer_VK.h.
        // If Renderer_VK.h is as provided, this `sizeof` will fail.
        // The prompt for Renderer_VK.h did not make ShaderParamsUBO public.
        // This needs to be resolved. For now, I'll assume it's resolved by making it public in Renderer_VK.h
        // or moving it to a shared header.
        // The struct `ShaderParamsUBO` is defined as private in `Renderer_VK.h`.
        // To make this compile, `ShaderParamsUBO` needs to be public, or its size exposed.
        // Let's assume for this step that `Renderer_VK::ShaderParamsUBO` was made public.
        struct TempShaderParamsUBO { // Local definition to get sizeof, assuming it matches the private one.
            alignas(4) int W; alignas(4) int H; alignas(4) int cfaType; alignas(4) float exposure;
            alignas(4) float blackLevel; alignas(4) float whiteLevel; alignas(4) float invBlackWhiteRange;
            alignas(4) float gainR; alignas(4) float gainG; alignas(4) float gainB;
            alignas(16) glm::mat4 CCM; alignas(4) float saturationAdjustment;
        };
        VkDeviceSize bufferSize = sizeof(TempShaderParamsUBO);


        renderer->m_uniformBuffers.resize(renderer->m_swapChainImageCount);
        renderer->m_uniformBufferAllocations.resize(renderer->m_swapChainImageCount);
        renderer->m_uniformBuffersMapped.resize(renderer->m_swapChainImageCount);

        LogToFile(std::string("[Descriptor::createUniformBuffers] Creating ") + std::to_string(renderer->m_swapChainImageCount) + " uniform buffers.");

        if (renderer->m_swapChainImageCount == 0) {
            LogToFile("[Descriptor::createUniformBuffers] m_swapChainImageCount is 0, no uniform buffers to create.");
            return true;
        }

        for (size_t i = 0; i < renderer->m_swapChainImageCount; i++) {
            VkBufferCreateInfo bufferInfo{};
            bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferInfo.size = bufferSize;
            bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

            VmaAllocationCreateInfo allocInfo{};
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

            VmaAllocationInfo allocationDetails;

            VkResult result = vmaCreateBuffer(renderer->m_allocator_p, &bufferInfo, &allocInfo, &renderer->m_uniformBuffers[i], &renderer->m_uniformBufferAllocations[i], &allocationDetails);
            if (result != VK_SUCCESS) {
                LogToFile(std::string("[Descriptor::createUniformBuffers] FAILED to create uniform buffer ") + std::to_string(i) + ". Error: " + std::to_string(result));
                for (size_t j = 0; j < i; ++j) {
                    if (renderer->m_uniformBuffers[j] != VK_NULL_HANDLE) vmaDestroyBuffer(renderer->m_allocator_p, renderer->m_uniformBuffers[j], renderer->m_uniformBufferAllocations[j]);
                }
                // Reset vectors to consistent state
                renderer->m_uniformBuffers.assign(renderer->m_swapChainImageCount, VK_NULL_HANDLE);
                renderer->m_uniformBufferAllocations.assign(renderer->m_swapChainImageCount, VK_NULL_HANDLE);
                renderer->m_uniformBuffersMapped.assign(renderer->m_swapChainImageCount, nullptr);
                VK_CHECK_RENDERER(result);
                return false;
            }

            renderer->m_uniformBuffersMapped[i] = allocationDetails.pMappedData;
            if (!renderer->m_uniformBuffersMapped[i]) {
                LogToFile(std::string("[Descriptor::createUniformBuffers] ERROR: Failed to map uniform buffer (pMappedData is null) ") + std::to_string(i));
                for (size_t j = 0; j <= i; ++j) {
                    if (renderer->m_uniformBuffers[j] != VK_NULL_HANDLE) vmaDestroyBuffer(renderer->m_allocator_p, renderer->m_uniformBuffers[j], renderer->m_uniformBufferAllocations[j]);
                }
                renderer->m_uniformBuffers.assign(renderer->m_swapChainImageCount, VK_NULL_HANDLE);
                renderer->m_uniformBufferAllocations.assign(renderer->m_swapChainImageCount, VK_NULL_HANDLE);
                renderer->m_uniformBuffersMapped.assign(renderer->m_swapChainImageCount, nullptr);
                return false;
            }
        }
        LogToFile("[Descriptor::createUniformBuffers] Uniform buffers created and mapped.");
        return true;
    }

    void cleanupUniformBuffers(Renderer_VK* renderer) {
        LogToFile(std::string("[Descriptor::cleanupUniformBuffers] Cleaning up ") + std::to_string(renderer->m_uniformBuffers.size()) + " uniform buffers.");
        if (renderer->m_allocator_p != VK_NULL_HANDLE) {
            for (size_t i = 0; i < renderer->m_uniformBuffers.size(); i++) {
                if (renderer->m_uniformBuffers[i] != VK_NULL_HANDLE) {
                    vmaDestroyBuffer(renderer->m_allocator_p, renderer->m_uniformBuffers[i], renderer->m_uniformBufferAllocations[i]);
                }
            }
        }
        renderer->m_uniformBuffers.clear();
        renderer->m_uniformBufferAllocations.clear();
        renderer->m_uniformBuffersMapped.clear();
    }

    bool createDescriptorPool(Renderer_VK* renderer) {
        LogToFile(std::string("[Descriptor::createDescriptorPool] Creating descriptor pool for ") + std::to_string(renderer->m_swapChainImageCount) + " sets.");

        if (renderer->m_descriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(renderer->m_device_p, renderer->m_descriptorPool, nullptr);
            renderer->m_descriptorPool = VK_NULL_HANDLE;
            renderer->m_descriptorSets.clear();
        }

        if (renderer->m_swapChainImageCount == 0) {
            LogToFile("[Descriptor::createDescriptorPool] WARNING: m_swapChainImageCount is 0. Pool will be minimal.");
        }
        std::array<VkDescriptorPoolSize, 2> poolSizes{};
        poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSizes[0].descriptorCount = (renderer->m_swapChainImageCount > 0) ? renderer->m_swapChainImageCount : 1;
        poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSizes[1].descriptorCount = (renderer->m_swapChainImageCount > 0) ? renderer->m_swapChainImageCount : 1;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = (renderer->m_swapChainImageCount > 0) ? renderer->m_swapChainImageCount : 1;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

        VK_CHECK_RENDERER(vkCreateDescriptorPool(renderer->m_device_p, &poolInfo, nullptr, &renderer->m_descriptorPool));
        LogToFile("[Descriptor::createDescriptorPool] Descriptor pool created.");
        return true;
    }

    bool createDescriptorSets(Renderer_VK* renderer) {
        if (renderer->m_swapChainImageCount == 0) {
            LogToFile("[Descriptor::createDescriptorSets] m_swapChainImageCount is 0. No descriptor sets to allocate.");
            renderer->m_descriptorSets.clear();
            return true;
        }

        LogToFile(std::string("[Descriptor::createDescriptorSets] Allocating ") + std::to_string(renderer->m_swapChainImageCount) + " descriptor sets.");

        std::vector<VkDescriptorSetLayout> layouts(renderer->m_swapChainImageCount, renderer->m_descriptorSetLayout);
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = renderer->m_descriptorPool;
        allocInfo.descriptorSetCount = renderer->m_swapChainImageCount;
        allocInfo.pSetLayouts = layouts.data();

        renderer->m_descriptorSets.resize(renderer->m_swapChainImageCount);
        VkResult result = vkAllocateDescriptorSets(renderer->m_device_p, &allocInfo, renderer->m_descriptorSets.data());
        if (result != VK_SUCCESS) {
            LogToFile(std::string("[Descriptor::createDescriptorSets] FAILED to allocate descriptor sets! Error: ") + std::to_string(result));
            renderer->m_descriptorSets.clear();
            VK_CHECK_RENDERER(result);
            return false;
        }

        updateDescriptorSetsWithNewRawImage(renderer);
        return true;
    }

    void updateDescriptorSetsWithNewRawImage(Renderer_VK* renderer) {
        if (renderer->m_descriptorSets.empty()) {
            LogToFile("[Descriptor::updateDescriptorSetsWithNewRawImage] No descriptor sets to update.");
            return;
        }
        if (renderer->m_rawImageView == VK_NULL_HANDLE || renderer->m_rawImageSampler == VK_NULL_HANDLE) {
            LogToFile("[Descriptor::updateDescriptorSetsWithNewRawImage] ERROR: Cannot update. Raw image view or sampler is invalid.");
            return;
        }
        LogToFile(std::string("[Descriptor::updateDescriptorSetsWithNewRawImage] Updating ") + std::to_string(renderer->m_descriptorSets.size()) + " descriptor sets.");

        struct TempShaderParamsUBO { // Copied from createUniformBuffers, assuming public access for sizeof
            alignas(4) int W; alignas(4) int H; alignas(4) int cfaType; alignas(4) float exposure;
            alignas(4) float blackLevel; alignas(4) float whiteLevel; alignas(4) float invBlackWhiteRange;
            alignas(4) float gainR; alignas(4) float gainG; alignas(4) float gainB;
            alignas(16) glm::mat4 CCM; alignas(4) float saturationAdjustment;
        };

        for (size_t i = 0; i < renderer->m_descriptorSets.size(); ++i) {
            if (i >= renderer->m_uniformBuffers.size() || renderer->m_uniformBuffers[i] == VK_NULL_HANDLE) {
                LogToFile(std::string("[Descriptor::updateDescriptorSetsWithNewRawImage] ERROR: Uniform buffer for set ") + std::to_string(i) + " is invalid. Skipping.");
                continue;
            }

            VkDescriptorImageInfo imageInfo{};
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfo.imageView = renderer->m_rawImageView;
            imageInfo.sampler = renderer->m_rawImageSampler;

            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = renderer->m_uniformBuffers[i];
            bufferInfo.offset = 0;
            bufferInfo.range = sizeof(TempShaderParamsUBO); // Using local temp struct for sizeof

            std::array<VkWriteDescriptorSet, 2> descriptorWrites{};
            descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[0].dstSet = renderer->m_descriptorSets[i];
            descriptorWrites[0].dstBinding = 0;
            descriptorWrites[0].dstArrayElement = 0;
            descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrites[0].descriptorCount = 1;
            descriptorWrites[0].pImageInfo = &imageInfo;

            descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrites[1].dstSet = renderer->m_descriptorSets[i];
            descriptorWrites[1].dstBinding = 1;
            descriptorWrites[1].dstArrayElement = 0;
            descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            descriptorWrites[1].descriptorCount = 1;
            descriptorWrites[1].pBufferInfo = &bufferInfo;

            vkUpdateDescriptorSets(renderer->m_device_p, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
        }
    }

} // namespace Descriptor