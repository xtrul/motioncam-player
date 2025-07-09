#include "Graphics/ComputePipeline.h"
#include "Graphics/Renderer_VK.h"
#include "Graphics/VulkanHelpers.h"
#include "Graphics/ImageResource.h"
#include "Utils/DebugLog.h"
#include <filesystem>
#include <chrono>

extern std::string g_AppBasePath;

namespace ComputePipeline {

bool createRawToYuvPipeline(Renderer_VK* renderer) {
    namespace fs = std::filesystem;
    fs::path shaderPath = fs::path(g_AppBasePath) / "shaders_spv" / "raw_to_yuv422.comp.spv";
    auto code = VulkanHelpers::readFile(shaderPath.string());
    VkShaderModule module = VulkanHelpers::createShaderModule(renderer->m_device_p, code);
    LogProRes("[GPU] Creating RAW->YUV compute pipeline");

    VkDescriptorSetLayoutBinding bindings[2]{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutCI{};
    layoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutCI.bindingCount = 2;
    layoutCI.pBindings = bindings;
    VK_CHECK_RENDERER(vkCreateDescriptorSetLayout(renderer->m_device_p, &layoutCI, nullptr, &renderer->m_computeDescriptorSetLayout));

    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcRange.offset = 0;
    pcRange.size = sizeof(int) * 2;

    VkPipelineLayoutCreateInfo pli{};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &renderer->m_computeDescriptorSetLayout;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pcRange;
    VK_CHECK_RENDERER(vkCreatePipelineLayout(renderer->m_device_p, &pli, nullptr, &renderer->m_computePipelineLayout));

    VkPipelineShaderStageCreateInfo stage{};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = module;
    stage.pName = "main";

    VkComputePipelineCreateInfo cp{};
    cp.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cp.stage = stage;
    cp.layout = renderer->m_computePipelineLayout;
    VK_CHECK_RENDERER(vkCreateComputePipelines(renderer->m_device_p, VK_NULL_HANDLE, 1, &cp, nullptr, &renderer->m_computePipeline));

    vkDestroyShaderModule(renderer->m_device_p, module, nullptr);

    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = 1;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[1].descriptorCount = 1;
    VkDescriptorPoolCreateInfo poolCI{};
    poolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolCI.maxSets = 1;
    poolCI.poolSizeCount = 2;
    poolCI.pPoolSizes = poolSizes;
    VK_CHECK_RENDERER(vkCreateDescriptorPool(renderer->m_device_p, &poolCI, nullptr, &renderer->m_computeDescriptorPool));

    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = renderer->m_computeDescriptorPool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &renderer->m_computeDescriptorSetLayout;
    VK_CHECK_RENDERER(vkAllocateDescriptorSets(renderer->m_device_p, &ai, &renderer->m_computeDescriptorSet));
    LogProRes("[GPU] Compute pipeline initialized");

    return true;
}

void cleanup(Renderer_VK* renderer) {
    LogProRes("[GPU] Destroying compute pipeline");
    if (renderer->m_computeDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(renderer->m_device_p, renderer->m_computeDescriptorPool, nullptr);
        renderer->m_computeDescriptorPool = VK_NULL_HANDLE;
    }
    if (renderer->m_computePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(renderer->m_device_p, renderer->m_computePipeline, nullptr);
        renderer->m_computePipeline = VK_NULL_HANDLE;
    }
    if (renderer->m_computePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(renderer->m_device_p, renderer->m_computePipelineLayout, nullptr);
        renderer->m_computePipelineLayout = VK_NULL_HANDLE;
    }
    if (renderer->m_computeDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(renderer->m_device_p, renderer->m_computeDescriptorSetLayout, nullptr);
        renderer->m_computeDescriptorSetLayout = VK_NULL_HANDLE;
    }
}

bool dispatchRawToYuv(Renderer_VK* renderer, VkCommandBuffer cmd, int width, int height) {
    VkDescriptorImageInfo inputInfo{};
    inputInfo.sampler = renderer->m_rawImageSampler;
    inputInfo.imageView = renderer->m_rawImageView;
    inputInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorImageInfo outInfo{};
    outInfo.imageView = renderer->m_yuvImageView;
    outInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet writes[2]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = renderer->m_computeDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].pImageInfo = &inputInfo;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = renderer->m_computeDescriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].pImageInfo = &outInfo;
    vkUpdateDescriptorSets(renderer->m_device_p, 2, writes, 0, nullptr);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, renderer->m_computePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, renderer->m_computePipelineLayout, 0, 1, &renderer->m_computeDescriptorSet, 0, nullptr);

    struct Push { int w; int h; } push{ width, height };
    vkCmdPushConstants(cmd, renderer->m_computePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Push), &push);

    vkCmdDispatch(cmd, (uint32_t)((width + 15) / 16), (uint32_t)((height + 15) / 16), 1);
    return true;
}

