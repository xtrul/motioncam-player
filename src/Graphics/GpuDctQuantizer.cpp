#include "Graphics/GpuDctQuantizer.h"
#include "Graphics/VulkanHelpers.h"
#include "Utils/DebugLog.h"
#include <filesystem>
#include <string>

extern std::string g_AppBasePath;

GpuDctQuantizer::GpuDctQuantizer(Renderer_VK* r) : m_renderer(r) {}
GpuDctQuantizer::~GpuDctQuantizer(){ cleanup(); }

bool GpuDctQuantizer::init(int width, int height){
    namespace fs = std::filesystem;
    m_width = width;
    m_height = height;
    fs::path shaderPath = fs::path(g_AppBasePath) / "shaders_spv" / "dct_quant.comp.spv";
    auto code = VulkanHelpers::readFile(shaderPath.string());
    VkShaderModule module = VulkanHelpers::createShaderModule(m_renderer->m_device_p, code);

    VkDescriptorSetLayoutBinding bindings[4]{};
    for(int i=0;i<3;++i){
        bindings[i].binding = i;
        bindings[i].descriptorCount = 1;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    bindings[3].binding = 3;
    bindings[3].descriptorCount = 1;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutCI{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutCI.bindingCount = 4;
    layoutCI.pBindings = bindings;
    VK_CHECK_RENDERER(vkCreateDescriptorSetLayout(m_renderer->m_device_p, &layoutCI, nullptr, &m_setLayout));

    // blocksX, plane index and planeOffset
    VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t)*3};
    VkPipelineLayoutCreateInfo pci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pci.setLayoutCount = 1;
    pci.pSetLayouts = &m_setLayout;
    pci.pushConstantRangeCount = 1;
    pci.pPushConstantRanges = &range;
    VK_CHECK_RENDERER(vkCreatePipelineLayout(m_renderer->m_device_p, &pci, nullptr, &m_layout));

    VkPipelineShaderStageCreateInfo stage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = module;
    stage.pName = "main";

    VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    ci.stage = stage;
    ci.layout = m_layout;
    VK_CHECK_RENDERER(vkCreateComputePipelines(m_renderer->m_device_p, VK_NULL_HANDLE, 1, &ci, nullptr, &m_pipeline));

    vkDestroyShaderModule(m_renderer->m_device_p, module, nullptr);

    VkDescriptorPoolSize poolSizes[4]{};
    for(int i=0;i<4;++i){
        poolSizes[i].type = bindings[i].descriptorType;
        poolSizes[i].descriptorCount = 1;
    }
    VkDescriptorPoolCreateInfo poolCI{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolCI.maxSets = 1;
    poolCI.poolSizeCount = 4;
    poolCI.pPoolSizes = poolSizes;
    VK_CHECK_RENDERER(vkCreateDescriptorPool(m_renderer->m_device_p, &poolCI, nullptr, &m_pool));

    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = m_pool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &m_setLayout;
    VK_CHECK_RENDERER(vkAllocateDescriptorSets(m_renderer->m_device_p, &ai, &m_set));

    VkDeviceSize outSize = static_cast<VkDeviceSize>(width) * height * sizeof(uint32_t) * 3;
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = outSize;
    bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VmaAllocationCreateInfo aiinfo{};
    aiinfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    VK_CHECK_RENDERER(vmaCreateBuffer(m_renderer->m_allocator_p, &bi, &aiinfo, &m_outBuffer, &m_outAlloc, nullptr));

    LogDnxhr(std::string("[GpuDctQuantizer] initialized ") + std::to_string(width) + "x" + std::to_string(height));

    return true;
}

void GpuDctQuantizer::cleanup(){
    if(m_outBuffer){
        vmaDestroyBuffer(m_renderer->m_allocator_p, m_outBuffer, m_outAlloc);
        m_outBuffer = VK_NULL_HANDLE;
    }
    if(m_pool){
        vkDestroyDescriptorPool(m_renderer->m_device_p, m_pool, nullptr);
        m_pool = VK_NULL_HANDLE;
    }
    if(m_pipeline){
        vkDestroyPipeline(m_renderer->m_device_p, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
    if(m_layout){
        vkDestroyPipelineLayout(m_renderer->m_device_p, m_layout, nullptr);
        m_layout = VK_NULL_HANDLE;
    }
    if(m_setLayout){
        vkDestroyDescriptorSetLayout(m_renderer->m_device_p, m_setLayout, nullptr);
        m_setLayout = VK_NULL_HANDLE;
    }
}

bool GpuDctQuantizer::process(VkImage luma, VkImage chromaU, VkImage chromaV, uint32_t* outBuf, size_t bufSize){
    if(!outBuf) return false;
    uint32_t blocksX = (m_width + 7) / 8;
    uint32_t blocksY = (m_height + 7) / 8;
    size_t blocks = static_cast<size_t>(blocksX) * blocksY;
    size_t required = blocks * 64 * sizeof(uint32_t) * 3; // Y + U + V

    LogDnxhr("[GpuDctQuantizer] dispatch " + std::to_string(blocksX) + "x" + std::to_string(blocksY));
    LogDnxhr("[GpuDctQuantizer] output buffer size=" + std::to_string(bufSize));

    if(bufSize < required){
        LogDnxhr("[GpuDctQuantizer] buffer too small, required=" + std::to_string(required));
        return false;
    }

    VkBuffer readbackBuf = VK_NULL_HANDLE;
    VmaAllocation readbackAlloc = VK_NULL_HANDLE;
    VmaAllocationInfo rbInfo{};
    VkBufferCreateInfo rbCi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    rbCi.size = required;
    rbCi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo rbAlloc{};
    rbAlloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    rbAlloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VK_CHECK_RENDERER(vmaCreateBuffer(m_renderer->m_allocator_p, &rbCi, &rbAlloc, &readbackBuf, &readbackAlloc, &rbInfo));

    VkImageView views[3]{};
    VkImage images[3] = { luma, chromaU, chromaV };
    for(int i=0;i<3;++i){
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = images[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = VK_FORMAT_R32_UINT;
        vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vi.subresourceRange.baseMipLevel = 0;
        vi.subresourceRange.levelCount = 1;
        vi.subresourceRange.baseArrayLayer = 0;
        vi.subresourceRange.layerCount = 1;
        VK_CHECK_RENDERER(vkCreateImageView(m_renderer->m_device_p, &vi, nullptr, &views[i]));
    }

    VkDescriptorImageInfo infos[3]{};
    for(int i=0;i<3;++i){
        infos[i].imageView = views[i];
        infos[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    }
    VkDescriptorBufferInfo bufInfo{m_outBuffer,0,required};

    VkWriteDescriptorSet writes[4]{};
    for(int i=0;i<3;++i){
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = m_set;
        writes[i].dstBinding = i;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[i].descriptorCount = 1;
        writes[i].pImageInfo = &infos[i];
    }
    writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[3].dstSet = m_set;
    writes[3].dstBinding = 3;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[3].descriptorCount = 1;
    writes[3].pBufferInfo = &bufInfo;
    vkUpdateDescriptorSets(m_renderer->m_device_p, 4, writes, 0, nullptr);

    VkCommandBuffer cmd = VulkanHelpers::beginSingleTimeCommands(m_renderer->m_device_p, m_renderer->m_hostSiteCommandPool_p);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_layout, 0, 1, &m_set, 0, nullptr);

    uint32_t planeOffset = 0;
    for(uint32_t plane=0; plane<3; ++plane){
        struct Push { uint32_t blocksX; uint32_t plane; uint32_t offset; } push{blocksX, plane, planeOffset};
        vkCmdPushConstants(cmd, m_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Push), &push);
        vkCmdDispatch(cmd, blocksX, blocksY, 1);
        planeOffset += blocksX*blocksY;
    }

    VkBufferMemoryBarrier b{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    b.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.buffer = m_outBuffer;
    b.offset = 0;
    b.size = required;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,nullptr,1,&b,0,nullptr);

    VkBufferCopy copy{0,0,required};
    vkCmdCopyBuffer(cmd, m_outBuffer, readbackBuf, 1, &copy);

    VulkanHelpers::endSingleTimeCommands(m_renderer->m_device_p, m_renderer->m_hostSiteCommandPool_p, m_renderer->m_graphicsQueue_p, cmd);

    vmaInvalidateAllocation(m_renderer->m_allocator_p, readbackAlloc, 0, required);
    memcpy(outBuf, rbInfo.pMappedData, required);

    vmaDestroyBuffer(m_renderer->m_allocator_p, readbackBuf, readbackAlloc);
    for(int i=0;i<3;++i) vkDestroyImageView(m_renderer->m_device_p, views[i], nullptr);

    return true;
}
