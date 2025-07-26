#include "Graphics/GpuYuvConverter.h"
#include "Graphics/GpuColorParams.h"
#include "Graphics/Renderer_VK.h"
#include "Graphics/VulkanHelpers.h"
#include "Graphics/ImageResource.h"
#include "Utils/DebugLog.h"
#include <vector>
#include <filesystem>
#include <chrono>
#include <sstream>

extern std::string g_AppBasePath;

GpuYuvConverter::GpuYuvConverter(Renderer_VK* renderer)
    : m_renderer(renderer) {}

GpuYuvConverter::~GpuYuvConverter() { cleanup(); }

bool GpuYuvConverter::init(int width, int height) {
    namespace fs = std::filesystem;
    fs::path rawToYuvPath = fs::path(g_AppBasePath) / "shaders_spv" / "raw_to_yuv422.comp.spv";
    auto code = VulkanHelpers::readFile(rawToYuvPath.string());
    VkShaderModule module = VulkanHelpers::createShaderModule(m_renderer->m_device_p, code);

    fs::path pass1Path = fs::path(g_AppBasePath) / "shaders_spv" / "demosaic_amaze_pass1.comp.spv";
    auto pass1Code = VulkanHelpers::readFile(pass1Path.string());
    VkShaderModule pass1Module = VulkanHelpers::createShaderModule(m_renderer->m_device_p, pass1Code);

    fs::path pass2Path = fs::path(g_AppBasePath) / "shaders_spv" / "rgb_to_yuv422.comp.spv";
    auto pass2Code = VulkanHelpers::readFile(pass2Path.string());
    VkShaderModule pass2Module = VulkanHelpers::createShaderModule(m_renderer->m_device_p, pass2Code);

    LogProRes("[GPU] Creating RAW->YUV compute pipelines");
    LogGpu("[GpuYuvConverter::init] width=" + std::to_string(width) + " height=" + std::to_string(height));
    LogGpu("[GpuYuvConverter::init] init start");

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

    VkDescriptorSetLayoutBinding bindings[3]{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutCI{};
    layoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutCI.bindingCount = 3;
    layoutCI.pBindings = bindings;
    VK_CHECK_RENDERER(vkCreateDescriptorSetLayout(m_renderer->m_device_p, &layoutCI, nullptr, &m_setLayout));

    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcRange.offset = 0;
    pcRange.size = 88;

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

    // AMaZE pass-1 pipeline
    VkPushConstantRange pc1Range{};
    pc1Range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pc1Range.offset = 0;
    pc1Range.size = 24;

    pli.pPushConstantRanges = &pc1Range;
    VK_CHECK_RENDERER(vkCreatePipelineLayout(m_renderer->m_device_p, &pli, nullptr, &m_amazePipelineLayout));

    stage.module = pass1Module;
    cp.stage = stage;
    cp.layout = m_amazePipelineLayout;
    VK_CHECK_RENDERER(vkCreateComputePipelines(m_renderer->m_device_p, VK_NULL_HANDLE, 1, &cp, nullptr, &m_amazePipeline));

    // RGB to YUV pipeline
    VkPushConstantRange pc2Range{};
    pc2Range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pc2Range.offset = 0;
    pc2Range.size = 16;

    pli.pPushConstantRanges = &pc2Range;
    VK_CHECK_RENDERER(vkCreatePipelineLayout(m_renderer->m_device_p, &pli, nullptr, &m_rgb2yuvPipelineLayout));

    stage.module = pass2Module;
    cp.stage = stage;
    cp.layout = m_rgb2yuvPipelineLayout;
    VK_CHECK_RENDERER(vkCreateComputePipelines(m_renderer->m_device_p, VK_NULL_HANDLE, 1, &cp, nullptr, &m_rgb2yuvPipeline));

    vkDestroyShaderModule(m_renderer->m_device_p, module, nullptr);
    vkDestroyShaderModule(m_renderer->m_device_p, pass1Module, nullptr);
    vkDestroyShaderModule(m_renderer->m_device_p, pass2Module, nullptr);

    VkDescriptorPoolSize poolSizes[3]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[0].descriptorCount = 1;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[1].descriptorCount = 1;
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[2].descriptorCount = 1;
    VkDescriptorPoolCreateInfo poolCI{};
    poolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolCI.maxSets = 1;
    poolCI.poolSizeCount = 3;
    poolCI.pPoolSizes = poolSizes;
    VK_CHECK_RENDERER(vkCreateDescriptorPool(m_renderer->m_device_p, &poolCI, nullptr, &m_descPool));

    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = m_descPool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &m_setLayout;
    VK_CHECK_RENDERER(vkAllocateDescriptorSets(m_renderer->m_device_p, &ai, &m_descSet));
    LogProRes("[GPU] Compute pipeline initialized");
    LogGpu("[GpuYuvConverter::init] descriptor sets allocated");
    LogGpu("[GpuYuvConverter::init] creating images");

    // Create RGBA32F temporary image for pass-1 output
    VkImageCreateInfo rgbInfo{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    rgbInfo.imageType = VK_IMAGE_TYPE_2D;
    rgbInfo.extent.width = static_cast<uint32_t>(width);
    rgbInfo.extent.height = static_cast<uint32_t>(height);
    rgbInfo.extent.depth = 1;
    rgbInfo.mipLevels = 1;
    rgbInfo.arrayLayers = 1;
    rgbInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    rgbInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    rgbInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    rgbInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT;
    rgbInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    rgbInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    VmaAllocationCreateInfo rgbAllocInfo{};
    rgbAllocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    VK_CHECK_RENDERER(vmaCreateImage(m_renderer->m_allocator_p, &rgbInfo, &rgbAllocInfo,
                                     &m_rgbImage, &m_rgbAlloc, nullptr));

    VkImageViewCreateInfo rgbViewCI{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    rgbViewCI.image = m_rgbImage;
    rgbViewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
    rgbViewCI.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    rgbViewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    rgbViewCI.subresourceRange.baseMipLevel = 0;
    rgbViewCI.subresourceRange.levelCount = 1;
    rgbViewCI.subresourceRange.baseArrayLayer = 0;
    rgbViewCI.subresourceRange.layerCount = 1;
    VK_CHECK_RENDERER(vkCreateImageView(m_renderer->m_device_p, &rgbViewCI, nullptr, &m_rgbView));

    ImageResource::transitionImageLayout(
        m_renderer->m_device_p,
        m_cmdPool,
        m_renderer->m_graphicsQueue_p,
        m_rgbImage,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_GENERAL);

    // Create RGBA32UI image for compute output (macropixels)
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = static_cast<uint32_t>((width + 1) / 2);
    imageInfo.extent.height = static_cast<uint32_t>(height);
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R32G32B32A32_UINT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    VK_CHECK_RENDERER(vmaCreateImage(m_renderer->m_allocator_p, &imageInfo, &allocInfo,
                                     &m_yuvImage, &m_yuvAlloc, nullptr));

    VkImageViewCreateInfo viewInfo{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    viewInfo.image = m_yuvImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R32G32B32A32_UINT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    VK_CHECK_RENDERER(vkCreateImageView(m_renderer->m_device_p, &viewInfo, nullptr, &m_yuvView));

    ImageResource::transitionImageLayout(
        m_renderer->m_device_p,
        m_cmdPool,
        m_renderer->m_graphicsQueue_p,
        m_yuvImage,
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
    rawInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
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


    ImageResource::transitionImageLayout(
        m_renderer->m_device_p,
        m_cmdPool,
        m_renderer->m_graphicsQueue_p,
        m_rawImage,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_GENERAL);

    // Create debug buffer for shader diagnostics
    VkBufferCreateInfo dbgInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    dbgInfo.size = sizeof(float) * 4;
    dbgInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

    VmaAllocationCreateInfo dbgAlloc{};
    dbgAlloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    dbgAlloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                     VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VK_CHECK_RENDERER(vmaCreateBuffer(m_renderer->m_allocator_p, &dbgInfo, &dbgAlloc,
                                      &m_debugBuf, &m_debugAlloc, nullptr));

    LogGpu("[GpuYuvConverter::init] resources created");

    return true;
}

void GpuYuvConverter::cleanup() {
    if (m_rawView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_renderer->m_device_p, m_rawView, nullptr);
        m_rawView = VK_NULL_HANDLE;
    }
    if (m_rawImage != VK_NULL_HANDLE) {
        vmaDestroyImage(m_renderer->m_allocator_p, m_rawImage, m_rawAlloc);
        m_rawImage = VK_NULL_HANDLE;
        m_rawAlloc = VK_NULL_HANDLE;
    }
    if (m_yuvView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_renderer->m_device_p, m_yuvView, nullptr);
        m_yuvView = VK_NULL_HANDLE;
    }
    if (m_yuvImage != VK_NULL_HANDLE) {
        vmaDestroyImage(m_renderer->m_allocator_p, m_yuvImage, m_yuvAlloc);
        m_yuvImage = VK_NULL_HANDLE;
        m_yuvAlloc = VK_NULL_HANDLE;
    }
    if (m_rgbView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_renderer->m_device_p, m_rgbView, nullptr);
        m_rgbView = VK_NULL_HANDLE;
    }
    if (m_rgbImage != VK_NULL_HANDLE) {
        vmaDestroyImage(m_renderer->m_allocator_p, m_rgbImage, m_rgbAlloc);
        m_rgbImage = VK_NULL_HANDLE;
        m_rgbAlloc = VK_NULL_HANDLE;
    }
    if (m_descPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_renderer->m_device_p, m_descPool, nullptr);
        m_descPool = VK_NULL_HANDLE;
    }
    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_renderer->m_device_p, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
    if (m_amazePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_renderer->m_device_p, m_amazePipeline, nullptr);
        m_amazePipeline = VK_NULL_HANDLE;
    }
    if (m_rgb2yuvPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_renderer->m_device_p, m_rgb2yuvPipeline, nullptr);
        m_rgb2yuvPipeline = VK_NULL_HANDLE;
    }
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_renderer->m_device_p, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }
    if (m_amazePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_renderer->m_device_p, m_amazePipelineLayout, nullptr);
        m_amazePipelineLayout = VK_NULL_HANDLE;
    }
    if (m_rgb2yuvPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_renderer->m_device_p, m_rgb2yuvPipelineLayout, nullptr);
        m_rgb2yuvPipelineLayout = VK_NULL_HANDLE;
    }
    if (m_setLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_renderer->m_device_p, m_setLayout, nullptr);
        m_setLayout = VK_NULL_HANDLE;
    }
    if (m_cmdPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_renderer->m_device_p, m_cmdPool, nullptr);
        m_cmdPool = VK_NULL_HANDLE;
    }
    if (m_debugBuf != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_renderer->m_allocator_p, m_debugBuf, m_debugAlloc);
        m_debugBuf = VK_NULL_HANDLE;
        m_debugAlloc = VK_NULL_HANDLE;
    }
}

