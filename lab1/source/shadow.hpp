#pragma once

#include "texture.hpp"

struct ShadowMap {
	Texture depth;
	VkFormat format = VK_FORMAT_UNDEFINED;
};

bool createShadowMap(ShadowMap& map);
void beginShadowPass(VkCommandBuffer cmd, const ShadowMap& map, float bias);
void endShadowPass(VkCommandBuffer cmd, const ShadowMap& map);
