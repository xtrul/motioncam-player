#define VMA_IMPLEMENTATION 
#include "vma_usage.h"     

#include "Renderer_VK.h" 
#include "DebugLog.h" 
#include "RawFrameBuffer.h" // Added for RawBytes and asU16

#include <nlohmann/json.hpp> // Full include for nlohmann::json definition
#include <stdexcept>
#include <array>
#include <iostream> 
#include <fstream>  
#include <algorithm> 
#include <glm/gtc/matrix_transform.hpp> 


#define VK_CHECK(x)                                                 \
    do {                                                            \
        VkResult err = x;                                           \
        if (err) {                                                  \
            std::string error_msg = std::string("[VULKAN CHECK FAILED IN RENDERER_VK] Error: ") + std::to_string(err) + " (" #x ") at " __FILE__ ":" + std::to_string(__LINE__); \
            LogToFile(error_msg);                                   \
            std::cerr << error_msg << std::endl;                    \
            abort();                                                \
        }                                                           \
    } while (0)

Renderer_VK::Renderer_VK(VkPhysicalDevice physicalDevice, VkDevice device, VmaAllocator allocator, VkQueue graphicsQueue, VkCommandPool commandPool)
    : m_physicalDevice(physicalDevice),
    m_device(device),
    m_allocator(allocator),
    m_graphicsQueue(graphicsQueue),
    m_hostSiteCommandPool(commandPool),
    m_rawImage(VK_NULL_HANDLE),
    m_rawImageAllocation(VK_NULL_HANDLE),
    m_rawImageView(VK_NULL_HANDLE),
    m_rawImageSampler(VK_NULL_HANDLE),
    m_descriptorSetLayout(VK_NULL_HANDLE),
    m_pipelineLayout(VK_NULL_HANDLE),
    m_graphicsPipeline(VK_NULL_HANDLE),
    m_descriptorPool(VK_NULL_HANDLE),
    m_currentRawW(0), m_currentRawH(0),
    m_zoomNativePixels(false),
    m_panX(0.0f), m_panY(0.0f),
    m_swapChainImageCount(0)
{
    LogToFile("[Renderer_VK] Constructor called.");
    std::cout << "[Renderer_VK] Constructor called." << std::endl;
}

Renderer_VK::~Renderer_VK() {
    LogToFile("[Renderer_VK] Destructor called.");
    std::cout << "[Renderer_VK] Destructor called." << std::endl;
}

bool Renderer_VK::init(VkRenderPass renderPass, uint32_t swapChainImageCount) {
    LogToFile(std::string("[Renderer_VK::init] Initializing with swapChainImageCount: ") + std::to_string(swapChainImageCount));
    std::cout << "[Renderer_VK::init] Initializing with swapChainImageCount: " << swapChainImageCount << std::endl;
    m_swapChainImageCount = swapChainImageCount;

    if (!createDescriptorSetLayout()) { LogToFile("[Renderer_VK::init] ERROR: Failed to create descriptor set layout."); std::cerr << "[Renderer_VK::init] Failed to create descriptor set layout." << std::endl; return false; }
    LogToFile("[Renderer_VK::init] Descriptor set layout created."); std::cout << "[Renderer_VK::init] Descriptor set layout created." << std::endl;
    if (!createGraphicsPipeline(renderPass)) { LogToFile("[Renderer_VK::init] ERROR: Failed to create graphics pipeline."); std::cerr << "[Renderer_VK::init] Failed to create graphics pipeline." << std::endl; return false; }
    LogToFile("[Renderer_VK::init] Graphics pipeline created."); std::cout << "[Renderer_VK::init] Graphics pipeline created." << std::endl;
    if (!createUniformBuffers()) { LogToFile("[Renderer_VK::init] ERROR: Failed to create uniform buffers."); std::cerr << "[Renderer_VK::init] Failed to create uniform buffers." << std::endl; return false; }
    LogToFile("[Renderer_VK::init] Uniform buffers created."); std::cout << "[Renderer_VK::init] Uniform buffers created." << std::endl;
    if (!createDescriptorPool()) { LogToFile("[Renderer_VK::init] ERROR: Failed to create descriptor pool."); std::cerr << "[Renderer_VK::init] Failed to create descriptor pool." << std::endl; return false; }
    LogToFile("[Renderer_VK::init] Descriptor pool created."); std::cout << "[Renderer_VK::init] Descriptor pool created." << std::endl;
    if (!createRawImageResources(1, 1)) { LogToFile("[Renderer_VK::init] ERROR: Failed to create initial raw image resources."); std::cerr << "[Renderer_VK::init] Failed to create initial raw image resources." << std::endl; return false; }
    LogToFile("[Renderer_VK::init] Initial raw image resources created."); std::cout << "[Renderer_VK::init] Initial raw image resources created." << std::endl;
    if (!createDescriptorSets()) { LogToFile("[Renderer_VK::init] ERROR: Failed to create descriptor sets."); std::cerr << "[Renderer_VK::init] Failed to create descriptor sets." << std::endl; return false; }
    LogToFile("[Renderer_VK::init] Descriptor sets created."); std::cout << "[Renderer_VK::init] Descriptor sets created." << std::endl;

    LogToFile("[Renderer_VK::init] Initialization successful.");
    std::cout << "[Renderer_VK::init] Initialization successful." << std::endl;
    return true;
}

