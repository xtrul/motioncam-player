#define VMA_IMPLEMENTATION
#include "Utils/vma_usage.h"

#include "Graphics/Renderer_VK.h"
#include "Graphics/ImageResource.h"
#include "Graphics/Pipeline.h"
#include "Graphics/Descriptor.h"
#include "Graphics/VulkanHelpers.h"
#include "Utils/DebugLog.h"
#include "Utils/RawFrameBuffer.h"
#include "Utils/OrientationUtils.h"

#include <nlohmann/json.hpp>
#include <stdexcept>
#include <array>
#include <iostream>
#include <fstream>
#include <algorithm>
#include <filesystem>
#include <glm/gtc/matrix_transform.hpp>
#include <filesystem>

extern std::string g_AppBasePath;


Renderer_VK::Renderer_VK(VkPhysicalDevice physicalDevice, VkDevice device, VmaAllocator allocator, VkQueue graphicsQueue, VkCommandPool commandPool)
    : m_physicalDevice_p(physicalDevice),
    m_device_p(device),
    m_allocator_p(allocator),
    m_graphicsQueue_p(graphicsQueue),
    m_hostSiteCommandPool_p(commandPool),
    m_rawImage(VK_NULL_HANDLE),
    m_rawImageAllocation(VK_NULL_HANDLE),
    m_rawImageView(VK_NULL_HANDLE),
    m_rawImageSampler(VK_NULL_HANDLE),
    m_descriptorSetLayout(VK_NULL_HANDLE),
    m_pipelineLayout(VK_NULL_HANDLE),
    m_graphicsPipeline(VK_NULL_HANDLE),
    m_descriptorPool(VK_NULL_HANDLE),
    m_previewPipeline(VK_NULL_HANDLE),
    m_previewPipelineLayout(VK_NULL_HANDLE),
    m_previewSetLayout(VK_NULL_HANDLE),
    m_previewDescPool(VK_NULL_HANDLE),
    m_previewDescSet(VK_NULL_HANDLE),
    m_currentRawW(0), m_currentRawH(0),
    m_zoomNativePixels(false),
    m_panX(0.0f), m_panY(0.0f),
    m_swapChainImageCount(0)
{
    LogToFile("[Renderer_VK] Constructor called.");
}

Renderer_VK::~Renderer_VK() {
    LogToFile("[Renderer_VK] Destructor called.");
}

