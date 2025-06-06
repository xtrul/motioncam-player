#include "Graphics/Pipeline.h"
#include "Graphics/Renderer_VK.h" // To access Renderer_VK members
#include "Graphics/VulkanHelpers.h" // For readFile, createShaderModule, VK_CHECK_RENDERER
#include "Graphics/Descriptor.h"    // For cleanupUniformBuffers
#include "Utils/DebugLog.h"

#include <array> // For std::array

namespace Pipeline {

    bool createGraphicsPipeline(Renderer_VK* renderer, VkRenderPass renderPass) {
        LogToFile("[Pipeline::createGraphicsPipeline] Creating graphics pipeline...");

        if (renderer->m_graphicsPipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(renderer->m_device_p, renderer->m_graphicsPipeline, nullptr);
            renderer->m_graphicsPipeline = VK_NULL_HANDLE;
        }
        if (renderer->m_pipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(renderer->m_device_p, renderer->m_pipelineLayout, nullptr);
            renderer->m_pipelineLayout = VK_NULL_HANDLE;
        }

        std::string vertShaderPath = "shaders_spv/fullscreen_quad.vert.spv";
        std::string fragShaderPath = "shaders_spv/image_process.frag.spv";

        auto vertShaderCode = VulkanHelpers::readFile(vertShaderPath);
        auto fragShaderCode = VulkanHelpers::readFile(fragShaderPath);

        VkShaderModule vertShaderModule = VulkanHelpers::createShaderModule(renderer->m_device_p, vertShaderCode);
        VkShaderModule fragShaderModule = VulkanHelpers::createShaderModule(renderer->m_device_p, fragShaderCode);
        LogToFile("[Pipeline::createGraphicsPipeline] Shader modules created.");

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
        pipelineLayoutInfo.pSetLayouts = &renderer->m_descriptorSetLayout;
        VK_CHECK_RENDERER(vkCreatePipelineLayout(renderer->m_device_p, &pipelineLayoutInfo, nullptr, &renderer->m_pipelineLayout));
        LogToFile("[Pipeline::createGraphicsPipeline] Pipeline layout created.");

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
        pipelineInfo.layout = renderer->m_pipelineLayout;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;

        VK_CHECK_RENDERER(vkCreateGraphicsPipelines(renderer->m_device_p, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &renderer->m_graphicsPipeline));
        LogToFile("[Pipeline::createGraphicsPipeline] Graphics pipeline created.");

        vkDestroyShaderModule(renderer->m_device_p, fragShaderModule, nullptr);
        vkDestroyShaderModule(renderer->m_device_p, vertShaderModule, nullptr);
        LogToFile("[Pipeline::createGraphicsPipeline] Shader modules destroyed.");

        return true;
    }

    void cleanupSwapChainResources(Renderer_VK* renderer) {
        LogToFile("[Pipeline::cleanupSwapChainResources] Cleaning swapchain-dependent resources...");
        if (renderer->m_graphicsPipeline != VK_NULL_HANDLE) {
            LogToFile("[Pipeline::cleanupSwapChainResources] Destroying graphics pipeline.");
            vkDestroyPipeline(renderer->m_device_p, renderer->m_graphicsPipeline, nullptr);
            renderer->m_graphicsPipeline = VK_NULL_HANDLE;
        }
        if (renderer->m_pipelineLayout != VK_NULL_HANDLE) {
            LogToFile("[Pipeline::cleanupSwapChainResources] Destroying pipeline layout.");
            vkDestroyPipelineLayout(renderer->m_device_p, renderer->m_pipelineLayout, nullptr);
            renderer->m_pipelineLayout = VK_NULL_HANDLE;
        }
        if (renderer->m_descriptorPool != VK_NULL_HANDLE) {
            LogToFile("[Pipeline::cleanupSwapChainResources] Destroying descriptor pool.");
            vkDestroyDescriptorPool(renderer->m_device_p, renderer->m_descriptorPool, nullptr);
            renderer->m_descriptorPool = VK_NULL_HANDLE;
            renderer->m_descriptorSets.clear();
        }
        Descriptor::cleanupUniformBuffers(renderer); // UBOs are swapchain-dependent
        LogToFile("[Pipeline::cleanupSwapChainResources] Swapchain-dependent resources cleaned.");
    }


} // namespace Pipeline