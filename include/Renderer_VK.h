#ifndef RENDERER_VK_H
#define RENDERER_VK_H

#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <optional>
#include <nlohmann/json_fwd.hpp>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include "vma_usage.h"

class Renderer_VK {
public:
    Renderer_VK(VkPhysicalDevice physicalDevice, VkDevice device, VmaAllocator allocator, VkQueue graphicsQueue, VkCommandPool commandPool);
    ~Renderer_VK();

    bool init(VkRenderPass renderPass, uint32_t swapChainImageCount);
    void cleanup();

    void onSwapChainRecreated(VkRenderPass renderPass, uint32_t swapChainImageCount);
    void cleanupSwapChainResources();

    // CORRECTED: Added ensureRawImageReadyAndUpload declaration
    void ensureRawImageReadyAndUpload(VkCommandBuffer commandBuffer,
                                      const std::vector<uint16_t>& rawData,
                                      int width, int height,
                                      uint32_t currentFrameIndex);

    // CORRECTED: Removed rawData from recordRenderCommands declaration
    void recordRenderCommands(VkCommandBuffer commandBuffer, uint32_t currentFrameIndex,
        const nlohmann::json& frameMetadata,
        double staticBlack, double staticWhite, int cfaTypeOverride,
        int windowWidth, int windowHeight, int orientationTag);

    static int getCfaType(const std::string& cfa);
    void setZoomNativePixels(bool nativePixels);
    void setPanOffsets(float x, float y);
    void resetPanOffsets();
    float getPanX() const;
    float getPanY() const;
    int getImageWidth() const;
    int getImageHeight() const;
    void resetDimensions();

private:
    struct ShaderParamsUBO {
        alignas(4) int W;
        alignas(4) int H;
        alignas(4) int cfaType;
        alignas(4) float exposure;
        alignas(4) float blackLevel;
        alignas(4) float whiteLevel;
        alignas(4) float invBlackWhiteRange;
        alignas(4) float gainR;
        alignas(4) float gainG;
        alignas(4) float gainB;
        alignas(16) glm::mat4 CCM;
        alignas(4) float saturationAdjustment;
        alignas(4) int   orientation;
    };

    VkPhysicalDevice m_physicalDevice;
    VkDevice m_device;
    VmaAllocator m_allocator;
    VkQueue m_graphicsQueue;
    VkCommandPool m_hostSiteCommandPool;

    VkImage m_rawImage;
    VmaAllocation m_rawImageAllocation;
    VkImageView m_rawImageView;
    VkSampler m_rawImageSampler;

    VkBuffer m_stagingBuffer;
    VmaAllocation m_stagingBufferAllocation;
    void* m_stagingBufferMappedData;
    VkDeviceSize m_stagingBufferSize;

    std::vector<VkBuffer> m_uniformBuffers;
    std::vector<VmaAllocation> m_uniformBufferAllocations;
    std::vector<void*> m_uniformBuffersMapped;

    VkDescriptorSetLayout m_descriptorSetLayout;
    VkPipelineLayout m_pipelineLayout;
    VkPipeline m_graphicsPipeline;
    std::vector<VkDescriptorSet> m_descriptorSets;
    VkDescriptorPool m_descriptorPool;

    int m_currentRawW;
    int m_currentRawH;
    bool m_zoomNativePixels;
    float m_panX;
    float m_panY;
    uint32_t m_swapChainImageCount;

    bool createRawImageResources(int width, int height);
    void cleanupRawImageResources();
    bool createUniformBuffers();
    void cleanupUniformBuffers();
    bool createDescriptorSetLayout();
    bool createGraphicsPipeline(VkRenderPass renderPass);
    bool createDescriptorPool();
    bool createDescriptorSets();

    void updateUniformBuffer(uint32_t currentImageIndex, const ShaderParamsUBO& ubo);
    // CORRECTED: Declaration for the private helper method
    void recordUploadRawTextureData(VkCommandBuffer commandBuffer,
                                   const std::vector<uint16_t>& rawData,
                                   int width, int height);


    static std::vector<char> readFile(const std::string& filename);
    VkShaderModule createShaderModule(const std::vector<char>& code);

    VkCommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(VkCommandBuffer commandBuffer);
};

#endif // RENDERER_VK_H