bool runRawToYuvAndReadback(Renderer_VK* renderer, const uint16_t* raw,
                             int width, int height,
                             std::vector<uint16_t>& outPacked) {
    auto start = std::chrono::steady_clock::now();
    VkDeviceSize rawSize = static_cast<VkDeviceSize>(width) * height * sizeof(uint16_t);
    VkDeviceSize outSize = static_cast<VkDeviceSize>(width) * height * 4;

    VkBuffer stagingBuf = VK_NULL_HANDLE;
    VmaAllocation stagingAlloc = VK_NULL_HANDLE;
    VkBufferCreateInfo sbInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    sbInfo.size = rawSize;
    sbInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VmaAllocationCreateInfo sbAlloc{};
    sbAlloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    sbAlloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                    VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo sbAllocInfo{};
    VK_CHECK_RENDERER(vmaCreateBuffer(renderer->m_allocator_p, &sbInfo, &sbAlloc, &stagingBuf, &stagingAlloc, &sbAllocInfo));
    memcpy(sbAllocInfo.pMappedData, raw, rawSize);
    vmaFlushAllocation(renderer->m_allocator_p, stagingAlloc, 0, rawSize);

    VkBuffer readbackBuf = VK_NULL_HANDLE;
    VmaAllocation readbackAlloc = VK_NULL_HANDLE;
    VkBufferCreateInfo rbInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    rbInfo.size = outSize;
    rbInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo rbAlloc{};
    rbAlloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    rbAlloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                    VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo rbAllocInfo{};
    VK_CHECK_RENDERER(vmaCreateBuffer(renderer->m_allocator_p, &rbInfo, &rbAlloc, &readbackBuf, &readbackAlloc, &rbAllocInfo));

    VkCommandBuffer cmd = VulkanHelpers::beginSingleTimeCommands(renderer->m_device_p, renderer->m_hostSiteCommandPool_p);

    ImageResource::transitionImageLayout(renderer->m_device_p, renderer->m_hostSiteCommandPool_p,
        renderer->m_graphicsQueue_p, renderer->m_rawImage,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0,0,0};
    region.imageExtent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };
    vkCmdCopyBufferToImage(cmd, stagingBuf, renderer->m_rawImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    ImageResource::transitionImageLayout(renderer->m_device_p, renderer->m_hostSiteCommandPool_p,
        renderer->m_graphicsQueue_p, renderer->m_rawImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    dispatchRawToYuv(renderer, cmd, width, height);

    ImageResource::transitionImageLayout(renderer->m_device_p, renderer->m_hostSiteCommandPool_p,
        renderer->m_graphicsQueue_p, renderer->m_yuvImage,
        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    VkBufferImageCopy rbRegion{};
    rbRegion.bufferOffset = 0;
    rbRegion.bufferRowLength = 0;
    rbRegion.bufferImageHeight = 0;
    rbRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    rbRegion.imageSubresource.mipLevel = 0;
    rbRegion.imageSubresource.baseArrayLayer = 0;
    rbRegion.imageSubresource.layerCount = 1;
    rbRegion.imageOffset = {0,0,0};
    rbRegion.imageExtent = { static_cast<uint32_t>((width + 1) / 2), static_cast<uint32_t>(height), 1 };
    vkCmdCopyImageToBuffer(cmd, renderer->m_yuvImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           readbackBuf, 1, &rbRegion);

    ImageResource::transitionImageLayout(renderer->m_device_p, renderer->m_hostSiteCommandPool_p,
        renderer->m_graphicsQueue_p, renderer->m_yuvImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);

    VulkanHelpers::endSingleTimeCommands(renderer->m_device_p, renderer->m_hostSiteCommandPool_p,
        renderer->m_graphicsQueue_p, cmd);

    vmaInvalidateAllocation(renderer->m_allocator_p, readbackAlloc, 0, outSize);
    outPacked.resize(static_cast<size_t>(outSize / sizeof(uint16_t)));
    memcpy(outPacked.data(), rbAllocInfo.pMappedData, outSize);

    vmaDestroyBuffer(renderer->m_allocator_p, stagingBuf, stagingAlloc);
    vmaDestroyBuffer(renderer->m_allocator_p, readbackBuf, readbackAlloc);
    auto end = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    LogProRes(std::string("[GPU] compute path duration=") + std::to_string(ms) + "ms");
    return true;
}

} // namespace ComputePipeline

