#include "Graphics/GpuYuvConverter.h"
#include "Graphics/Renderer_VK.h"
#include "Graphics/VulkanHelpers.h"
#include "Utils/DebugLog.h"
#include <stdexcept>
#include <cstring>

GpuYuvConverter::GpuYuvConverter() = default;
GpuYuvConverter::~GpuYuvConverter() { cleanup(); }

bool GpuYuvConverter::init(Renderer_VK* renderer, int width, int height, int ringSize){
    m_renderer = renderer;
    m_width = width;
    m_height = height;
    m_ringSize = ringSize;
    m_index = 0;

    // Descriptor layout: binding 0 sampled raw image, binding 1 storage image
    VkDescriptorSetLayoutBinding b0{}; b0.binding = 0; b0.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; b0.descriptorCount=1; b0.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutBinding b1{}; b1.binding = 1; b1.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; b1.descriptorCount=1; b1.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutBinding bindings[2] = {b0,b1};
    VkDescriptorSetLayoutCreateInfo dci{}; dci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO; dci.bindingCount=2; dci.pBindings=bindings;
    VK_CHECK_RENDERER(vkCreateDescriptorSetLayout(renderer->m_device_p,&dci,nullptr,&m_descLayout));

    VkPipelineLayoutCreateInfo plci{}; plci.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO; plci.setLayoutCount=1; plci.pSetLayouts=&m_descLayout;
    VK_CHECK_RENDERER(vkCreatePipelineLayout(renderer->m_device_p,&plci,nullptr,&m_pipelineLayout));

    // Load compute shader
    extern std::string g_AppBasePath;
    std::string spv = (std::filesystem::path(g_AppBasePath)/"shaders_spv"/"raw_to_yuv422.comp.spv").string();
    auto code = VulkanHelpers::readFile(spv);
    VkShaderModule module = VulkanHelpers::createShaderModule(renderer->m_device_p, code);

    VkComputePipelineCreateInfo cpci{}; cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO; cpci.layout=m_pipelineLayout;
    VkPipelineShaderStageCreateInfo psi{}; psi.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; psi.stage=VK_SHADER_STAGE_COMPUTE_BIT; psi.module=module; psi.pName="main";
    cpci.stage = psi;
    VK_CHECK_RENDERER(vkCreateComputePipelines(renderer->m_device_p,VK_NULL_HANDLE,1,&cpci,nullptr,&m_pipeline));
    vkDestroyShaderModule(renderer->m_device_p,module,nullptr);

    // Descriptor pool
    VkDescriptorPoolSize poolSizes[2]{}; poolSizes[0].type=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; poolSizes[0].descriptorCount=ringSize; poolSizes[1].type=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; poolSizes[1].descriptorCount=ringSize;
    VkDescriptorPoolCreateInfo dpci{}; dpci.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO; dpci.maxSets=ringSize; dpci.poolSizeCount=2; dpci.pPoolSizes=poolSizes;
    VK_CHECK_RENDERER(vkCreateDescriptorPool(renderer->m_device_p,&dpci,nullptr,&m_descPool));

    m_images.resize(ringSize);
    std::vector<VkDescriptorSetLayout> layouts(ringSize,m_descLayout);
    VkDescriptorSetAllocateInfo dsai{}; dsai.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO; dsai.descriptorPool=m_descPool; dsai.descriptorSetCount=ringSize; dsai.pSetLayouts=layouts.data();
    std::vector<VkDescriptorSet> sets(ringSize);
    VK_CHECK_RENDERER(vkAllocateDescriptorSets(renderer->m_device_p,&dsai,sets.data()));

    for(int i=0;i<ringSize;++i){
        ImageItem item{}; item.set=sets[i];
        VkImageCreateInfo ici{}; ici.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO; ici.imageType=VK_IMAGE_TYPE_2D; ici.extent={static_cast<uint32_t>(width),static_cast<uint32_t>(height),1};
        ici.mipLevels=1; ici.arrayLayers=1; ici.format=VK_FORMAT_G10X6_B10X6_R10X6_2PACK16; ici.tiling=VK_IMAGE_TILING_OPTIMAL;
        ici.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED; ici.usage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT; ici.samples=VK_SAMPLE_COUNT_1_BIT;
        VmaAllocationCreateInfo aci{}; aci.usage=VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        VK_CHECK_RENDERER(vmaCreateImage(renderer->m_allocator_p,&ici,&aci,&item.image,&item.allocation,nullptr));
        VkImageViewCreateInfo ivci{}; ivci.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO; ivci.image=item.image; ivci.viewType=VK_IMAGE_VIEW_TYPE_2D; ivci.format=ici.format; ivci.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT; ivci.subresourceRange.levelCount=1; ivci.subresourceRange.layerCount=1; VK_CHECK_RENDERER(vkCreateImageView(renderer->m_device_p,&ivci,nullptr,&item.view));
        VkDescriptorImageInfo in{}; in.imageLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; in.imageView=renderer->m_rawImageView; in.sampler=renderer->m_rawImageSampler;
        VkDescriptorImageInfo out{}; out.imageLayout=VK_IMAGE_LAYOUT_GENERAL; out.imageView=item.view; out.sampler=VK_NULL_HANDLE;
        VkWriteDescriptorSet writes[2]{}; writes[0].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; writes[0].dstSet=item.set; writes[0].dstBinding=0; writes[0].descriptorCount=1; writes[0].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; writes[0].pImageInfo=&in;
        writes[1].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; writes[1].dstSet=item.set; writes[1].dstBinding=1; writes[1].descriptorCount=1; writes[1].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; writes[1].pImageInfo=&out;
        vkUpdateDescriptorSets(renderer->m_device_p,2,writes,0,nullptr);
        m_images[i]=item;
    }
    return true;
}