void Renderer_VK::cleanup() {
    LogToFile("[Renderer_VK::cleanup] Starting cleanup...");
    std::cout << "[Renderer_VK::cleanup] Starting cleanup..." << std::endl;
    cleanupSwapChainResources();
    cleanupRawImageResources();

    if (m_descriptorSetLayout != VK_NULL_HANDLE) {
        LogToFile("[Renderer_VK::cleanup] Destroying descriptor set layout.");
        std::cout << "[Renderer_VK::cleanup] Destroying descriptor set layout." << std::endl;
        vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
        m_descriptorSetLayout = VK_NULL_HANDLE;
    }
    LogToFile("[Renderer_VK::cleanup] Cleanup complete.");
    std::cout << "[Renderer_VK::cleanup] Cleanup complete." << std::endl;
}

void Renderer_VK::cleanupSwapChainResources() {
    LogToFile("[Renderer_VK::cleanupSwapChainResources] Cleaning swapchain-dependent resources...");
    std::cout << "[Renderer_VK::cleanupSwapChainResources] Cleaning swapchain-dependent resources..." << std::endl;
    if (m_graphicsPipeline != VK_NULL_HANDLE) {
        LogToFile("[Renderer_VK::cleanupSwapChainResources] Destroying graphics pipeline.");
        std::cout << "[Renderer_VK::cleanupSwapChainResources] Destroying graphics pipeline." << std::endl;
        vkDestroyPipeline(m_device, m_graphicsPipeline, nullptr);
        m_graphicsPipeline = VK_NULL_HANDLE;
    }
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        LogToFile("[Renderer_VK::cleanupSwapChainResources] Destroying pipeline layout.");
        std::cout << "[Renderer_VK::cleanupSwapChainResources] Destroying pipeline layout." << std::endl;
        vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }
    if (m_descriptorPool != VK_NULL_HANDLE) {
        LogToFile("[Renderer_VK::cleanupSwapChainResources] Destroying descriptor pool.");
        std::cout << "[Renderer_VK::cleanupSwapChainResources] Destroying descriptor pool." << std::endl;
        vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
        m_descriptorSets.clear();
    }
    cleanupUniformBuffers();
    LogToFile("[Renderer_VK::cleanupSwapChainResources] Swapchain-dependent resources cleaned.");
    std::cout << "[Renderer_VK::cleanupSwapChainResources] Swapchain-dependent resources cleaned." << std::endl;
}


void Renderer_VK::onSwapChainRecreated(VkRenderPass renderPass, uint32_t swapChainImageCount) {
    LogToFile(std::string("[Renderer_VK::onSwapChainRecreated] Recreating for ") + std::to_string(swapChainImageCount) + " images.");
    std::cout << "[Renderer_VK::onSwapChainRecreated] Recreating for " << swapChainImageCount << " images." << std::endl;
    cleanupSwapChainResources();

    m_swapChainImageCount = swapChainImageCount;

    if (!createGraphicsPipeline(renderPass)) { LogToFile("[Renderer_VK::onSwapChainRecreated] ERROR: Failed to recreate graphics pipeline"); std::cerr << "Failed to recreate graphics pipeline" << std::endl; throw std::runtime_error("Failed to recreate graphics pipeline"); }
    if (!createUniformBuffers()) { LogToFile("[Renderer_VK::onSwapChainRecreated] ERROR: Failed to recreate uniform buffers"); std::cerr << "Failed to recreate uniform buffers" << std::endl; throw std::runtime_error("Failed to recreate uniform buffers"); }
    if (!createDescriptorPool()) { LogToFile("[Renderer_VK::onSwapChainRecreated] ERROR: Failed to recreate descriptor pool"); std::cerr << "Failed to recreate descriptor pool" << std::endl; throw std::runtime_error("Failed to recreate descriptor pool"); }
    if (!createDescriptorSets()) { LogToFile("[Renderer_VK::onSwapChainRecreated] ERROR: Failed to recreate descriptor sets"); std::cerr << "Failed to recreate descriptor sets" << std::endl; throw std::runtime_error("Failed to recreate descriptor sets"); }
    LogToFile("[Renderer_VK::onSwapChainRecreated] Swapchain-dependent resources recreated.");
    std::cout << "[Renderer_VK::onSwapChainRecreated] Swapchain-dependent resources recreated." << std::endl;
}


