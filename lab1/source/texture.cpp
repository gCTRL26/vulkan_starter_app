#include "texture.hpp"
#include "graphics_internal.hpp"

#include <iostream>
#include <lodepng.h>

namespace {
using graphics::internal::context;

bool createDeviceBuffer(VkDeviceSize size, VkBufferUsageFlags usage, graphics::Buffer& result) {
    const VkBufferCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    };
    const VmaAllocationCreateInfo memory = {
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
    };
    return vmaCreateBuffer(context.allocator, &info, &memory, &result.buffer,
                           &result.allocation, nullptr) == VK_SUCCESS;
}

bool createTexture(unsigned width, unsigned height, Texture& texture) {
    const VkImageCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_SRGB,
        .extent = {width, height, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
    };
    const VmaAllocationCreateInfo memory = {
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
    };
    if (vmaCreateImage(context.allocator, &info, &memory, &texture.image,
                       &texture.allocation, nullptr) != VK_SUCCESS) return false;

    const VkImageViewCreateInfo view = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = texture.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = info.format,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    const VkSamplerCreateInfo sampler = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
    };
    return vkCreateImageView(context.device, &view, nullptr, &texture.view) == VK_SUCCESS &&
           vkCreateSampler(context.device, &sampler, nullptr, &texture.sampler) == VK_SUCCESS;
}

bool copyToGpu(VkCommandPool pool, VkBuffer staging, const VkBuffer buffers[3],
               const VkDeviceSize sizes[3], VkImage image, unsigned width, unsigned height) {
    VkCommandBuffer cmd;
    const VkCommandBufferAllocateInfo alloc = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    const VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    if (vkAllocateCommandBuffers(context.device, &alloc, &cmd) != VK_SUCCESS ||
        vkBeginCommandBuffer(cmd, &begin) != VK_SUCCESS) return false;

    VkDeviceSize offset = 0;
    for (int i = 0; i < 3; ++i) {
        const VkBufferCopy region = {offset, 0, sizes[i]};
        vkCmdCopyBuffer(cmd, staging, buffers[i], 1, &region);
        offset += sizes[i];
    }

    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
    const VkBufferImageCopy region = {
        .bufferOffset = offset,
        .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        .imageExtent = {width, height, 1},
    };
    vkCmdCopyBufferToImage(cmd, staging, image, barrier.newLayout, 1, &region);

    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    const VkMemoryBarrier geometry = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT,
    };
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
                         0, 1, &geometry, 0, nullptr, 1, &barrier);

    const VkSubmitInfo submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd,
    };
    return vkEndCommandBuffer(cmd) == VK_SUCCESS &&
           vkQueueSubmit(context.graphics_queue, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS &&
           vkQueueWaitIdle(context.graphics_queue) == VK_SUCCESS;
}
}

bool uploadMeshAndTexture(const hexahedron::Mesh& mesh, graphics::Buffer& vertices,
                          graphics::Buffer& triangles, graphics::Buffer& edges, Texture& texture) {
    std::vector<unsigned char> pixels;
    unsigned width, height;
    unsigned error = lodepng::decode(pixels, width, height, PROJECT_ROOT_DIR "/textures/checker.png");
    if (error) {
        std::cerr << "checker.png: " << lodepng_error_text(error) << '\n';
        return false;
    }
    const VkDeviceSize sizes[] = {mesh.vertices.size() * sizeof(Vertex),
                                  mesh.triangles.size() * sizeof(uint32_t),
                                  mesh.edges.size() * sizeof(uint32_t)};
    if (!createDeviceBuffer(sizes[0], VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertices) ||
        !createDeviceBuffer(sizes[1], VK_BUFFER_USAGE_INDEX_BUFFER_BIT, triangles) ||
        !createDeviceBuffer(sizes[2], VK_BUFFER_USAGE_INDEX_BUFFER_BIT, edges) ||
        !createTexture(width, height, texture)) return false;

    const VkDeviceSize pixel_offset = sizes[0] + sizes[1] + sizes[2];
    graphics::Buffer staging;
    if (!graphics::createBuffer(pixel_offset + pixels.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                staging)) return false;
    graphics::writeBuffer(staging, mesh.vertices.data(), sizes[0]);
    graphics::writeBuffer(staging, mesh.triangles.data(), sizes[1], sizes[0]);
    graphics::writeBuffer(staging, mesh.edges.data(), sizes[2], sizes[0] + sizes[1]);
    graphics::writeBuffer(staging, pixels.data(), pixels.size(), pixel_offset);

    const VkCommandPoolCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = context.graphics_queue_index,
    };
    VkCommandPool pool{};
    bool ok = vkCreateCommandPool(context.device, &info, nullptr, &pool) == VK_SUCCESS;
    if (ok) {
        const VkBuffer buffers[] = {vertices.buffer, triangles.buffer, edges.buffer};
        ok = copyToGpu(pool, staging.buffer, buffers, sizes, texture.image, width, height);
        vkDestroyCommandPool(context.device, pool, nullptr);
    }
    graphics::destroyBuffer(staging);
    return ok;
}

void destroyTexture(Texture& texture) {
    vkDestroySampler(context.device, texture.sampler, nullptr);
    vkDestroyImageView(context.device, texture.view, nullptr);
    if (texture.image) vmaDestroyImage(context.allocator, texture.image, texture.allocation);
    texture = {};
}
