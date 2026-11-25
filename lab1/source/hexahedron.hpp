#pragma once

#include <cstdint>
#include <vector>

#include "math.hpp"

struct Vertex {
	math::Vector3 position;
	math::Vector3 normal;
	math::Vector3 color;
	float uv[2];
};

namespace hexahedron {

struct Mesh {
	float edge_length;
	std::vector<Vertex> vertices;
	std::vector<uint32_t> triangles;
	std::vector<uint32_t> edges;
	uint32_t corner_count;
	uint32_t face_count;
};

Mesh build(float edge_length);

} // namespace hexahedron