bool Renderer_VK::createRawImageResources(int width, int height) {
    LogToFile(std::string("[Renderer_VK::createRawImageResources] Creating raw image ") + std::to_string(width) + "x" + std::to_string(height));
    std::cout << "[Renderer_VK::createRawImageResources] Creating raw image " << width << "x" << height << std::endl;
    cleanupRawImageResources();

    m_currentRawW = width;
    m_currentRawH = height;

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

    VK_CHECK(vmaCreateImage(m_allocator, &imageInfo, &allocInfo, &m_rawImage, &m_rawImageAllocation, nullptr));

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_rawImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R16_UINT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    VK_CHECK(vkCreateImageView(m_device, &viewInfo, nullptr, &m_rawImageView));

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
    VK_CHECK(vkCreateSampler(m_device, &samplerInfo, nullptr, &m_rawImageSampler));

    VkCommandBuffer cmdBuffer = beginSingleTimeCommands();
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_rawImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmdBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier);
    endSingleTimeCommands(cmdBuffer);
    LogToFile("[Renderer_VK::createRawImageResources] Raw image resources created and transitioned.");
    std::cout << "[Renderer_VK::createRawImageResources] Raw image resources created and transitioned." << std::endl;
    return true;
}

void Renderer_VK::cleanupRawImageResources() {
    if (m_rawImageSampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_device, m_rawImageSampler, nullptr);
        m_rawImageSampler = VK_NULL_HANDLE;
    }
    if (m_rawImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(m_device, m_rawImageView, nullptr);
        m_rawImageView = VK_NULL_HANDLE;
    }
    if (m_rawImage != VK_NULL_HANDLE) {
        vmaDestroyImage(m_allocator, m_rawImage, m_rawImageAllocation);
        m_rawImage = VK_NULL_HANDLE;
        m_rawImageAllocation = VK_NULL_HANDLE;
    }
}


bool Renderer_VK::createUniformBuffers() {
    VkDeviceSize bufferSize = sizeof(ShaderParamsUBO);
    m_uniformBuffers.resize(m_swapChainImageCount);
    m_uniformBufferAllocations.resize(m_swapChainImageCount);
    m_uniformBuffersMapped.resize(m_swapChainImageCount);
    LogToFile(std::string("[Renderer_VK::createUniformBuffers] Creating ") + std::to_string(m_swapChainImageCount) + " uniform buffers.");
    std::cout << "[Renderer_VK::createUniformBuffers] Creating " << m_swapChainImageCount << " uniform buffers." << std::endl;

    for (size_t i = 0; i < m_swapChainImageCount; i++) {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = bufferSize;
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VK_CHECK(vmaCreateBuffer(m_allocator, &bufferInfo, &allocInfo, &m_uniformBuffers[i], &m_uniformBufferAllocations[i], nullptr));

        VmaAllocationInfo allocationInfoResult;
        vmaGetAllocationInfo(m_allocator, m_uniformBufferAllocations[i], &allocationInfoResult);
        m_uniformBuffersMapped[i] = allocationInfoResult.pMappedData;
        if (!m_uniformBuffersMapped[i]) {
            LogToFile(std::string("[Renderer_VK::createUniformBuffers] ERROR: Failed to map uniform buffer ") + std::to_string(i));
            std::cerr << "[Renderer_VK::createUniformBuffers] Failed to map uniform buffer " << i << std::endl;
            return false;
        }
    }
    LogToFile("[Renderer_VK::createUniformBuffers] Uniform buffers created and mapped.");
    std::cout << "[Renderer_VK::createUniformBuffers] Uniform buffers created and mapped." << std::endl;
    return true;
}

