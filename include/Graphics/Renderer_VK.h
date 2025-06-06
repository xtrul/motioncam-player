#ifndef RENDERER_VK_H
#define RENDERER_VK_H

#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <optional>

#include <nlohmann/json.hpp>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "vma_usage.h" 
#include "Utils/RawFrameBuffer.h" // For RawBytes

// Include new sub-module headers
#include "Graphics/VulkanHelpers.h"
#include "Graphics/ImageResource.h" // ADD THIS LINE
#include "Graphics/Pipeline.h"      // ADD THIS LINE
#include "Graphics/Descriptor.h"    // ADD THIS LINE
// For VK_CHECK_RENDERER, if used, and helper function declarations
// Other headers like ImageResource.h, Pipeline.h, Descriptor.h are not directly included here
// as their functions are called from Renderer_VK.cpp via namespaces.
// If Renderer_VK class itself needs types from them, they would be included.

class Renderer_VK {
public:
    Renderer_VK(
        VkPhysicalDevice physicalDevice,
        VkDevice device,
        VmaAllocator allocator,
        VkQueue graphicsQueue,
        VkCommandPool commandPool
    );
    ~Renderer_VK();

    bool init(VkRenderPass renderPass, uint32_t swapChainImageCount);
    void cleanup();
    void onSwapChainRecreated(VkRenderPass renderPass, uint32_t swapChainImageCount);
    // cleanupSwapChainResources is now a free function in Pipeline.cpp, called internally

    void prepareAndUploadFrameData(
        VkCommandBuffer commandBuffer,
        uint32_t uboBindingIndex,
        VkBuffer prefilledStagingBuffer,
        int frameWidth, int frameHeight,
        const nlohmann::json& frameMetadata,
        double staticBlack, double staticWhite, int cfaTypeOverride,
        bool forceUpload
    );

    void recordDrawCommands(
        VkCommandBuffer commandBuffer,
        uint32_t currentFrameIndex,
        int windowWidth,
        int windowHeight
    );

    static int getCfaType(const std::string& cfaString);
    void setZoomNativePixels(bool nativePixels);
    void setPanOffsets(float x, float y);
    void resetPanOffsets();
    float getPanX() const;
    float getPanY() const;
    int getImageWidth() const;
    int getImageHeight() const;
    void resetDimensions();
    void ensureRawImageCapacity(uint32_t w, uint32_t h);

    // Public members needed by helper namespaces (e.g., ImageResource, Pipeline, Descriptor)
    // These allow the namespaced functions to operate on Renderer_VK's state.
    VkPhysicalDevice m_physicalDevice_p; // Renamed to avoid conflict if original was public
    VkDevice m_device_p;
    VmaAllocator m_allocator_p;
    VkQueue m_graphicsQueue_p;
    VkCommandPool m_hostSiteCommandPool_p; // Command pool provided by App

    VkImage m_rawImage = VK_NULL_HANDLE;
    VmaAllocation m_rawImageAllocation = VK_NULL_HANDLE;
    VkImageView m_rawImageView = VK_NULL_HANDLE;
    VkSampler m_rawImageSampler = VK_NULL_HANDLE;

    std::vector<VkBuffer> m_uniformBuffers;
    std::vector<VmaAllocation> m_uniformBufferAllocations;
    std::vector<void*> m_uniformBuffersMapped;

    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_graphicsPipeline = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_descriptorSets;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;

    uint32_t m_swapChainImageCount = 0; // Needed by Descriptor helpers

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
    };

    // Internal state not directly manipulated by namespaced helpers
    int m_currentRawW = 0;
    int m_currentRawH = 0;
    bool m_zoomNativePixels = false;
    float m_panX = 0.0f;
    float m_panY = 0.0f;

    // Private methods that remain part of Renderer_VK class
    void updateUniformBuffer(uint32_t currentImageIndex, const ShaderParamsUBO& ubo);

    // Friend declarations for helper namespaces to access private members if necessary,
    // or make members they need public (as done above with _p suffix).
    friend bool ImageResource::createRawImageResources(Renderer_VK* renderer, int width, int height);
    friend void ImageResource::cleanupRawImageResources(Renderer_VK* renderer);
    friend bool Pipeline::createGraphicsPipeline(Renderer_VK* renderer, VkRenderPass renderPass);
    friend void Pipeline::cleanupSwapChainResources(Renderer_VK* renderer);
    friend bool Descriptor::createDescriptorSetLayout(Renderer_VK* renderer);
    friend bool Descriptor::createDescriptorPool(Renderer_VK* renderer);
    friend bool Descriptor::createDescriptorSets(Renderer_VK* renderer);
    friend void Descriptor::updateDescriptorSetsWithNewRawImage(Renderer_VK* renderer);
    friend bool Descriptor::createUniformBuffers(Renderer_VK* renderer);
    friend void Descriptor::cleanupUniformBuffers(Renderer_VK* renderer);
};

#endif // RENDERER_VK_H