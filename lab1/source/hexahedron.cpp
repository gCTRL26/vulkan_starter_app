#include "hexahedron.hpp"

#include <bit>
#include <cmath>
#include <utility>

namespace hexahedron {

namespace {

math::Vector3 hsvToRgb(float h, float s, float v) {
	h = (h - std::floor(h)) * 6.0f;
	const int sector = int(h) % 6;
	const float f = h - std::floor(h);
	const float p = v * (1.0f - s);
	const float q = v * (1.0f - s * f);
	const float t = v * (1.0f - s * (1.0f - f));

	switch (sector) {
	case 0: return { v, t, p };
	case 1: return { q, v, p };
	case 2: return { p, v, t };
	case 3: return { p, q, v };
	case 4: return { t, p, v };
	default: return { v, p, q };
	}
}

math::Vector3 mix(const math::Vector3& a, const math::Vector3& b, float t) {
	return a * (1.0f - t) + b * t;
}

} // namespace

Mesh build(float edge_length) {
	Mesh mesh = {
		.edge_length = edge_length,
		.face_count = 0,
	};

	const float h = edge_length * 0.5f;

	for (uint32_t i = 0; i < 8; ++i) {
		mesh.vertices.push_back({
			.position = { (i & 1) ? h : -h, (i & 2) ? h : -h, (i & 4) ? h : -h },
			.color = { 1.0f, 1.0f, 1.0f },
		});
	}

	for (uint32_t axis = 0; axis < 3; ++axis) {
		const uint32_t u = (axis + 1) % 3;
		const uint32_t v = (axis + 2) % 3;

		for (uint32_t side = 0; side < 2; ++side) {
			const uint32_t corners[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };

			uint32_t quad[4];
			math::Vector3 center = { 0.0f, 0.0f, 0.0f };

			for (uint32_t c = 0; c < 4; ++c) {
				quad[c] = (side << axis) | (corners[c][0] << u) | (corners[c][1] << v);
				center += mesh.vertices[quad[c]].position;
			}

			uint32_t triangles[2][3] = {
				{ quad[0], quad[1], quad[2] },
				{ quad[0], quad[2], quad[3] },
			};

			for (auto& triangle : triangles) {
				const math::Vector3& a = mesh.vertices[triangle[0]].position;
				const math::Vector3& b = mesh.vertices[triangle[1]].position;
				const math::Vector3& c = mesh.vertices[triangle[2]].position;

				if (math::dot(math::cross(b - a, c - a), center) > 0.0f) {
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
				mesh.edges.push_back(i);
				mesh.edges.push_back(j);
			}
		}
	}

	return mesh;
}

void paint(Mesh& mesh, ColorScheme scheme) {
	const float h = mesh.edge_length * 0.5f;

	for (Vertex& vertex : mesh.vertices) {
		const math::Vector3& p = vertex.position;

		const math::Vector3 n = {
			(p.x / h + 1.0f) * 0.5f,
			(p.y / h + 1.0f) * 0.5f,
			(p.z / h + 1.0f) * 0.5f,
		};

		switch (scheme) {
		case ColorScheme::PositionRGB:
			vertex.color = n;
			break;

		case ColorScheme::HeightGradient:
			vertex.color = mix({ 0.10f, 0.25f, 0.95f }, { 1.00f, 0.60f, 0.10f }, n.y);
			break;

		case ColorScheme::HueAroundY: {
			const float hue = std::atan2(p.z, p.x) / (2.0f * math::pi) + 0.5f;
			vertex.color = hsvToRgb(hue, 0.85f, 0.35f + 0.65f * n.y);
			break;
		}

		default:
			break;
		}
	}
}

} // namespace hexahedron