void Renderer_VK::cleanupUniformBuffers() {
    LogToFile(std::string("[Renderer_VK::cleanupUniformBuffers] Cleaning up ") + std::to_string(m_uniformBuffers.size()) + " uniform buffers.");
    std::cout << "[Renderer_VK::cleanupUniformBuffers] Cleaning up " << m_uniformBuffers.size() << " uniform buffers." << std::endl;
    for (size_t i = 0; i < m_uniformBuffers.size(); i++) {
        if (m_uniformBuffers[i] != VK_NULL_HANDLE) {
            vmaDestroyBuffer(m_allocator, m_uniformBuffers[i], m_uniformBufferAllocations[i]);
        }
    }
    m_uniformBuffers.assign(m_swapChainImageCount > 0 ? m_swapChainImageCount : 0, VK_NULL_HANDLE);
    m_uniformBufferAllocations.assign(m_swapChainImageCount > 0 ? m_swapChainImageCount : 0, VK_NULL_HANDLE);
    m_uniformBuffersMapped.assign(m_swapChainImageCount > 0 ? m_swapChainImageCount : 0, nullptr);
}


bool Renderer_VK::createDescriptorSetLayout() {
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

    VK_CHECK(vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &m_descriptorSetLayout));
    return true;
}

bool Renderer_VK::createGraphicsPipeline(VkRenderPass renderPass) {
    LogToFile("[Renderer_VK::createGraphicsPipeline] Creating graphics pipeline...");
    std::cout << "[Renderer_VK::createGraphicsPipeline] Creating graphics pipeline..." << std::endl;

    LogToFile("[Renderer_VK::createGraphicsPipeline] SHADER PATH CHECK - Version 2024-05-16-C");
    std::cout << "[Renderer_VK::createGraphicsPipeline] SHADER PATH CHECK - Version 2024-05-16-C" << std::endl;

    std::string vertShaderPath = "shaders_spv/fullscreen_quad.vert.spv";
    std::string fragShaderPath = "shaders_spv/image_process.frag.spv";

    LogToFile(std::string("[Renderer_VK::createGraphicsPipeline] FINAL vertShaderPath being used: ") + vertShaderPath);
    LogToFile(std::string("[Renderer_VK::createGraphicsPipeline] FINAL fragShaderPath being used: ") + fragShaderPath);
    std::cout << "[Renderer_VK::createGraphicsPipeline] FINAL vertShaderPath being used: " << vertShaderPath << std::endl;
    std::cout << "[Renderer_VK::createGraphicsPipeline] FINAL fragShaderPath being used: " << fragShaderPath << std::endl;

    auto vertShaderCode = readFile(vertShaderPath);
    auto fragShaderCode = readFile(fragShaderPath);

    VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
    VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);
    LogToFile("[Renderer_VK::createGraphicsPipeline] Shader modules created.");
    std::cout << "[Renderer_VK::createGraphicsPipeline] Shader modules created." << std::endl;

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    vertexInputInfo.vertexAttributeDescriptionCount = 0;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_descriptorSetLayout;
    VK_CHECK(vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout));
    LogToFile("[Renderer_VK::createGraphicsPipeline] Pipeline layout created.");
    std::cout << "[Renderer_VK::createGraphicsPipeline] Pipeline layout created." << std::endl;


    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = nullptr;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    VK_CHECK(vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_graphicsPipeline));
    LogToFile("[Renderer_VK::createGraphicsPipeline] Graphics pipeline created.");
    std::cout << "[Renderer_VK::createGraphicsPipeline] Graphics pipeline created." << std::endl;


    vkDestroyShaderModule(m_device, fragShaderModule, nullptr);
    vkDestroyShaderModule(m_device, vertShaderModule, nullptr);
    LogToFile("[Renderer_VK::createGraphicsPipeline] Shader modules destroyed.");
    std::cout << "[Renderer_VK::createGraphicsPipeline] Shader modules destroyed." << std::endl;

    return true;
}

bool Renderer_VK::createDescriptorPool() {
    LogToFile(std::string("[Renderer_VK::createDescriptorPool] Creating descriptor pool for ") + std::to_string(m_swapChainImageCount) + " sets.");
    std::cout << "[Renderer_VK::createDescriptorPool] Creating descriptor pool for " << m_swapChainImageCount << " sets." << std::endl;
    if (m_swapChainImageCount == 0) {
        LogToFile("[Renderer_VK::createDescriptorPool] ERROR: m_swapChainImageCount is 0, cannot create pool.");
        std::cerr << "[Renderer_VK::createDescriptorPool] Error: m_swapChainImageCount is 0, cannot create pool." << std::endl;
        return false;
    }
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = static_cast<uint32_t>(m_swapChainImageCount);
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[1].descriptorCount = static_cast<uint32_t>(m_swapChainImageCount);

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<uint32_t>(m_swapChainImageCount);
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;

    VK_CHECK(vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool));
    LogToFile("[Renderer_VK::createDescriptorPool] Descriptor pool created.");
    std::cout << "[Renderer_VK::createDescriptorPool] Descriptor pool created." << std::endl;
    return true;
}

