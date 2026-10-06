#include "graphics.hpp"

#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "graphics_internal.hpp"

namespace graphics {

using internal::context;

bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, Buffer& buffer) {
	const VkBufferCreateInfo buffer_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = (size + 0xf) & ~VkDeviceSize(0xf),
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};

	const VmaAllocationCreateInfo allocation_info = {
		.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT |
		         VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT,
		.usage = VMA_MEMORY_USAGE_AUTO,
	};

	VmaAllocationInfo allocation_result = {};

	if (vmaCreateBuffer(context.allocator, &buffer_info, &allocation_info,
	                    &buffer.buffer, &buffer.allocation, &allocation_result) != VK_SUCCESS) {
		std::cerr << "Failed to create and allocate buffer\n";
		return false;
	}

	buffer.memory = allocation_result.pMappedData;
	buffer.size = buffer_info.size;

	return true;
}

void destroyBuffer(Buffer& buffer) {
	if (buffer.buffer != VK_NULL_HANDLE) {
		vmaDestroyBuffer(context.allocator, buffer.buffer, buffer.allocation);
	}

	buffer = {};
}

void writeBuffer(const Buffer& buffer, const void* data, size_t size, size_t offset) {
	std::memcpy(static_cast<char*>(buffer.memory) + offset, data, size);

	vmaFlushAllocation(context.allocator, buffer.allocation, offset, size);
}

VkShaderModule loadShaderModule(const char* file_name) {
	std::vector<std::string> paths = { std::string("shaders/") + file_name };
#ifdef PROJECT_ROOT_DIR
	paths.push_back(std::string(PROJECT_ROOT_DIR) + "/shaders/" + file_name);
#endif

	std::ifstream file;
	for (const std::string& path : paths) {
		file.open(path, std::ios::binary | std::ios::ate);
		if (file.is_open()) {
			break;
		}
		file.clear();
	}

	if (!file.is_open()) {
		std::cerr << "Failed to open shader file " << file_name << '\n';
		return VK_NULL_HANDLE;
	}

	const size_t size = size_t(file.tellg());
	std::vector<uint32_t> code((size + 3) / 4);

	file.seekg(0);
	file.read(reinterpret_cast<char*>(code.data()), std::streamsize(size));

	const VkShaderModuleCreateInfo shader_module = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = size,
		.pCode = code.data(),
	};

	VkShaderModule result;

	if (vkCreateShaderModule(context.device, &shader_module, nullptr, &result) != VK_SUCCESS) {
		std::cerr << "Failed to create shader module from " << file_name << '\n';
		return VK_NULL_HANDLE;
	}

	return result;
}

VkPipeline createGraphicsPipeline(const PipelineDescription& description) {
	const VkPipelineShaderStageCreateInfo stage_infos[] = {
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_VERTEX_BIT,
			.module = description.vertex_shader,
			.pName = "main",
		},
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			.module = description.fragment_shader,
			.pName = "main",
		},
	};

	const VkPipelineVertexInputStateCreateInfo input_state_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = description.vertex_binding_count,
		.pVertexBindingDescriptions = description.vertex_bindings,
		.vertexAttributeDescriptionCount = description.vertex_attribute_count,
		.pVertexAttributeDescriptions = description.vertex_attributes,
	};

	const VkPipelineInputAssemblyStateCreateInfo assembly_state_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = description.topology,
	};

	const VkPipelineViewportStateCreateInfo viewport_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.scissorCount = 1,
	};

	const VkPipelineRasterizationStateCreateInfo raster_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = description.cull_mode,
		.frontFace = VK_FRONT_FACE_CLOCKWISE,
		.depthBiasEnable = description.depth_bias,
		.depthBiasConstantFactor = description.depth_bias ? 1.0f : 0.0f,
		.depthBiasClamp = 0.0f,
		.depthBiasSlopeFactor = description.depth_bias ? 1.0f : 0.0f,
		.lineWidth = 1.0f,
	};

	const VkPipelineMultisampleStateCreateInfo sample_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
	};

	const VkPipelineDepthStencilStateCreateInfo depth_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = true,
		.depthWriteEnable = true,
		.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
	};

	const VkPipelineColorBlendAttachmentState attachment_info = {
		.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
		                  VK_COLOR_COMPONENT_G_BIT |
		                  VK_COLOR_COMPONENT_B_BIT |
		                  VK_COLOR_COMPONENT_A_BIT,
	};

	const VkPipelineColorBlendStateCreateInfo blend_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &attachment_info,
	};

	const VkDynamicState dynamic_states[] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR,
	};

	const VkPipelineDynamicStateCreateInfo dynamic_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = sizeof(dynamic_states) / sizeof(dynamic_states[0]),
		.pDynamicStates = dynamic_states,
	};

	const VkGraphicsPipelineCreateInfo pipeline_info = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = sizeof(stage_infos) / sizeof(stage_infos[0]),
		.pStages = stage_infos,
		.pVertexInputState = &input_state_info,
		.pInputAssemblyState = &assembly_state_info,
		.pViewportState = &viewport_info,
		.pRasterizationState = &raster_info,
		.pMultisampleState = &sample_info,
		.pDepthStencilState = &depth_info,
		.pColorBlendState = &blend_info,
		.pDynamicState = &dynamic_state,
		.layout = description.layout,
		.renderPass = context.render_pass,
		.subpass = 0,
	};

	VkPipeline pipeline;

	if (vkCreateGraphicsPipelines(context.device, VK_NULL_HANDLE, 1, &pipeline_info,
	                              nullptr, &pipeline) != VK_SUCCESS) {
		std::cerr << "Failed to create graphics pipeline\n";
		return VK_NULL_HANDLE;
	}

	return pipeline;
}

} // namespace graphics
