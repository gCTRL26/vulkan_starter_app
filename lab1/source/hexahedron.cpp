#include "hexahedron.hpp"

#include <bit>
#include <cmath>
#include <utility>

namespace hexahedron {

Mesh build(float edge_length) {
	Mesh mesh = {
		.edge_length = edge_length,
		.corner_count = 8,
		.face_count = 0,
	};

	const float h = edge_length * 0.5f;

	math::Vector3 corners[8];
	for (uint32_t i = 0; i < 8; ++i) {
		corners[i] = { (i & 1) ? h : -h, (i & 2) ? h : -h, (i & 4) ? h : -h };
	}

	uint32_t corner_vertex[8] = {};

	for (uint32_t axis = 0; axis < 3; ++axis) {
		const uint32_t u = (axis + 1) % 3;
		const uint32_t v = (axis + 2) % 3;

		for (uint32_t side = 0; side < 2; ++side) {
			const uint32_t square[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };

			float n[3] = { 0.0f, 0.0f, 0.0f };
			n[axis] = side ? 1.0f : -1.0f;
			const math::Vector3 normal = { n[0], n[1], n[2] };

			const uint32_t first = uint32_t(mesh.vertices.size());

			for (uint32_t c = 0; c < 4; ++c) {
				const uint32_t corner = (side << axis) | (square[c][0] << u) | (square[c][1] << v);
				corner_vertex[corner] = uint32_t(mesh.vertices.size());
				mesh.vertices.push_back({
					.position = corners[corner],
					.normal = normal,
					.color = { 1.0f, 1.0f, 1.0f },
					.uv = { float(square[c][0]), float(square[c][1]) },
				});
			}

			uint32_t triangles[2][3] = {
				{ first, first + 1, first + 2 },
				{ first, first + 2, first + 3 },
			};

			for (auto& triangle : triangles) {
				const math::Vector3& a = mesh.vertices[triangle[0]].position;
				const math::Vector3& b = mesh.vertices[triangle[1]].position;
				const math::Vector3& c = mesh.vertices[triangle[2]].position;

				if (math::dot(math::cross(b - a, c - a), normal) > 0.0f) {
					std::swap(triangle[1], triangle[2]);
				}

				mesh.triangles.insert(mesh.triangles.end(), triangle, triangle + 3);
			}

			++mesh.face_count;
		}
	}

	for (uint32_t i = 0; i < 8; ++i) {
		for (uint32_t j = i + 1; j < 8; ++j) {
			if (std::has_single_bit(i ^ j)) {
				mesh.edges.push_back(corner_vertex[i]);
				mesh.edges.push_back(corner_vertex[j]);
			}
		}
	}

	return mesh;
}

} // namespace hexahedron
