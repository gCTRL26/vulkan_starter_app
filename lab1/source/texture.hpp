#pragma once

#include "graphics.hpp"
#include "hexahedron.hpp"

struct Texture {
    VkImage image{};
    VmaAllocation allocation{};
    VkImageView view{};
    VkSampler sampler{};
};

bool uploadMeshAndTexture(const hexahedron::Mesh& mesh, graphics::Buffer& vertices,
                          graphics::Buffer& triangles, graphics::Buffer& edges, Texture& texture);
void destroyTexture(Texture& texture);
