#pragma once

#include <cstdint>
#include <vector>

#include "math.hpp"

struct Vertex {
	math::Vector3 position;
	math::Vector3 color;
};

namespace hexahedron {

enum class ColorScheme : int {
	PositionRGB,
	HeightGradient,
	HueAroundY,
	Count,
};

inline const char* const color_scheme_names[] = {
	"RGB по координатам (x, y, z)",
	"Градиент по высоте (y)",
	"Радуга по углу вокруг оси Y",
};

struct Mesh {
	float edge_length;
	std::vector<Vertex> vertices;
	std::vector<uint32_t> triangles;
	std::vector<uint32_t> edges;
	uint32_t face_count;
};

Mesh build(float edge_length);

void paint(Mesh& mesh, ColorScheme scheme);

} // namespace hexahedron
