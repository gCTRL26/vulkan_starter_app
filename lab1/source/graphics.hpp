#pragma once

#include <cstddef>
#include <cstdint>

#include <vulkan/vulkan_core.h>

#include <vk_mem_alloc.h>

namespace graphics {

struct Buffer {
	VkBuffer buffer = VK_NULL_HANDLE;
	VmaAllocation allocation = VK_NULL_HANDLE;
	void* memory = nullptr;
	VkDeviceSize size = 0;
};

bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, Buffer& buffer);
void destroyBuffer(Buffer& buffer);

void writeBuffer(const Buffer& buffer, const void* data, size_t size, size_t offset = 0);

VkShaderModule loadShaderModule(const char* file_name);

struct PipelineDescription {
	VkShaderModule vertex_shader;
	VkShaderModule fragment_shader;
	VkPipelineLayout layout;

	const VkVertexInputBindingDescription* vertex_bindings;
	uint32_t vertex_binding_count;
	const VkVertexInputAttributeDescription* vertex_attributes;
	uint32_t vertex_attribute_count;

	VkPrimitiveTopology topology;
	VkCullModeFlags cull_mode;
	bool depth_bias;
};

VkPipeline createGraphicsPipeline(const PipelineDescription& description);

} // namespace graphics
