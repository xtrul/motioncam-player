#ifndef RENDERER_VK_H
#define RENDERER_VK_H

#include <vulkan/vulkan.h> // Vulkan main header
#include <vector>
#include <string>
#include <optional>

// Use nlohmann/json for metadata. Full include if methods need it,
// fwd declare if only passing by const ref without deep member access.
#include <nlohmann/json.hpp>

// GLM for math (matrices, vectors)
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE // Vulkan uses 0-1 depth range
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp> // For glm::lookAt, glm::perspective etc. (if used)

#include "vma_usage.h" // Vulkan Memory Allocator
#include "RawFrameBuffer.h" // Added for RawBytes type

class Renderer_VK {
public:
    Renderer_VK(
        VkPhysicalDevice physicalDevice,
        VkDevice device,
        VmaAllocator allocator,
        VkQueue graphicsQueue,
        VkCommandPool commandPool // Command pool from App, for single-time commands
    );
    ~Renderer_VK();

    // Initializes renderer resources like pipelines, descriptor sets, etc.
    // `renderPass`: The main application render pass.
    // `swapChainImageCount`: Number of images in the swapchain, for per-frame resources.
    bool init(VkRenderPass renderPass, uint32_t swapChainImageCount);

    // Cleans up all renderer resources.
    void cleanup();

    // Called when the swapchain is recreated (e.g., window resize).
    // Recreates swapchain-dependent resources.
    void onSwapChainRecreated(VkRenderPass renderPass, uint32_t swapChainImageCount);

    // Cleans up only swapchain-dependent resources.
    void cleanupSwapChainResources();

    // Records rendering commands into the provided command buffer for the current frame.
    void recordRenderCommands(
        VkCommandBuffer commandBuffer,
        uint32_t currentFrameIndex, // Index for per-frame resources (UBO, descriptor set)
        const RawBytes& rawData,    // Changed from std::vector<uint16_t>
        const nlohmann::json& frameMetadata,
        double staticBlack, // Fallback black level
        double staticWhite, // Fallback white level
        int cfaTypeOverride, // CFA pattern (0:BGGR, 1:RGGB, 2:GBRG, 3:GRBG)
        int windowWidth,     // Current window width for viewport calculation
        int windowHeight     // Current window height for viewport calculation
    );

    static int getCfaType(const std::string& cfaString); // Converts CFA string to integer type
    void setZoomNativePixels(bool nativePixels);
    void setPanOffsets(float x, float y); // For panning when zoomed
    void resetPanOffsets();
    float getPanX() const;
    float getPanY() const;
    int getImageWidth() const;  // Current raw image width being processed
    int getImageHeight() const; // Current raw image height being processed
    void resetDimensions();     // Resets stored image dimensions (e.g., on new file load)

private:
    // Uniform Buffer Object structure for shader parameters
    struct ShaderParamsUBO {
        alignas(4) int W;                   // Image width
        alignas(4) int H;                   // Image height
        alignas(4) int cfaType;             // CFA pattern type
        alignas(4) float exposure;          // Exposure adjustment
        alignas(4) float blackLevel;        // Black level
        alignas(4) float whiteLevel;        // White level
        alignas(4) float invBlackWhiteRange;// 1.0 / (whiteLevel - blackLevel)
        alignas(4) float gainR;             // Red channel gain (white balance)
        alignas(4) float gainG;             // Green channel gain (white balance)
        alignas(4) float gainB;             // Blue channel gain (white balance)
        alignas(16) glm::mat4 CCM;          // Color Correction Matrix (using mat4 for alignment, only mat3 part used)
        alignas(4) float saturationAdjustment; // Saturation adjustment factor
    };

    VkPhysicalDevice m_physicalDevice;
    VkDevice m_device;
    VmaAllocator m_allocator;
    VkQueue m_graphicsQueue;
    VkCommandPool m_hostSiteCommandPool; // Command pool owned by App, used for one-off tasks

    // Raw image resources (16-bit uint texture)
    VkImage m_rawImage = VK_NULL_HANDLE;
    VmaAllocation m_rawImageAllocation = VK_NULL_HANDLE;
    VkImageView m_rawImageView = VK_NULL_HANDLE;
    VkSampler m_rawImageSampler = VK_NULL_HANDLE;

    // Uniform buffers (one per frame in flight)
    std::vector<VkBuffer> m_uniformBuffers;
    std::vector<VmaAllocation> m_uniformBufferAllocations;
    std::vector<void*> m_uniformBuffersMapped; // Mapped memory for UBOs

    // Descriptor sets and pipeline
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_graphicsPipeline = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_descriptorSets;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;

    int m_currentRawW = 0; // Stored width of the current m_rawImage
    int m_currentRawH = 0; // Stored height of the current m_rawImage
    bool m_zoomNativePixels = false;
    float m_panX = 0.0f;
    float m_panY = 0.0f;
    uint32_t m_swapChainImageCount = 0; // Number of images in the swapchain

    // Internal helper methods for resource creation/destruction
    bool createRawImageResources(int width, int height);
    void cleanupRawImageResources();
    bool createUniformBuffers();
    void cleanupUniformBuffers();
    bool createDescriptorSetLayout();
    bool createGraphicsPipeline(VkRenderPass renderPass);
    bool createDescriptorPool();
    bool createDescriptorSets();

    void updateUniformBuffer(uint32_t currentImageIndex, const ShaderParamsUBO& ubo);
    void uploadRawTextureData(VkCommandBuffer commandBuffer, const RawBytes& rawData, int width, int height); // Changed from std::vector<uint16_t>

    static std::vector<char> readFile(const std::string& filename);
    VkShaderModule createShaderModule(const std::vector<char>& code);

    // Helpers for single-time command buffer operations
    VkCommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(VkCommandBuffer commandBuffer);
};

#endif // RENDERER_VK_H