bool Renderer_VK::createDescriptorSets() {
    if (m_swapChainImageCount == 0) {
        LogToFile("[Renderer_VK::createDescriptorSets] ERROR: m_swapChainImageCount is 0.");
        std::cerr << "[Renderer_VK::createDescriptorSets] Error: m_swapChainImageCount is 0, cannot create descriptor sets." << std::endl;
        return false;
    }
    if (m_rawImageView == VK_NULL_HANDLE || m_rawImageSampler == VK_NULL_HANDLE) {
        LogToFile("[Renderer_VK::createDescriptorSets] ERROR: Raw image view or sampler is null.");
        std::cerr << "[Renderer_VK::createDescriptorSets] Error: Raw image view or sampler is null." << std::endl;
        return false;
    }
    if (m_uniformBuffers.empty() || m_uniformBuffers.size() != m_swapChainImageCount || m_uniformBuffers[0] == VK_NULL_HANDLE) {
        LogToFile("[Renderer_VK::createDescriptorSets] ERROR: Uniform buffers not created, wrong size, or empty.");
        std::cerr << "[Renderer_VK::createDescriptorSets] Error: Uniform buffers not created, wrong size, or empty." << std::endl;
        return false;
    }

    LogToFile(std::string("[Renderer_VK::createDescriptorSets] Creating ") + std::to_string(m_swapChainImageCount) + " descriptor sets.");
    std::cout << "[Renderer_VK::createDescriptorSets] Creating " << m_swapChainImageCount << " descriptor sets." << std::endl;
    std::vector<VkDescriptorSetLayout> layouts(m_swapChainImageCount, m_descriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(m_swapChainImageCount);
    allocInfo.pSetLayouts = layouts.data();

    m_descriptorSets.resize(m_swapChainImageCount);
    VK_CHECK(vkAllocateDescriptorSets(m_device, &allocInfo, m_descriptorSets.data()));

    for (size_t i = 0; i < m_swapChainImageCount; i++) {
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = m_rawImageView;
        imageInfo.sampler = m_rawImageSampler;

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_uniformBuffers[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(ShaderParamsUBO);

        std::array<VkWriteDescriptorSet, 2> descriptorWrites{};
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = m_descriptorSets[i];
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pImageInfo = &imageInfo;

        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = m_descriptorSets[i];
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
    LogToFile("[Renderer_VK::createDescriptorSets] Descriptor sets created and updated.");
    std::cout << "[Renderer_VK::createDescriptorSets] Descriptor sets created and updated." << std::endl;
    return true;
}


void Renderer_VK::updateUniformBuffer(uint32_t currentImageIndex, const ShaderParamsUBO& ubo) {
    if (currentImageIndex >= m_uniformBuffersMapped.size() || !m_uniformBuffersMapped[currentImageIndex]) {
        LogToFile(std::string("[Renderer_VK::updateUniformBuffer] ERROR: Invalid currentImageIndex (") + std::to_string(currentImageIndex)
            + ") or unmapped buffer. Mapped size: " + std::to_string(m_uniformBuffersMapped.size()));
        std::cerr << "[Renderer_VK::updateUniformBuffer] Error: Invalid currentImageIndex (" << currentImageIndex
            << ") or unmapped buffer. Mapped size: " << m_uniformBuffersMapped.size() << std::endl;
        return;
    }
    memcpy(m_uniformBuffersMapped[currentImageIndex], &ubo, sizeof(ubo));
}

void Renderer_VK::uploadRawTextureData(VkCommandBuffer commandBuffer, const RawBytes& rawData, int width, int height) {
    if (width <= 0 || height <= 0 || rawData.empty()) {
        LogToFile("[Renderer_VK::uploadRawTextureData] ERROR: Invalid data or dimensions for upload.");
        std::cerr << "[Renderer_VK::uploadRawTextureData] Invalid data or dimensions for upload." << std::endl;
        return;
    }

    // rawData.size() is already in bytes
    VkDeviceSize bufferSize = rawData.size();
    if (bufferSize != static_cast<VkDeviceSize>(width) * height * sizeof(uint16_t)) {
        LogToFile("[Renderer_VK::uploadRawTextureData] ERROR: rawData size mismatch with width/height/sizeof(uint16_t).");
        std::cerr << "[Renderer_VK::uploadRawTextureData] ERROR: rawData size mismatch with width/height/sizeof(uint16_t)." << std::endl;
        return;
    }


    VkBuffer stagingBuffer;
    VmaAllocation stagingBufferAllocation;
    VmaAllocationInfo stagingBufferAllocInfo;


    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VmaAllocationCreateInfo allocCreateInfo{};
    allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    allocCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VK_CHECK(vmaCreateBuffer(m_allocator, &bufferInfo, &allocCreateInfo, &stagingBuffer, &stagingBufferAllocation, &stagingBufferAllocInfo));

    memcpy(stagingBufferAllocInfo.pMappedData, rawData.data(), bufferSize);

    VkImageMemoryBarrier barrierToTransfer{};
    barrierToTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrierToTransfer.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrierToTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrierToTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrierToTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrierToTransfer.image = m_rawImage;
    barrierToTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrierToTransfer.subresourceRange.baseMipLevel = 0;
    barrierToTransfer.subresourceRange.levelCount = 1;
    barrierToTransfer.subresourceRange.baseArrayLayer = 0;
    barrierToTransfer.subresourceRange.layerCount = 1;
    barrierToTransfer.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrierToTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrierToTransfer);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = { 0, 0, 0 };
    region.imageExtent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };
    vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, m_rawImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    VkImageMemoryBarrier barrierToShaderRead{};
    barrierToShaderRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrierToShaderRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrierToShaderRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrierToShaderRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrierToShaderRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrierToShaderRead.image = m_rawImage;
    barrierToShaderRead.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrierToShaderRead.subresourceRange.baseMipLevel = 0;
    barrierToShaderRead.subresourceRange.levelCount = 1;
    barrierToShaderRead.subresourceRange.baseArrayLayer = 0;
    barrierToShaderRead.subresourceRange.layerCount = 1;
    barrierToShaderRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrierToShaderRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrierToShaderRead);

    // Staging buffer needs to be destroyed by App after command buffer completion.
    // For now, we just destroy it. This is NOT robust for in-flight command buffers.
    // A proper solution would queue this for deletion.
    // TODO: Implement deferred deletion of stagingBuffer and stagingBufferAllocation in App.
    vmaDestroyBuffer(m_allocator, stagingBuffer, stagingBufferAllocation);
}


