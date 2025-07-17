#ifndef DESCRIPTOR_H
#define DESCRIPTOR_H

#include <vulkan/vulkan.h>

class Renderer_VK; // Forward declaration

namespace Descriptor {

	bool createDescriptorSetLayout(Renderer_VK* renderer);
	bool createDescriptorPool(Renderer_VK* renderer);
	bool createDescriptorSets(Renderer_VK* renderer);
	void updateDescriptorSetsWithNewRawImage(Renderer_VK* renderer);

	// Uniform buffer functions are closely related to descriptors
	bool createUniformBuffers(Renderer_VK* renderer);
	void cleanupUniformBuffers(Renderer_VK* renderer);

} // namespace Descriptor

#endif // DESCRIPTOR_H