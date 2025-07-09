#ifndef IMAGE_RESOURCE_H
#define IMAGE_RESOURCE_H

#include <vulkan/vulkan.h>
#include "Utils/vma_usage.h"

class Renderer_VK;

namespace ImageResource {

    bool createRawImageResources(Renderer_VK* renderer, int width, int height);
    void cleanupRawImageResources(Renderer_VK* renderer);

    void transitionImageLayout(
        VkDevice device,
        VkCommandPool commandPool,
        VkQueue queue,
        VkImage image,
        VkImageLayout oldLayout,
        VkImageLayout newLayout
    );

}

#endif