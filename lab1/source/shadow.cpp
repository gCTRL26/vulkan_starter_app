#include "shadow.hpp"
#include "graphics_internal.hpp"

using graphics::internal::context;

namespace {
constexpr uint32_t size = 4096;
constexpr VkImageSubresourceRange depth_range = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
constexpr VkPipelineStageFlags depth_tests =
	VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
}

bool createShadowMap(ShadowMap& map) {
	const VkFormatFeatureFlags required = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT |
	                                      VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
	VkFilter filter = VK_FILTER_NEAREST;
	for (VkFormat format : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM}) {
		VkFormatProperties props;
		vkGetPhysicalDeviceFormatProperties(context.physical_device, format, &props);
		if ((props.optimalTilingFeatures & required) == required) {
			map.format = format;
			if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)
				filter = VK_FILTER_LINEAR;
			break;
		}
	}
	if (map.format == VK_FORMAT_UNDEFINED) return false;

	const VkImageCreateInfo image = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = map.format,
		.extent = { size, size, 1 },
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_OPTIMAL,
		.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
	};
	const VmaAllocationCreateInfo memory = { .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE };
	if (vmaCreateImage(context.allocator, &image, &memory, &map.depth.image,
	                   &map.depth.allocation, nullptr) != VK_SUCCESS) return false;

	const VkImageViewCreateInfo view = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
		.image = map.depth.image,
		.viewType = VK_IMAGE_VIEW_TYPE_2D,
		.format = map.format,
		.subresourceRange = depth_range,
	};
	const VkSamplerCreateInfo sampler = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = filter,
		.minFilter = filter,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
		.compareEnable = VK_TRUE,
		.compareOp = VK_COMPARE_OP_LESS,
		.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
	};
	return vkCreateImageView(context.device, &view, nullptr, &map.depth.view) == VK_SUCCESS &&
	       vkCreateSampler(context.device, &sampler, nullptr, &map.depth.sampler) == VK_SUCCESS;
}

void beginShadowPass(VkCommandBuffer cmd, const ShadowMap& map, float bias) {
	const VkImageMemoryBarrier barrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = map.depth.image,
		.subresourceRange = depth_range,
	};
	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, depth_tests,
	                     0, 0, nullptr, 0, nullptr, 1, &barrier);
	const VkRenderingAttachmentInfo depth = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.imageView = map.depth.view,
		.imageLayout = barrier.newLayout,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.clearValue = { .depthStencil = { 1.0f, 0 } },
	};
	const VkRect2D area = { {0, 0}, {size, size} };
	const VkRenderingInfo rendering = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
		.renderArea = area,
		.layerCount = 1,
		.pDepthAttachment = &depth,
	};
	vkCmdBeginRendering(cmd, &rendering);
	const VkViewport viewport = { 0, 0, float(size), float(size), 0, 1 };
	vkCmdSetViewport(cmd, 0, 1, &viewport);
	vkCmdSetScissor(cmd, 0, 1, &area);
	vkCmdSetDepthBias(cmd, bias, 0.0f, 1.0f);
}

void endShadowPass(VkCommandBuffer cmd, const ShadowMap& map) {
	vkCmdEndRendering(cmd);
	const VkImageMemoryBarrier barrier = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
		.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
		.image = map.depth.image,
		.subresourceRange = depth_range,
	};
	vkCmdPipelineBarrier(cmd, depth_tests, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
	                     0, 0, nullptr, 0, nullptr, 1, &barrier);
}