void Renderer_VK::recordRenderCommands(VkCommandBuffer commandBuffer, uint32_t currentFrameIndex,
    const RawBytes& rawData, // Changed from std::vector<uint16_t>
    const nlohmann::json& frameMetadata,
    double staticBlack, double staticWhite, int cfaTypeOverride,
    int windowWidth, int windowHeight) {
    int w = frameMetadata.value("width", 0);
    int h = frameMetadata.value("height", 0);

    if (w <= 0 || h <= 0 || rawData.empty()) {
        return;
    }

    if (w != m_currentRawW || h != m_currentRawH) {
        LogToFile(std::string("[Renderer_VK::recordRenderCommands] Raw image dimensions changed from ")
            + std::to_string(m_currentRawW) + "x" + std::to_string(m_currentRawH) + " to " + std::to_string(w) + "x" + std::to_string(h)
            + ". Recreating resources.");
        std::cout << "[Renderer_VK::recordRenderCommands] Raw image dimensions changed from "
            << m_currentRawW << "x" << m_currentRawH << " to " << w << "x" << h
            << ". Recreating resources." << std::endl;
        vkDeviceWaitIdle(m_device);
        if (!createRawImageResources(w, h)) {
            LogToFile("[Renderer_VK::recordRenderCommands] ERROR: Failed to recreate raw image resources for new dimensions.");
            std::cerr << "[Renderer_VK::recordRenderCommands] Failed to recreate raw image resources for new dimensions." << std::endl;
            throw std::runtime_error("Failed to recreate raw image resources");
        }
        LogToFile("[Renderer_VK::recordRenderCommands] Updating descriptor sets for new raw image view.");
        std::cout << "[Renderer_VK::recordRenderCommands] Updating descriptor sets for new raw image view." << std::endl;
        if (m_descriptorPool != VK_NULL_HANDLE && !m_descriptorSets.empty()) {
            for (size_t i = 0; i < m_swapChainImageCount; i++) {
                VkDescriptorImageInfo imageInfo{};
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageInfo.imageView = m_rawImageView;
                imageInfo.sampler = m_rawImageSampler;

                VkWriteDescriptorSet descriptorWrite{};
                descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                descriptorWrite.dstSet = m_descriptorSets[i];
                descriptorWrite.dstBinding = 0;
                descriptorWrite.dstArrayElement = 0;
                descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                descriptorWrite.descriptorCount = 1;
                descriptorWrite.pImageInfo = &imageInfo;
                vkUpdateDescriptorSets(m_device, 1, &descriptorWrite, 0, nullptr);
            }
        }
        else {
            LogToFile("[Renderer_VK::recordRenderCommands] WARNING: Descriptor pool or sets not ready for update after image resize.");
            std::cerr << "[Renderer_VK::recordRenderCommands] Warning: Descriptor pool or sets not ready for update after image resize." << std::endl;
        }
    }

    uploadRawTextureData(commandBuffer, rawData, w, h);

    ShaderParamsUBO ubo{};
    ubo.W = w;
    ubo.H = h;
    ubo.cfaType = cfaTypeOverride;
    ubo.exposure = 1.0f;

    float blackLvl = static_cast<float>(staticBlack);
    if (frameMetadata.contains("dynamicBlackLevel")) {
        const auto& jb = frameMetadata.at("dynamicBlackLevel");
        if (jb.is_array() && !jb.empty()) {
            double avg = 0; std::size_t count = 0;
            for (const auto& val : jb) { if (val.is_number()) { avg += val.get<double>(); count++; } }
            if (count > 0) blackLvl = static_cast<float>(avg / count);
        }
        else if (jb.is_number()) { blackLvl = jb.get<float>(); }
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
    std::vector<double> asn = { 1.0, 1.0, 1.0 };
    if (asn_json.is_array() && asn_json.size() >= 3) {
        bool v = true; std::vector<double> t;
        for (const auto& e : asn_json) { if (e.is_number()) t.push_back(e.get<double>()); else { v = false; break; } }
        if (v && t.size() >= 3) asn = t;
    }
    ubo.gainG = 1.0f;
    ubo.gainR = (asn.size() >= 1 && asn[0] > 1e-6 && asn.size() >= 2 && asn[1] > 1e-6) ? static_cast<float>(asn[1] / asn[0]) : 1.0f;
    ubo.gainB = (asn.size() >= 3 && asn[2] > 1e-6 && asn[1] > 1e-6) ? static_cast<float>(asn[1] / asn[2]) : 1.0f;

    glm::mat3 ccm3x3_glm = glm::mat3(1.0f);
    nlohmann::json ccm_json_meta;
    if (frameMetadata.contains("ColorMatrix2") && frameMetadata.at("ColorMatrix2").is_array() && frameMetadata.at("ColorMatrix2").size() == 9) {
        ccm_json_meta = frameMetadata.at("ColorMatrix2");
    }
    else if (frameMetadata.contains("ColorMatrix") && frameMetadata.at("ColorMatrix").is_array() && frameMetadata.at("ColorMatrix").size() == 9) {
        ccm_json_meta = frameMetadata.at("ColorMatrix");
    }

    if (!ccm_json_meta.is_null() && ccm_json_meta.is_array() && ccm_json_meta.size() == 9) {
        bool valid_ccm = true;
        for (int r_idx = 0; r_idx < 3; ++r_idx) {
            for (int c_idx = 0; c_idx < 3; ++c_idx) {
                if (ccm_json_meta.at(r_idx * 3 + c_idx).is_number()) {
                    ccm3x3_glm[c_idx][r_idx] = ccm_json_meta.at(r_idx * 3 + c_idx).get<float>();
                }
                else { valid_ccm = false; break; }
            }
            if (!valid_ccm) break;
        }
        if (!valid_ccm) ccm3x3_glm = glm::mat3(1.0f);
    }
    ubo.CCM = glm::mat4(ccm3x3_glm);

    // --- SET SATURATION ADJUSTMENT ---
    ubo.saturationAdjustment = 1.50f; // For +50% saturation
    // You can make this a member variable of Renderer_VK or App
    // to control it dynamically via UI later.
    // --- END SET SATURATION ADJUSTMENT ---

    updateUniformBuffer(currentFrameIndex, ubo);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphicsPipeline);

    VkViewport viewport{};
    VkRect2D scissor{};

    if (m_zoomNativePixels) {
        viewport.x = m_panX;
        viewport.y = m_panY;
        viewport.width = (float)m_currentRawW;
        viewport.height = (float)m_currentRawH;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        scissor.offset = { 0, 0 };
        scissor.extent = { (uint32_t)windowWidth, (uint32_t)windowHeight };
    }
    else {
        float imgAspect = (m_currentRawH == 0) ? 1.0f : ((float)m_currentRawW / (float)m_currentRawH);
        float winAspect = (windowHeight == 0) ? 1.0f : ((float)windowWidth / (float)windowHeight);
        float vpWidth = (float)windowWidth;
        float vpHeight = (float)windowHeight;
        float vpX = 0.0f;
        float vpY = 0.0f;
        if (imgAspect > winAspect) {
            vpHeight = windowWidth / imgAspect;
            vpY = (windowHeight - vpHeight) / 2.0f;
        }
        else {
            vpWidth = windowHeight * imgAspect;
            vpX = (windowWidth - vpWidth) / 2.0f;
        }
        viewport.x = vpX; viewport.y = vpY;
        viewport.width = vpWidth; viewport.height = vpHeight;
        viewport.minDepth = 0.0f; viewport.maxDepth = 1.0f;
        scissor.offset = { (int32_t)vpX, (int32_t)vpY };
        scissor.extent = { (uint32_t)vpWidth, (uint32_t)vpHeight };
    }

    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    if (currentFrameIndex < m_descriptorSets.size()) {
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &m_descriptorSets[currentFrameIndex], 0, nullptr);
        vkCmdDraw(commandBuffer, 6, 1, 0, 0);
    }
    else {
        LogToFile(std::string("[Renderer_VK::recordRenderCommands] ERROR: currentFrameIndex ") + std::to_string(currentFrameIndex)
            + " out of bounds for descriptorSets (size " + std::to_string(m_descriptorSets.size()) + ")");
        std::cerr << "[Renderer_VK::recordRenderCommands] Error: currentFrameIndex " << currentFrameIndex
            << " out of bounds for descriptorSets (size " << m_descriptorSets.size() << ")" << std::endl;
    }
}


