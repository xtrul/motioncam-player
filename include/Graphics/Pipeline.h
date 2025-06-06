#ifndef PIPELINE_H
#define PIPELINE_H

#include <vulkan/vulkan.h>

class Renderer_VK; // Forward declaration

namespace Pipeline {

	bool createGraphicsPipeline(Renderer_VK* renderer, VkRenderPass renderPass);
	void cleanupSwapChainResources(Renderer_VK* renderer); // This also cleans up pipeline and layout

} // namespace Pipeline

#endif // PIPELINE_H