void GpuYuvConverter::cleanup(){
    if(!m_renderer) return;
    for(auto& it:m_images){
        if(it.view) vkDestroyImageView(m_renderer->m_device_p,it.view,nullptr);
        if(it.image) vmaDestroyImage(m_renderer->m_allocator_p,it.image,it.allocation);
    }
    m_images.clear();
    if(m_descPool) vkDestroyDescriptorPool(m_renderer->m_device_p,m_descPool,nullptr); m_descPool=VK_NULL_HANDLE;
    if(m_pipeline) vkDestroyPipeline(m_renderer->m_device_p,m_pipeline,nullptr); m_pipeline=VK_NULL_HANDLE;
    if(m_pipelineLayout) vkDestroyPipelineLayout(m_renderer->m_device_p,m_pipelineLayout,nullptr); m_pipelineLayout=VK_NULL_HANDLE;
    if(m_descLayout) vkDestroyDescriptorSetLayout(m_renderer->m_device_p,m_descLayout,nullptr); m_descLayout=VK_NULL_HANDLE;
}

VkImage GpuYuvConverter::convert(const nlohmann::json& meta,double black,double white,int cfa){
    if(m_images.empty()) return VK_NULL_HANDLE;
    ImageItem& item = m_images[m_index];
    m_index = (m_index+1)%m_ringSize;

    VkCommandBuffer cmd = VulkanHelpers::beginSingleTimeCommands(m_renderer->m_device_p,m_renderer->m_hostSiteCommandPool_p);
    vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,m_pipeline);
    vkCmdBindDescriptorSets(cmd,VK_PIPELINE_BIND_POINT_COMPUTE,m_pipelineLayout,0,1,&item.set,0,nullptr);
    struct Push{int W;int H;int cfaType;float black;float white;float gainR;float gainG;float gainB;float ccm[9];} push{};
    push.W=m_width; push.H=m_height; push.cfaType=cfa; push.black=(float)black; push.white=(float)white; push.gainR=1.0f; push.gainG=1.0f; push.gainB=1.0f;
    auto ccm = meta.value("ColorMatrix", std::vector<float>{1,0,0,0,1,0,0,0,1});
    for(int i=0;i<9 && i<(int)ccm.size();++i) push.ccm[i]=ccm[i];
    vkCmdPushConstants(cmd,m_pipelineLayout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(Push),&push);
    vkCmdDispatch(cmd,(uint32_t)((m_width+15)/16),(uint32_t)((m_height+15)/16),1);
    VulkanHelpers::endSingleTimeCommands(m_renderer->m_device_p,m_renderer->m_hostSiteCommandPool_p,m_renderer->m_graphicsQueue_p,cmd);
    return item.image;
}

bool GpuYuvConverter::readback(VkImage image, std::vector<uint16_t>& out){
    if(!m_renderer || image==VK_NULL_HANDLE) return false;
    VkDevice device = m_renderer->m_device_p;
    VkCommandPool pool = m_renderer->m_hostSiteCommandPool_p;
    VkQueue queue = m_renderer->m_graphicsQueue_p;
    VkDeviceSize bufSize = static_cast<VkDeviceSize>(m_width*m_height*4*sizeof(uint16_t));

    VkBufferCreateInfo bci{}; bci.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO; bci.size=bufSize; bci.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo aci{}; aci.usage=VMA_MEMORY_USAGE_AUTO_PREFER_HOST; aci.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    VkBuffer buf; VmaAllocation alloc; VmaAllocationInfo info{};
    VK_CHECK_RENDERER(vmaCreateBuffer(m_renderer->m_allocator_p,&bci,&aci,&buf,&alloc,&info));

    VkCommandBuffer cmd = VulkanHelpers::beginSingleTimeCommands(device,pool);
    VkImageMemoryBarrier bar{}; bar.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER; bar.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; bar.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    bar.image=image; bar.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT; bar.subresourceRange.levelCount=1; bar.subresourceRange.layerCount=1;
    bar.oldLayout=VK_IMAGE_LAYOUT_GENERAL; bar.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    bar.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT; bar.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&bar);

    VkBufferImageCopy region{}; region.bufferOffset=0; region.bufferRowLength=0; region.bufferImageHeight=0;
    region.imageSubresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT; region.imageSubresource.mipLevel=0; region.imageSubresource.baseArrayLayer=0; region.imageSubresource.layerCount=1;
    region.imageExtent={static_cast<uint32_t>(m_width),static_cast<uint32_t>(m_height),1};
    vkCmdCopyImageToBuffer(cmd,image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,buf,1,&region);

    bar.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; bar.newLayout=VK_IMAGE_LAYOUT_GENERAL; bar.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT; bar.dstAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,1,&bar);

    VulkanHelpers::endSingleTimeCommands(device,pool,queue,cmd);

    out.resize(bufSize/sizeof(uint16_t));
    memcpy(out.data(), info.pMappedData, bufSize);
    vmaUnmapMemory(m_renderer->m_allocator_p, alloc);
    vmaDestroyBuffer(m_renderer->m_allocator_p, buf, alloc);
    return true;
}
