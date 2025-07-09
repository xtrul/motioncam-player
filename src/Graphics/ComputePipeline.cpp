#include "Graphics/ComputePipeline.h"
#include "Graphics/Renderer_VK.h"
#include "Graphics/VulkanHelpers.h"
#include "Graphics/ImageResource.h"
#include "Utils/DebugLog.h"
#include <filesystem>

extern std::string g_AppBasePath;

namespace ComputePipeline {

bool createRawToYuvPipeline(Renderer_VK* renderer) {
    namespace fs = std::filesystem;
    fs::path shaderPath = fs::path(g_AppBasePath) / "shaders_spv" / "raw_to_yuv422.comp.spv";
    auto code = VulkanHelpers::readFile(shaderPath.string());
    VkShaderModule module = VulkanHelpers::createShaderModule(renderer->m_device_p, code);

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

    VkPipelineLayoutCreateInfo pli{};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &renderer->m_computeDescriptorSetLayout;
    pli.pushConstantRangeCount = 0;
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

    return true;
}

void cleanup(Renderer_VK* renderer) {
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

} // namespace ComputePipeline

