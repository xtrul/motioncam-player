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

    VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t)};
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

    VkDeviceSize outSize = width * height * sizeof(uint32_t);
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
    size_t required = static_cast<size_t>(blocksX) * blocksY * 64 * sizeof(uint32_t);

    LogDnxhr("[GpuDctQuantizer] dispatch " + std::to_string(blocksX) + "x" + std::to_string(blocksY));
    LogDnxhr("[GpuDctQuantizer] output buffer size=" + std::to_string(bufSize));

    if(bufSize < required){
        LogDnxhr("[GpuDctQuantizer] buffer too small, required=" + std::to_string(required));
        return false;
    }

    // TODO: Issue compute dispatch to actually perform DCT + quantization and
    // copy results back to CPU. This stub simply reports required buffer size.
    (void)luma; (void)chromaU; (void)chromaV;
    return false;
}