std::vector<char> Renderer_VK::readFile(const std::string& filename) {
    std::string fullPath = filename;
    LogToFile(std::string("[Renderer_VK::readFile] Attempting to read shader file: ") + fullPath);
    std::cout << "[Renderer_VK::readFile] Attempting to read shader file: " << fullPath << std::endl;
    std::ifstream file(fullPath, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        LogToFile(std::string("[Renderer_VK::readFile] ERROR: FAILED to open shader file: ") + fullPath);
        std::cerr << "[Renderer_VK::readFile] FAILED to open shader file: " << fullPath << std::endl;
        throw std::runtime_error("failed to open file: " + fullPath);
    }
    size_t fileSize = (size_t)file.tellg();
    LogToFile(std::string("[Renderer_VK::readFile] Shader file ") + fullPath + " size: " + std::to_string(fileSize) + " bytes.");
    std::cout << "[Renderer_VK::readFile] Shader file " << fullPath << " size: " << fileSize << " bytes." << std::endl;
    if (fileSize == 0) {
        LogToFile(std::string("[Renderer_VK::readFile] ERROR: Shader file is EMPTY: ") + fullPath);
        std::cerr << "[Renderer_VK::readFile] Shader file is EMPTY: " << fullPath << std::endl;
        throw std::runtime_error("Shader file is empty: " + fullPath);
    }
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    LogToFile(std::string("[Renderer_VK::readFile] Successfully read shader file: ") + fullPath);
    std::cout << "[Renderer_VK::readFile] Successfully read shader file: " << fullPath << std::endl;
    return buffer;
}

