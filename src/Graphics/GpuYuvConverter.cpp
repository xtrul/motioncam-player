#include "Graphics/GpuYuvConverter.h"
#include "Graphics/Renderer_VK.h"
#include "Graphics/VulkanHelpers.h"
#include "Graphics/ImageResource.h"
#include "Utils/DebugLog.h"
#include <vector>
#include <filesystem>
#include <chrono>

extern std::string g_AppBasePath;

GpuYuvConverter::GpuYuvConverter(Renderer_VK* renderer)
    : m_renderer(renderer) {}

GpuYuvConverter::~GpuYuvConverter() { cleanup(); }

bool GpuYuvConverter::init(int width, int height) {
    namespace fs = std::filesystem;
    fs::path shaderPath = fs::path(g_AppBasePath) / "shaders_spv" / "raw_to_yuv422.comp.spv";
    auto code = VulkanHelpers::readFile(shaderPath.string());
    VkShaderModule module = VulkanHelpers::createShaderModule(m_renderer->m_device_p, code);
    LogProRes("[GPU] Creating RAW->YUV compute pipeline");
    LogProRes("[GPU] init start");

    // Create a private command pool for all converter operations
    uint32_t graphicsFamily = 0;
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_renderer->m_physicalDevice_p, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_renderer->m_physicalDevice_p, &queueFamilyCount, families.data());
    for (uint32_t i = 0; i < queueFamilyCount; ++i) {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { graphicsFamily = i; break; }
    }

    VkCommandPoolCreateInfo ci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    ci.queueFamilyIndex = graphicsFamily;
    ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    VK_CHECK_RENDERER(vkCreateCommandPool(m_renderer->m_device_p, &ci, nullptr, &m_cmdPool));

    VkDescriptorSetLayoutBinding bindings[4]{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutCI{};
    layoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutCI.bindingCount = 4;
    layoutCI.pBindings = bindings;
    VK_CHECK_RENDERER(vkCreateDescriptorSetLayout(m_renderer->m_device_p, &layoutCI, nullptr, &m_setLayout));

    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcRange.offset = 0;
    pcRange.size = sizeof(int) * 2;

    VkPipelineLayoutCreateInfo pli{};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &m_setLayout;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pcRange;
    VK_CHECK_RENDERER(vkCreatePipelineLayout(m_renderer->m_device_p, &pli, nullptr, &m_pipelineLayout));

    VkPipelineShaderStageCreateInfo stage{};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = module;
    stage.pName = "main";

    VkComputePipelineCreateInfo cp{};
    cp.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cp.stage = stage;
    cp.layout = m_pipelineLayout;
    VK_CHECK_RENDERER(vkCreateComputePipelines(m_renderer->m_device_p, VK_NULL_HANDLE, 1, &cp, nullptr, &m_pipeline));

    vkDestroyShaderModule(m_renderer->m_device_p, module, nullptr);

    VkDescriptorPoolSize poolSizes[4]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = 1;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[1].descriptorCount = 1;
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[2].descriptorCount = 1;
    poolSizes[3].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[3].descriptorCount = 1;
    VkDescriptorPoolCreateInfo poolCI{};
    poolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolCI.maxSets = 1;
    poolCI.poolSizeCount = 4;
    poolCI.pPoolSizes = poolSizes;
    VK_CHECK_RENDERER(vkCreateDescriptorPool(m_renderer->m_device_p, &poolCI, nullptr, &m_descPool));

    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = m_descPool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &m_setLayout;
    VK_CHECK_RENDERER(vkAllocateDescriptorSets(m_renderer->m_device_p, &ai, &m_descSet));
    LogProRes("[GPU] Compute pipeline initialized");
    LogProRes("[GPU] init complete");

    // Create Y plane image
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
    imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    VK_CHECK_RENDERER(vmaCreateImage(m_renderer->m_allocator_p, &imageInfo, &allocInfo, &m_yImage, &m_yAlloc, nullptr));

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_yImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R16_UINT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    VK_CHECK_RENDERER(vkCreateImageView(m_renderer->m_device_p, &viewInfo, nullptr, &m_yView));

    ImageResource::transitionImageLayout(
        m_renderer->m_device_p,
        m_cmdPool,
        m_renderer->m_graphicsQueue_p,
        m_yImage,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_GENERAL);

    // Create U plane image
    imageInfo.extent.width = static_cast<uint32_t>((width + 1) / 2);
    imageInfo.format = VK_FORMAT_R16_UINT;
    VK_CHECK_RENDERER(vmaCreateImage(m_renderer->m_allocator_p, &imageInfo, &allocInfo, &m_uImage, &m_uAlloc, nullptr));
    viewInfo.image = m_uImage;
    VK_CHECK_RENDERER(vkCreateImageView(m_renderer->m_device_p, &viewInfo, nullptr, &m_uView));
    ImageResource::transitionImageLayout(
        m_renderer->m_device_p,
        m_cmdPool,
        m_renderer->m_graphicsQueue_p,
        m_uImage,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_GENERAL);

    // Create V plane image
    VK_CHECK_RENDERER(vmaCreateImage(m_renderer->m_allocator_p, &imageInfo, &allocInfo, &m_vImage, &m_vAlloc, nullptr));
    viewInfo.image = m_vImage;
    VK_CHECK_RENDERER(vkCreateImageView(m_renderer->m_device_p, &viewInfo, nullptr, &m_vView));
    ImageResource::transitionImageLayout(
        m_renderer->m_device_p,
        m_cmdPool,
        m_renderer->m_graphicsQueue_p,
        m_vImage,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_GENERAL);

    // Create private RAW image for input
    VkImageCreateInfo rawInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    rawInfo.imageType = VK_IMAGE_TYPE_2D;
    rawInfo.extent.width = static_cast<uint32_t>(width);
    rawInfo.extent.height = static_cast<uint32_t>(height);
    rawInfo.extent.depth = 1;
    rawInfo.mipLevels = 1;
    rawInfo.arrayLayers = 1;
    rawInfo.format = VK_FORMAT_R16_UINT;
    rawInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    rawInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    rawInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    rawInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    rawInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    VmaAllocationCreateInfo rawAllocInfo{};
    rawAllocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    VK_CHECK_RENDERER(vmaCreateImage(m_renderer->m_allocator_p, &rawInfo, &rawAllocInfo,
                                     &m_rawImage, &m_rawAlloc, nullptr));

    VkImageViewCreateInfo rawViewCI{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    rawViewCI.image = m_rawImage;
    rawViewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
    rawViewCI.format = VK_FORMAT_R16_UINT;
    rawViewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    rawViewCI.subresourceRange.baseMipLevel = 0;
    rawViewCI.subresourceRange.levelCount = 1;
    rawViewCI.subresourceRange.baseArrayLayer = 0;
    rawViewCI.subresourceRange.layerCount = 1;
    VK_CHECK_RENDERER(vkCreateImageView(m_renderer->m_device_p, &rawViewCI, nullptr, &m_rawView));

    VkSamplerCreateInfo samplerInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
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
    VK_CHECK_RENDERER(vkCreateSampler(m_renderer->m_device_p, &samplerInfo, nullptr, &m_rawSampler));

    ImageResource::transitionImageLayout(
        m_renderer->m_device_p,
        m_cmdPool,
        m_renderer->m_graphicsQueue_p,
        m_rawImage,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    return true;
}

void GpuYuvConverter::cleanup() {
    if (m_rawSampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_renderer->m_device_p, m_rawSampler, nullptr);
        m_rawSampler = VK_NULL_HANDLE;
    }
    if (m_rawView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_renderer->m_device_p, m_rawView, nullptr);
        m_rawView = VK_NULL_HANDLE;
    }
    if (m_rawImage != VK_NULL_HANDLE) {
        vmaDestroyImage(m_renderer->m_allocator_p, m_rawImage, m_rawAlloc);
        m_rawImage = VK_NULL_HANDLE;
        m_rawAlloc = VK_NULL_HANDLE;
    }
    if (m_yView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_renderer->m_device_p, m_yView, nullptr);
        m_yView = VK_NULL_HANDLE;
    }
    if (m_yImage != VK_NULL_HANDLE) {
        vmaDestroyImage(m_renderer->m_allocator_p, m_yImage, m_yAlloc);
        m_yImage = VK_NULL_HANDLE;
        m_yAlloc = VK_NULL_HANDLE;
    }
    if (m_uView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_renderer->m_device_p, m_uView, nullptr);
        m_uView = VK_NULL_HANDLE;
    }
    if (m_uImage != VK_NULL_HANDLE) {
        vmaDestroyImage(m_renderer->m_allocator_p, m_uImage, m_uAlloc);
        m_uImage = VK_NULL_HANDLE;
        m_uAlloc = VK_NULL_HANDLE;
    }
    if (m_vView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_renderer->m_device_p, m_vView, nullptr);
        m_vView = VK_NULL_HANDLE;
    }
    if (m_vImage != VK_NULL_HANDLE) {
        vmaDestroyImage(m_renderer->m_allocator_p, m_vImage, m_vAlloc);
        m_vImage = VK_NULL_HANDLE;
        m_vAlloc = VK_NULL_HANDLE;
    }
    if (m_descPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_renderer->m_device_p, m_descPool, nullptr);
        m_descPool = VK_NULL_HANDLE;
    }
    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_renderer->m_device_p, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_renderer->m_device_p, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }
    if (m_setLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_renderer->m_device_p, m_setLayout, nullptr);
        m_setLayout = VK_NULL_HANDLE;
    }
    if (m_cmdPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_renderer->m_device_p, m_cmdPool, nullptr);
        m_cmdPool = VK_NULL_HANDLE;
    }
}