bool Renderer_VK::init(VkRenderPass renderPass, uint32_t swapChainImageCount) {
    LogToFile(std::string("[Renderer_VK::init] Initializing with swapChainImageCount: ") + std::to_string(swapChainImageCount));
    m_swapChainImageCount = swapChainImageCount;

    if (!Descriptor::createDescriptorSetLayout(this)) { LogToFile("[Renderer_VK::init] ERROR: Failed to create descriptor set layout."); return false; }
    LogToFile("[Renderer_VK::init] Descriptor set layout created.");

    if (!ImageResource::createRawImageResources(this, 1, 1)) { LogToFile("[Renderer_VK::init] ERROR: Failed to create initial raw image resources."); return false; }
    LogToFile("[Renderer_VK::init] Initial raw image resources created.");
    if (!ImageResource::createPreviewImage(this, 1, 1)) { LogToFile("[Renderer_VK::init] ERROR: Failed to create preview image."); return false; }
    LogToFile("[Renderer_VK::init] Preview image created.");

    {
        namespace fs = std::filesystem;
        fs::path spv = fs::path(g_AppBasePath) / "shaders_spv" / "raw_to_rgba.comp.spv";
        auto code = VulkanHelpers::readFile(spv.string());
        VkShaderModule mod = VulkanHelpers::createShaderModule(m_device_p, code);

        VkDescriptorSetLayoutBinding binds[2]{};
        binds[0].binding = 0;
        binds[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        binds[0].descriptorCount = 1;
        binds[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        binds[1].binding = 1;
        binds[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        binds[1].descriptorCount = 1;
        binds[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo lci{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        lci.bindingCount = 2;
        lci.pBindings = binds;
        VK_CHECK_RENDERER(vkCreateDescriptorSetLayout(m_device_p, &lci, nullptr, &m_previewSetLayout));

        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pcr.offset = 0;
        pcr.size = 80;

        VkPipelineLayoutCreateInfo pli{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        pli.setLayoutCount = 1;
        pli.pSetLayouts = &m_previewSetLayout;
        pli.pushConstantRangeCount = 1;
        pli.pPushConstantRanges = &pcr;
        VK_CHECK_RENDERER(vkCreatePipelineLayout(m_device_p, &pli, nullptr, &m_previewPipelineLayout));

        VkPipelineShaderStageCreateInfo stage{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
        stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stage.module = mod;
        stage.pName = "main";

        VkComputePipelineCreateInfo cpi{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
        cpi.stage = stage;
        cpi.layout = m_previewPipelineLayout;
        VK_CHECK_RENDERER(vkCreateComputePipelines(m_device_p, VK_NULL_HANDLE, 1, &cpi, nullptr, &m_previewPipeline));
        vkDestroyShaderModule(m_device_p, mod, nullptr);

        VkDescriptorPoolSize ps[2]{};
        ps[0].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        ps[0].descriptorCount = 1;
        ps[1].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        ps[1].descriptorCount = 1;
        VkDescriptorPoolCreateInfo pci{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        pci.maxSets = 1;
        pci.poolSizeCount = 2;
        pci.pPoolSizes = ps;
        VK_CHECK_RENDERER(vkCreateDescriptorPool(m_device_p, &pci, nullptr, &m_previewDescPool));

        VkDescriptorSetAllocateInfo ai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        ai.descriptorPool = m_previewDescPool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &m_previewSetLayout;
        VK_CHECK_RENDERER(vkAllocateDescriptorSets(m_device_p, &ai, &m_previewDescSet));
    }

    onSwapChainRecreated(renderPass, swapChainImageCount);

    LogToFile("[Renderer_VK::init] Initialization successful.");
    return true;
}

void Renderer_VK::cleanup() {
    LogToFile("[Renderer_VK::cleanup] Starting cleanup...");
    Pipeline::cleanupSwapChainResources(this);
    ImageResource::cleanupRawImageResources(this);
    ImageResource::cleanupPreviewImage(this);

    if (m_descriptorSetLayout != VK_NULL_HANDLE) {
        LogToFile("[Renderer_VK::cleanup] Destroying descriptor set layout.");
        vkDestroyDescriptorSetLayout(m_device_p, m_descriptorSetLayout, nullptr);
        m_descriptorSetLayout = VK_NULL_HANDLE;
    }

    if (m_previewPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device_p, m_previewPipeline, nullptr);
        m_previewPipeline = VK_NULL_HANDLE;
    }
    if (m_previewPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device_p, m_previewPipelineLayout, nullptr);
        m_previewPipelineLayout = VK_NULL_HANDLE;
    }
    if (m_previewSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device_p, m_previewSetLayout, nullptr);
        m_previewSetLayout = VK_NULL_HANDLE;
    }
    if (m_previewDescPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device_p, m_previewDescPool, nullptr);
        m_previewDescPool = VK_NULL_HANDLE;
    }
    LogToFile("[Renderer_VK::cleanup] Cleanup complete.");
}

void Renderer_VK::onSwapChainRecreated(VkRenderPass renderPass, uint32_t swapChainImageCount) {
    LogToFile(std::string("[Renderer_VK::onSwapChainRecreated] Recreating for ") + std::to_string(swapChainImageCount) + " images.");

    m_swapChainImageCount = swapChainImageCount;

    Pipeline::cleanupSwapChainResources(this);

    if (!Pipeline::createGraphicsPipeline(this, renderPass)) { LogToFile("[Renderer_VK::onSwapChainRecreated] ERROR: Failed to recreate graphics pipeline"); throw std::runtime_error("Failed to recreate graphics pipeline"); }
    if (!Descriptor::createUniformBuffers(this)) { LogToFile("[Renderer_VK::onSwapChainRecreated] ERROR: Failed to recreate uniform buffers"); throw std::runtime_error("Failed to recreate uniform buffers"); }
    if (!Descriptor::createDescriptorPool(this)) { LogToFile("[Renderer_VK::onSwapChainRecreated] ERROR: Failed to recreate descriptor pool"); throw std::runtime_error("Failed to recreate descriptor pool"); }
    if (!Descriptor::createDescriptorSets(this)) { LogToFile("[Renderer_VK::onSwapChainRecreated] ERROR: Failed to recreate descriptor sets"); throw std::runtime_error("Failed to recreate descriptor sets"); }

    LogToFile("[Renderer_VK::onSwapChainRecreated] Swapchain-dependent resources recreated.");
}

void Renderer_VK::updateUniformBuffer(uint32_t uboBindingIndex, const ShaderParamsUBO& ubo) {
    if (uboBindingIndex >= m_uniformBuffersMapped.size() || !m_uniformBuffersMapped[uboBindingIndex]) {
        LogToFile(std::string("[Renderer_VK::updateUniformBuffer] ERROR: Invalid uboBindingIndex (") + std::to_string(uboBindingIndex)
            + ") or unmapped buffer. Mapped size: " + std::to_string(m_uniformBuffersMapped.size()));
        return;
    }
    memcpy(m_uniformBuffersMapped[uboBindingIndex], &ubo, sizeof(ubo));
}

void Renderer_VK::prepareAndUploadFrameData(
    VkCommandBuffer commandBuffer,
    uint32_t uboBindingIndex,
    VkBuffer prefilledStagingBuffer,
    int frameWidth, int frameHeight,
    const nlohmann::json& frameMetadata,
    double staticBlack, double staticWhite, int cfaTypeOverride,
    bool forceUpload,
    OrientationTag defaultOrientation,
    bool containerFlipped
) {
    if (frameWidth <= 0 || frameHeight <= 0) {
        LogToFile(std::string("[Renderer_VK::prepareAndUploadFrameData] Invalid dimensions ") + std::to_string(frameWidth) + "x" + std::to_string(frameHeight) + ". Skipping upload.");
        frameWidth = std::max(1, frameWidth);
        frameHeight = std::max(1, frameHeight);
        forceUpload = false;
    }

    bool dimensionsChanged = (frameWidth != m_currentRawW || frameHeight != m_currentRawH);
    if (dimensionsChanged) {
        LogToFile(std::string("[Renderer_VK::prepareAndUploadFrameData] Dimensions changed from ")
            + std::to_string(m_currentRawW) + "x" + std::to_string(m_currentRawH) + " to " + std::to_string(frameWidth) + "x" + std::to_string(frameHeight)
            + ". Recreating GPU image resources if necessary.");

        ensureRawImageCapacity(static_cast<uint32_t>(frameWidth), static_cast<uint32_t>(frameHeight));
        Descriptor::updateDescriptorSetsWithNewRawImage(this);
        forceUpload = true;
    }

    if (forceUpload) {
        if (prefilledStagingBuffer == VK_NULL_HANDLE) {
            LogToFile("[Renderer_VK::prepareAndUploadFrameData] ERROR: forceUpload is true, but prefilledStagingBuffer is VK_NULL_HANDLE. Cannot upload.");
        }
        else {
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = m_rawImage;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;

            barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

            vkCmdPipelineBarrier(commandBuffer,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &barrier);

            VkBufferImageCopy region{};
            region.bufferOffset = 0;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = { 0, 0, 0 };
            region.imageExtent = { static_cast<uint32_t>(frameWidth), static_cast<uint32_t>(frameHeight), 1 };
            vkCmdCopyBufferToImage(commandBuffer, prefilledStagingBuffer, m_rawImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier(commandBuffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &barrier);
        }
    }

    ShaderParamsUBO ubo{};
    ubo.W = frameWidth;
    ubo.H = frameHeight;
    ubo.cfaType = cfaTypeOverride;
    ubo.exposure = 1.0f;

    float blackLvl = static_cast<float>(staticBlack);
    if (frameMetadata.contains("dynamicBlackLevel")) {
        const auto& jb = frameMetadata.at("dynamicBlackLevel");
        if (jb.is_array() && !jb.empty()) {
            double avg_black = 0.0;
            std::size_t count_black = 0;
            for (const auto& val : jb) {
                if (val.is_number()) {
                    avg_black += val.get<double>();
                    count_black++;
                }
            }
            if (count_black > 0) blackLvl = static_cast<float>(avg_black / count_black);
        }
        else if (jb.is_number()) {
            blackLvl = jb.get<float>();
        }
    }
    ubo.blackLevel = blackLvl;

    float whiteLvl = static_cast<float>(staticWhite);
    if (frameMetadata.contains("dynamicWhiteLevel") && frameMetadata.at("dynamicWhiteLevel").is_number()) {
        whiteLvl = frameMetadata.at("dynamicWhiteLevel").get<float>();
    }
    ubo.whiteLevel = whiteLvl;
    float range = ubo.whiteLevel - ubo.blackLevel;
    ubo.invBlackWhiteRange = (range <= 1e-5f) ? 1.0f : (1.0f / range);

    auto asn_json = frameMetadata.value("asShotNeutral", nlohmann::json::array({ 1.0, 1.0, 1.0 }));
    std::vector<double> asn_values = { 1.0, 1.0, 1.0 };
    if (asn_json.is_array() && asn_json.size() >= 3) {
        bool valid_asn = true;
        std::vector<double> temp_asn;
        for (const auto& elem : asn_json) {
            if (elem.is_number()) temp_asn.push_back(elem.get<double>());
            else { valid_asn = false; break; }
        }
        if (valid_asn && temp_asn.size() >= 3) asn_values = temp_asn;
    }
    ubo.gainG = 1.0f;
    ubo.gainR = (asn_values.size() >= 2 && asn_values[0] > 1e-6 && asn_values[1] > 1e-6) ? static_cast<float>(asn_values[1] / asn_values[0]) : 1.0f;
    ubo.gainB = (asn_values.size() >= 3 && asn_values[2] > 1e-6 && asn_values[1] > 1e-6) ? static_cast<float>(asn_values[1] / asn_values[2]) : 1.0f;

    glm::mat3 ccm3x3_glm = glm::mat3(1.0f);
    nlohmann::json ccm_json_from_meta;
    if (frameMetadata.contains("ColorMatrix2") && frameMetadata.at("ColorMatrix2").is_array() && frameMetadata.at("ColorMatrix2").size() == 9) {
        ccm_json_from_meta = frameMetadata.at("ColorMatrix2");
    }
    else if (frameMetadata.contains("ColorMatrix") && frameMetadata.at("ColorMatrix").is_array() && frameMetadata.at("ColorMatrix").size() == 9) {
        ccm_json_from_meta = frameMetadata.at("ColorMatrix");
    }
    if (!ccm_json_from_meta.is_null() && ccm_json_from_meta.is_array() && ccm_json_from_meta.size() == 9) {
        bool valid_ccm_parse = true;
        for (int r_idx = 0; r_idx < 3; ++r_idx) {
            for (int c_idx = 0; c_idx < 3; ++c_idx) {
                if (ccm_json_from_meta.at(r_idx * 3 + c_idx).is_number()) {
                    ccm3x3_glm[c_idx][r_idx] = ccm_json_from_meta.at(r_idx * 3 + c_idx).get<float>();
                }
                else { valid_ccm_parse = false; break; }
            }
            if (!valid_ccm_parse) break;
        }
        if (!valid_ccm_parse) ccm3x3_glm = glm::mat3(1.0f);
    }
    ubo.CCM = glm::mat4(ccm3x3_glm);
    ubo.saturationAdjustment = 1.50f;
    nlohmann::json frameOrientVal = findOrientationValue(frameMetadata);
    OrientationTag tag = computeOrientationTag(
        frameOrientVal,
        frameMetadata.value("flipped", containerFlipped),
        defaultOrientation);
    ubo.orientationDegrees = orientationDegreesFromTag(tag);
    m_currentOrientationDegrees = ubo.orientationDegrees;

    updateUniformBuffer(uboBindingIndex, ubo);

    runPreviewCompute(commandBuffer, frameWidth, frameHeight,
                      blackLvl, whiteLvl, ccm3x3_glm,
                      ubo.gainR, ubo.gainG, ubo.gainB,
                      cfaTypeOverride);
}

void Renderer_VK::recordDrawCommands(
    VkCommandBuffer commandBuffer,
    uint32_t uboBindingIndex,
    int windowWidth,
    int windowHeight,
    int offsetX,
    int offsetY,
    float blackNorm,
    float whiteNorm
) {
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline);

    PreviewPushConstants pc{ blackNorm, whiteNorm };
    vkCmdPushConstants(commandBuffer, m_pipelineLayout,
                       VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(PreviewPushConstants), &pc);

    VkViewport viewport{};
    VkRect2D scissor{};

    if (m_currentRawW <= 0 || m_currentRawH <= 0) {
        viewport.x = 0.0f; viewport.y = 0.0f;
        viewport.width = (float)windowWidth; viewport.height = (float)windowHeight;
        viewport.minDepth = 0.0f; viewport.maxDepth = 1.0f;
        scissor.offset = { 0, 0 }; scissor.extent = { (uint32_t)windowWidth, (uint32_t)windowHeight };
    }
    else if (m_zoomNativePixels) {
        viewport.x = m_panX; viewport.y = m_panY;
        float w = (float)m_currentRawW;
        float h = (float)m_currentRawH;
        if (m_currentOrientationDegrees == 90 || m_currentOrientationDegrees == 270) {
            std::swap(w, h);
        }
        viewport.width = w; viewport.height = h;
        viewport.minDepth = 0.0f; viewport.maxDepth = 1.0f;
        scissor.offset = { 0, 0 }; scissor.extent = { (uint32_t)windowWidth, (uint32_t)windowHeight };
    }
    else {
        float imgAspect = (m_currentOrientationDegrees == 90 || m_currentOrientationDegrees == 270) ?
            (float)m_currentRawH / (float)m_currentRawW : (float)m_currentRawW / (float)m_currentRawH;
        float winAspect = (float)windowWidth / (float)windowHeight;
        float vpWidth, vpHeight, vpX, vpY;
        if (imgAspect > winAspect) {
            vpWidth = (float)windowWidth; vpHeight = vpWidth / imgAspect;
            vpX = 0.0f; vpY = ((float)windowHeight - vpHeight) / 2.0f;
        }
        else {
            vpHeight = (float)windowHeight; vpWidth = vpHeight * imgAspect;
            vpY = 0.0f; vpX = ((float)windowWidth - vpWidth) / 2.0f;
        }
        viewport.x = vpX; viewport.y = vpY;
        viewport.width = vpWidth; viewport.height = vpHeight;
        viewport.minDepth = 0.0f; viewport.maxDepth = 1.0f;
        scissor.offset = { (int32_t)std::max(0.0f, vpX), (int32_t)std::max(0.0f, vpY) };
        scissor.extent = { (uint32_t)std::max(0.0f, vpWidth), (uint32_t)std::max(0.0f, vpHeight) };
    }

    viewport.x += (float)offsetX;
    viewport.y += (float)offsetY;
    scissor.offset.x += offsetX;
    scissor.offset.y += offsetY;

    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    if (uboBindingIndex < m_descriptorSets.size() && m_descriptorSets[uboBindingIndex] != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &m_descriptorSets[uboBindingIndex], 0, nullptr);
        vkCmdDraw(commandBuffer, 6, 1, 0, 0);
    }
    else {
        LogToFile(std::string("[Renderer_VK::recordDrawCommands] ERROR: uboBindingIndex ") + std::to_string(uboBindingIndex)
            + " out of bounds for descriptorSets (size " + std::to_string(m_descriptorSets.size()) + ") or set is null. Skipping draw.");
    }
}

int Renderer_VK::getCfaType(const std::string& c) {
    std::string upper_cfa = c;
    std::transform(upper_cfa.begin(), upper_cfa.end(), upper_cfa.begin(),
        [](unsigned char char_c) { return static_cast<char>(std::toupper(char_c)); });
    if (upper_cfa == "BGGR") return 0;
    if (upper_cfa == "RGGB") return 1;
    if (upper_cfa == "GBRG") return 2;
    if (upper_cfa == "GRBG") return 3;
    LogToFile(std::string("[Renderer_VK::getCfaType] Unknown CFA pattern: ") + c + ". Defaulting to BGGR (0).");
    return 0;
}

void Renderer_VK::setZoomNativePixels(bool n) { m_zoomNativePixels = n; }
void Renderer_VK::setPanOffsets(float x, float y) { m_panX = x; m_panY = y; }
void Renderer_VK::resetPanOffsets() { m_panX = 0.0f; m_panY = 0.0f; }
float Renderer_VK::getPanX() const { return m_panX; }
float Renderer_VK::getPanY() const { return m_panY; }
int Renderer_VK::getImageWidth() const { return m_currentRawW; }
int Renderer_VK::getImageHeight() const { return m_currentRawH; }
void Renderer_VK::resetDimensions() {
    LogToFile("[Renderer_VK::resetDimensions] Resetting current raw dimensions to 0x0.");
    m_currentRawW = 0;
    m_currentRawH = 0;
    m_currentOrientationDegrees = 0;
}

void Renderer_VK::ensureRawImageCapacity(uint32_t w, uint32_t h)
{
    if (w <= static_cast<uint32_t>(m_currentRawW) && h <= static_cast<uint32_t>(m_currentRawH)) {
        return;
    }
    LogToFile(std::string("[Renderer_VK::ensureRawImageCapacity] Capacity insufficient (current: ") +
        std::to_string(m_currentRawW) + "x" + std::to_string(m_currentRawH) +
        ", required: " + std::to_string(w) + "x" + std::to_string(h) + "). Resizing GPU image.");

    if (m_device_p != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device_p);
    }
    if (!ImageResource::createRawImageResources(this, static_cast<int>(w), static_cast<int>(h))) {
        LogToFile("[Renderer_VK::ensureRawImageCapacity] ERROR: Failed to recreate raw image resources for new capacity.");
        throw std::runtime_error("Failed to ensure raw image capacity by recreating resources.");
    }
    ImageResource::createPreviewImage(this, static_cast<int>(w), static_cast<int>(h));
}

void Renderer_VK::runPreviewCompute(VkCommandBuffer cmd, int width, int height,
                                    float black, float white,
                                    const glm::mat3& ccm,
                                    float wbR, float wbG, float wbB,
                                    int cfa)
{
    VkDescriptorImageInfo inInfo{};
    inInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    inInfo.imageView = m_rawImageView;

    VkDescriptorImageInfo outInfo{};
    outInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    outInfo.imageView = m_previewView;

    VkWriteDescriptorSet wr[2]{};
    wr[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wr[0].dstSet = m_previewDescSet;
    wr[0].dstBinding = 0;
    wr[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    wr[0].descriptorCount = 1;
    wr[0].pImageInfo = &inInfo;
    wr[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wr[1].dstSet = m_previewDescSet;
    wr[1].dstBinding = 1;
    wr[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    wr[1].descriptorCount = 1;
    wr[1].pImageInfo = &outInfo;
    vkUpdateDescriptorSets(m_device_p, 2, wr, 0, nullptr);

    VkImageMemoryBarrier barriers[2]{};
    barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].image = m_rawImage;
    barriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barriers[0].subresourceRange.levelCount = 1;
    barriers[0].subresourceRange.layerCount = 1;
    barriers[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    barriers[1] = barriers[0];
    barriers[1].image = m_previewImage;
    barriers[1].oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 2, barriers);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_previewPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_previewPipelineLayout,
                            0, 1, &m_previewDescSet, 0, nullptr);

    struct PC {
        uint32_t w, h, cfa;
        float wbR, wbG, wbB;
        glm::mat3 ccm;
        uint32_t black, white;
    } pc{ (uint32_t)width,(uint32_t)height,(uint32_t)cfa, wbR, wbG, wbB, ccm, (uint32_t)black, (uint32_t)white };

    vkCmdPushConstants(cmd, m_previewPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PC), &pc);
    vkCmdDispatch(cmd, (uint32_t)((width + 15)/16), (uint32_t)((height + 15)/16), 1);

    barriers[1].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barriers[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barriers[1].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0,nullptr,0,nullptr,1,&barriers[1]);
}