#include "Graphics/ImageResource.h"
#include "Graphics/Renderer_VK.h"
#include "Graphics/VulkanHelpers.h"
#include "Utils/DebugLog.h"
#include <stdexcept>

namespace ImageResource {

    void transitionImageLayout(
        VkDevice device,
        VkCommandPool commandPool,
        VkQueue queue,
        VkImage image,
        VkImageLayout oldLayout,
        VkImageLayout newLayout
    ) {
        std::string oldLayoutStr = "UNKNOWN_LAYOUT_" + std::to_string(oldLayout);
        std::string newLayoutStr = "UNKNOWN_LAYOUT_" + std::to_string(newLayout);

        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED) oldLayoutStr = "UNDEFINED";
        else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) oldLayoutStr = "TRANSFER_DST_OPTIMAL";
        else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) oldLayoutStr = "SHADER_READ_ONLY_OPTIMAL";

        if (newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) newLayoutStr = "TRANSFER_DST_OPTIMAL";
        else if (newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) newLayoutStr = "SHADER_READ_ONLY_OPTIMAL";

        LogToFile(std::string("transitionImageLayout: Image ") + std::to_string(reinterpret_cast<uintptr_t>(image)) + ": " + oldLayoutStr + " -> " + newLayoutStr);

        VkCommandBuffer commandBuffer = VulkanHelpers::beginSingleTimeCommands(device, commandPool);

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            sourceStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        }
        else {
            LogToFile(std::string("transitionImageLayout: ERROR - Unsupported layout transition from ") + oldLayoutStr + " to " + newLayoutStr + " for image " + std::to_string(reinterpret_cast<uintptr_t>(image)));
            VulkanHelpers::endSingleTimeCommands(device, commandPool, queue, commandBuffer);
            throw std::invalid_argument("Unsupported layout transition!");
        }

        vkCmdPipelineBarrier(
            commandBuffer,
            sourceStage, destinationStage,
            0,
            0, nullptr,
            0, nullptr,
            1, &barrier
        );

        VulkanHelpers::endSingleTimeCommands(device, commandPool, queue, commandBuffer);
    }

    bool createRawImageResources(Renderer_VK* renderer, int width, int height) {
        LogToFile(std::string("ImageResource::createRawImageResources Creating raw image ") + std::to_string(width) + "x" + std::to_string(height));

        cleanupRawImageResources(renderer);

        renderer->m_currentRawW = width;
        renderer->m_currentRawH = height;

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = static_cast<uint32_t>(width);
        imageInfo.extent.height = static_cast<uint32_t>(height);
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = VK_FORMAT_R16_UINT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

        VK_CHECK_RENDERER(vmaCreateImage(renderer->m_allocator_p, &imageInfo, &allocInfo, &renderer->m_rawImage, &renderer->m_rawImageAllocation, nullptr));

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = renderer->m_rawImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R16_UINT;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;
        VK_CHECK_RENDERER(vkCreateImageView(renderer->m_device_p, &viewInfo, nullptr, &renderer->m_rawImageView));

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_NEAREST;
        samplerInfo.minFilter = VK_FILTER_NEAREST;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 0.0f;
        VK_CHECK_RENDERER(vkCreateSampler(renderer->m_device_p, &samplerInfo, nullptr, &renderer->m_rawImageSampler));

        transitionImageLayout(
            renderer->m_device_p,
            renderer->m_hostSiteCommandPool_p,
            renderer->m_graphicsQueue_p,
            renderer->m_rawImage,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        );
        LogToFile("ImageResource::createRawImageResources Raw image resources created and transitioned.");
        return true;
    }

    void cleanupRawImageResources(Renderer_VK* renderer) {
        if (renderer->m_rawImageSampler != VK_NULL_HANDLE) {
            vkDestroySampler(renderer->m_device_p, renderer->m_rawImageSampler, nullptr);
            renderer->m_rawImageSampler = VK_NULL_HANDLE;
        }
        if (renderer->m_rawImageView != VK_NULL_HANDLE) {
            vkDestroyImageView(renderer->m_device_p, renderer->m_rawImageView, nullptr);
            renderer->m_rawImageView = VK_NULL_HANDLE;
        }
        if (renderer->m_rawImage != VK_NULL_HANDLE && renderer->m_allocator_p != VK_NULL_HANDLE) {
            vmaDestroyImage(renderer->m_allocator_p, renderer->m_rawImage, renderer->m_rawImageAllocation);
            renderer->m_rawImage = VK_NULL_HANDLE;
            renderer->m_rawImageAllocation = VK_NULL_HANDLE;
        }
    }

}