bool GpuYuvConverter::convertAndReadback(const uint16_t* raw, int width, int height,
                                         std::vector<uint16_t>& outY,
                                         std::vector<uint16_t>& outU,
                                         std::vector<uint16_t>& outV) {
    LogProRes("[GPU] convertAndReadback invoked");
    VkDeviceSize rawSize = static_cast<VkDeviceSize>(width) * height * sizeof(uint16_t);
    VkDeviceSize ySize = static_cast<VkDeviceSize>(width) * height * sizeof(uint16_t);
    VkDeviceSize uvRow = static_cast<VkDeviceSize>((width + 1) / 2) * sizeof(uint16_t);
    VkDeviceSize uSize = uvRow * height;
    VkDeviceSize vSize = uSize;
    VkDeviceSize outSize = ySize + uSize + vSize;

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
    VK_CHECK_RENDERER(vmaCreateBuffer(m_renderer->m_allocator_p, &sbInfo, &sbAlloc,
                                      &stagingBuf, &stagingAlloc, &sbAllocInfo));
    memcpy(sbAllocInfo.pMappedData, raw, rawSize);
    vmaFlushAllocation(m_renderer->m_allocator_p, stagingAlloc, 0, rawSize);

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
    VK_CHECK_RENDERER(vmaCreateBuffer(m_renderer->m_allocator_p, &rbInfo, &rbAlloc,
                                      &readbackBuf, &readbackAlloc, &rbAllocInfo));

    VkCommandBuffer cmd = VulkanHelpers::beginSingleTimeCommands(m_renderer->m_device_p,
                                                                m_cmdPool);

    VkImageMemoryBarrier bar1{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    bar1.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    bar1.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    bar1.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bar1.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bar1.image = m_rawImage;
    bar1.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    bar1.subresourceRange.baseMipLevel = 0;
    bar1.subresourceRange.levelCount = 1;
    bar1.subresourceRange.baseArrayLayer = 0;
    bar1.subresourceRange.layerCount = 1;
    bar1.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    bar1.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &bar1);

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
    vkCmdCopyBufferToImage(cmd, stagingBuf, m_rawImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    VkImageMemoryBarrier bar2{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    bar2.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    bar2.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    bar2.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bar2.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bar2.image = m_rawImage;
    bar2.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    bar2.subresourceRange.baseMipLevel = 0;
    bar2.subresourceRange.levelCount = 1;
    bar2.subresourceRange.baseArrayLayer = 0;
    bar2.subresourceRange.layerCount = 1;
    bar2.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    bar2.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &bar2);

    VkDescriptorImageInfo inputInfo{};
    inputInfo.sampler = m_rawSampler;
    inputInfo.imageView = m_rawView;
    inputInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorImageInfo outYInfo{};
    outYInfo.imageView = m_yView;
    outYInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkDescriptorImageInfo outUInfo{};
    outUInfo.imageView = m_uView;
    outUInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkDescriptorImageInfo outVInfo{};
    outVInfo.imageView = m_vView;
    outVInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet writes[4]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = m_descSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].pImageInfo = &inputInfo;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = m_descSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].pImageInfo = &outYInfo;
    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = m_descSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorCount = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[2].pImageInfo = &outUInfo;
    writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[3].dstSet = m_descSet;
    writes[3].dstBinding = 3;
    writes[3].descriptorCount = 1;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[3].pImageInfo = &outVInfo;
    vkUpdateDescriptorSets(m_renderer->m_device_p, 4, writes, 0, nullptr);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout, 0, 1, &m_descSet, 0, nullptr);

    struct Push { int w; int h; } push{ width, height };
    vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Push), &push);

    vkCmdDispatch(cmd, (uint32_t)((width + 15) / 16), (uint32_t)((height + 15) / 16), 1);
    LogProRes("[GPU] compute dispatched");

    VkImageMemoryBarrier bars[3]{};
    for(int i=0;i<3;++i){
        bars[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        bars[i].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        bars[i].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        bars[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bars[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bars[i].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        bars[i].subresourceRange.baseMipLevel = 0;
        bars[i].subresourceRange.levelCount = 1;
        bars[i].subresourceRange.baseArrayLayer = 0;
        bars[i].subresourceRange.layerCount = 1;
        bars[i].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        bars[i].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    }
    bars[0].image = m_yImage;
    bars[1].image = m_uImage;
    bars[2].image = m_vImage;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        3, bars);

    VkBufferImageCopy copyY{};
    copyY.bufferOffset = 0;
    copyY.bufferRowLength = 0;
    copyY.bufferImageHeight = 0;
    copyY.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copyY.imageSubresource.mipLevel = 0;
    copyY.imageSubresource.baseArrayLayer = 0;
    copyY.imageSubresource.layerCount = 1;
    copyY.imageOffset = {0,0,0};
    copyY.imageExtent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };
    VkBufferImageCopy copyU = copyY;
    copyU.bufferOffset = ySize;
    copyU.imageExtent.width = static_cast<uint32_t>((width + 1) / 2);
    VkBufferImageCopy copyV = copyU;
    copyV.bufferOffset = ySize + uSize;

    vkCmdCopyImageToBuffer(cmd, m_yImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           readbackBuf, 1, &copyY);
    vkCmdCopyImageToBuffer(cmd, m_uImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           readbackBuf, 1, &copyU);
    vkCmdCopyImageToBuffer(cmd, m_vImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           readbackBuf, 1, &copyV);

    for(int i=0;i<3;++i){
        bars[i].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        bars[i].newLayout = VK_IMAGE_LAYOUT_GENERAL;
        bars[i].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        bars[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    }
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        3, bars);
    VulkanHelpers::endSingleTimeCommands(m_renderer->m_device_p, m_cmdPool,
        m_renderer->m_graphicsQueue_p, cmd);

    vmaInvalidateAllocation(m_renderer->m_allocator_p, readbackAlloc, 0, outSize);
    outY.resize(static_cast<size_t>(ySize / sizeof(uint16_t)));
    outU.resize(static_cast<size_t>(uSize / sizeof(uint16_t)));
    outV.resize(static_cast<size_t>(vSize / sizeof(uint16_t)));
    memcpy(outY.data(), rbAllocInfo.pMappedData, ySize);
    memcpy(outU.data(), static_cast<uint8_t*>(rbAllocInfo.pMappedData) + ySize, uSize);
    memcpy(outV.data(), static_cast<uint8_t*>(rbAllocInfo.pMappedData) + ySize + uSize, vSize);
    LogProRes("[GPU] readback complete");

    vmaDestroyBuffer(m_renderer->m_allocator_p, stagingBuf, stagingAlloc);
    vmaDestroyBuffer(m_renderer->m_allocator_p, readbackBuf, readbackAlloc);
    return true;
}