VkShaderModule Renderer_VK::createShaderModule(const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule shaderModule;
    VK_CHECK(vkCreateShaderModule(m_device, &createInfo, nullptr, &shaderModule));
    return shaderModule;
}

VkCommandBuffer Renderer_VK::beginSingleTimeCommands() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_hostSiteCommandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    VK_CHECK(vkAllocateCommandBuffers(m_device, &allocInfo, &commandBuffer));

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));
    return commandBuffer;
}

void Renderer_VK::endSingleTimeCommands(VkCommandBuffer commandBuffer) {
    VK_CHECK(vkEndCommandBuffer(commandBuffer));

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    VK_CHECK(vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(m_graphicsQueue));

    vkFreeCommandBuffers(m_device, m_hostSiteCommandPool, 1, &commandBuffer);
}


int Renderer_VK::getCfaType(const std::string& c) {
    std::string upper_cfa = c;
    std::transform(upper_cfa.begin(), upper_cfa.end(), upper_cfa.begin(),
        [](unsigned char char_c) { return static_cast<char>(std::toupper(char_c)); });
    if (upper_cfa == "BGGR") return 0;
    if (upper_cfa == "RGGB") return 1;
    if (upper_cfa == "GBRG") return 2;
    if (upper_cfa == "GRBG") return 3;
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
    m_currentRawW = 0;
    m_currentRawH = 0;
}