bool GpuYuvConverter::convertToFrame(const uint16_t* raw, int width, int height,
                                     AVFrame* frame,
                                     const GpuColorParams& params) {
    LogProRes("[GPU] convertToFrame invoked");
    LogGpu("[GpuYuvConverter::convert] width=" + std::to_string(width) + " height=" + std::to_string(height));
    VkDeviceSize rawSize = static_cast<VkDeviceSize>(width) * height * sizeof(uint16_t);
    VkDeviceSize outSize = static_cast<VkDeviceSize>(( (width + 1) / 2 )) * height * sizeof(uint32_t) * 4;

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

    // Clear debug buffer
    void* dbgPtr = nullptr;
    vmaMapMemory(m_renderer->m_allocator_p, m_debugAlloc, &dbgPtr);
    memset(dbgPtr, 0, sizeof(float) * 4);
    vmaFlushAllocation(m_renderer->m_allocator_p, m_debugAlloc, 0, VK_WHOLE_SIZE);
    vmaUnmapMemory(m_renderer->m_allocator_p, m_debugAlloc);

    VkBuffer readbackBuf = VK_NULL_HANDLE;
    VmaAllocation readbackAlloc = VK_NULL_HANDLE;
    VmaAllocationInfo rbAllocInfo{};
    VkBufferCreateInfo rbInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    rbInfo.size = outSize;
    rbInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo rbAlloc{};
    rbAlloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    rbAlloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                    VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VK_CHECK_RENDERER(vmaCreateBuffer(m_renderer->m_allocator_p, &rbInfo, &rbAlloc,
                                      &readbackBuf, &readbackAlloc, &rbAllocInfo));

    VkCommandBuffer cmd = VulkanHelpers::beginSingleTimeCommands(m_renderer->m_device_p,
                                                                m_cmdPool);

    VkImageMemoryBarrier bar1{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    bar1.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    bar1.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    bar1.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bar1.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bar1.image = m_rawImage;
    bar1.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    bar1.subresourceRange.baseMipLevel = 0;
    bar1.subresourceRange.levelCount = 1;
    bar1.subresourceRange.baseArrayLayer = 0;
    bar1.subresourceRange.layerCount = 1;
    bar1.srcAccessMask = 0;
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
    bar2.newLayout = VK_IMAGE_LAYOUT_GENERAL;
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
    inputInfo.imageView = m_rawView;
    inputInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorImageInfo outInfo{};
    outInfo.imageView = m_rgbView;
    outInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorBufferInfo dbgInfoWrite{};
    dbgInfoWrite.buffer = m_debugBuf;
    dbgInfoWrite.offset = 0;
    dbgInfoWrite.range = sizeof(float) * 4;

    VkWriteDescriptorSet writes[3]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = m_descSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[0].pImageInfo = &inputInfo;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = m_descSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].pImageInfo = &outInfo;
    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = m_descSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorCount = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[2].pBufferInfo = &dbgInfoWrite;
    vkUpdateDescriptorSets(m_renderer->m_device_p, 3, writes, 0, nullptr);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_amazePipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_amazePipelineLayout, 0, 1, &m_descSet, 0, nullptr);

    struct PC1 {
        uint32_t width;
        uint32_t height;
        uint32_t cfaType;
        float black;
        float white;
        float invScale;
    } pc1{};
    pc1.width = width;
    pc1.height = height;
    pc1.cfaType = params.cfaType;
    pc1.black = static_cast<float>(params.black);
    pc1.white = static_cast<float>(params.white);
    pc1.invScale = 1.0f / (pc1.white - pc1.black);
    {
        std::ostringstream oss;
        oss << "[GpuYuvConverter::convert] PC1 width=" << pc1.width
            << " height=" << pc1.height
            << " cfaType=" << pc1.cfaType
            << " black=" << pc1.black
            << " white=" << pc1.white;
        LogGpu(oss.str());
    }
    vkCmdPushConstants(cmd, m_amazePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                       0, sizeof(PC1), &pc1);
    vkCmdDispatch(cmd, (uint32_t)((width + 31) / 32), (uint32_t)((height + 15) / 16), 1);

    // barrier RGB write -> read
    VkImageMemoryBarrier rgbBarrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    rgbBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    rgbBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    rgbBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    rgbBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    rgbBarrier.image = m_rgbImage;
    rgbBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    rgbBarrier.subresourceRange.baseMipLevel = 0;
    rgbBarrier.subresourceRange.levelCount = 1;
    rgbBarrier.subresourceRange.baseArrayLayer = 0;
    rgbBarrier.subresourceRange.layerCount = 1;
    rgbBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    rgbBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0,nullptr, 0,nullptr, 1,&rgbBarrier);

    inputInfo.imageView = m_rgbView;
    outInfo.imageView = m_yuvView;
    writes[0].pImageInfo = &inputInfo;
    writes[1].pImageInfo = &outInfo;
    writes[2].pBufferInfo = &dbgInfoWrite;
    vkUpdateDescriptorSets(m_renderer->m_device_p, 3, writes, 0, nullptr);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_rgb2yuvPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_rgb2yuvPipelineLayout, 0, 1, &m_descSet, 0, nullptr);

    struct PC2 { uint32_t width, height, fullSwing; float hueSmooth; } pc2{};
    pc2.width = width;
    pc2.height = height;
    pc2.fullSwing = params.fullSwing;
    pc2.hueSmooth = 0.0f;
    {
        std::ostringstream oss;
        oss << "[GpuYuvConverter::convert] PC2 width=" << pc2.width
            << " height=" << pc2.height
            << " fullSwing=" << pc2.fullSwing
            << " hueSmooth=" << pc2.hueSmooth;
        LogGpu(oss.str());
    }
    vkCmdPushConstants(cmd, m_rgb2yuvPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PC2), &pc2);
    vkCmdDispatch(cmd, (uint32_t)((width + 31) / 32), (uint32_t)((height + 15) / 16), 1);
    LogProRes("[GPU] compute dispatched");

    VkImageMemoryBarrier barOut{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barOut.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barOut.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barOut.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barOut.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barOut.image = m_yuvImage;
    barOut.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barOut.subresourceRange.baseMipLevel = 0;
    barOut.subresourceRange.levelCount = 1;
    barOut.subresourceRange.baseArrayLayer = 0;
    barOut.subresourceRange.layerCount = 1;
    barOut.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barOut.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barOut);

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
    vkCmdCopyImageToBuffer(cmd, m_yuvImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           readbackBuf, 1, &rbRegion);

    barOut.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barOut.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barOut.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barOut.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barOut);
    VulkanHelpers::endSingleTimeCommands(m_renderer->m_device_p, m_cmdPool,
        m_renderer->m_graphicsQueue_p, cmd);

    vmaInvalidateAllocation(m_renderer->m_allocator_p, readbackAlloc, 0, outSize);

    float dbgVals[4] = {};
    vmaInvalidateAllocation(m_renderer->m_allocator_p, m_debugAlloc, 0, VK_WHOLE_SIZE);
    void* dbgMap = nullptr;
    vmaMapMemory(m_renderer->m_allocator_p, m_debugAlloc, &dbgMap);
    memcpy(dbgVals, dbgMap, sizeof(dbgVals));
    vmaUnmapMemory(m_renderer->m_allocator_p, m_debugAlloc);
    {
        std::ostringstream oss;
        oss << "[GpuYuvConverter::convert] debug0 norm=" << dbgVals[0]
            << " raw=" << dbgVals[1]
            << " invScale=" << dbgVals[2];
        LogGpu(oss.str());
    }

    const uint32_t* macropix = static_cast<const uint32_t*>(rbAllocInfo.pMappedData);
    uint32_t macroPitch = ((width + 1) / 2) * 4;
    for(int y=0;y<height;++y){
        const uint32_t* row = macropix + y * macroPitch;
        uint16_t* dstY = reinterpret_cast<uint16_t*>(frame->data[0] + y*frame->linesize[0]);
        uint16_t* dstU = reinterpret_cast<uint16_t*>(frame->data[1] + y*frame->linesize[1]);
        uint16_t* dstV = reinterpret_cast<uint16_t*>(frame->data[2] + y*frame->linesize[2]);
        for(int x=0;x<(width+1)/2;++x){
            uint32_t y0 = row[x*4];
            uint32_t u  = row[x*4+1];
            uint32_t y1 = row[x*4+2];
            uint32_t v  = row[x*4+3];
            dstY[x*2]     = static_cast<uint16_t>(y0);
            dstY[x*2 + 1] = static_cast<uint16_t>(y1);
            dstU[x] = static_cast<uint16_t>(u);
            dstV[x] = static_cast<uint16_t>(v);
        }
    }

    {
        const uint32_t* first = macropix;
        std::ostringstream oss;
        oss << "[GpuYuvConverter::convert] first macropixel Y0=" << first[0]
            << " U=" << first[1] << " Y1=" << first[2] << " V=" << first[3];
        LogGpu(oss.str());
    }

    LogProRes("[GPU] readback complete");

    vmaDestroyBuffer(m_renderer->m_allocator_p, stagingBuf, stagingAlloc);
    vmaDestroyBuffer(m_renderer->m_allocator_p, readbackBuf, readbackAlloc);
    return true;
}
