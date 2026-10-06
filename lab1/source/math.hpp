#pragma once

#include <cmath>

namespace math {

constexpr float pi = 3.14159265358979323846f;

constexpr float radians(float degrees) {
	return degrees * (pi / 180.0f);
}

struct Vector3 {
	float x, y, z;

	constexpr Vector3 operator+(const Vector3& v) const { return { x + v.x, y + v.y, z + v.z }; }
	constexpr Vector3 operator-(const Vector3& v) const { return { x - v.x, y - v.y, z - v.z }; }
	constexpr Vector3 operator-() const { return { -x, -y, -z }; }
	constexpr Vector3 operator*(float s) const { return { x * s, y * s, z * s }; }

	Vector3& operator+=(const Vector3& v) {
		x += v.x; y += v.y; z += v.z;
		return *this;
	}
};

constexpr float dot(const Vector3& a, const Vector3& b) {
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

constexpr Vector3 cross(const Vector3& a, const Vector3& b) {
	return {
		a.y * b.z - a.z * b.y,
		a.z * b.x - a.x * b.z,
		a.x * b.y - a.y * b.x,
	};
}

inline float length(const Vector3& v) {
	return std::sqrt(dot(v, v));
}

inline Vector3 normalize(const Vector3& v) {
	const float l = length(v);
	return l > 0.0f ? v * (1.0f / l) : v;
}

struct Matrix4 {
	float elements[4][4];

	static constexpr Matrix4 identity() {
		return {{
			{ 1, 0, 0, 0 },
			{ 0, 1, 0, 0 },
			{ 0, 0, 1, 0 },
			{ 0, 0, 0, 1 },
		}};
	}

	constexpr float& operator()(int row, int column) { return elements[column][row]; }
	constexpr float operator()(int row, int column) const { return elements[column][row]; }

	constexpr Matrix4 operator*(const Matrix4& m) const {
		Matrix4 result = {};
		for (int c = 0; c < 4; ++c) {
			for (int r = 0; r < 4; ++r) {
				float sum = 0.0f;
				for (int k = 0; k < 4; ++k) {
					sum += (*this)(r, k) * m(k, c);
				}
				result(r, c) = sum;
			}
		}
		return result;
	}

	constexpr Vector3 transformPoint(const Vector3& p) const {
		return {
			(*this)(0, 0) * p.x + (*this)(0, 1) * p.y + (*this)(0, 2) * p.z + (*this)(0, 3),
			(*this)(1, 0) * p.x + (*this)(1, 1) * p.y + (*this)(1, 2) * p.z + (*this)(1, 3),
			(*this)(2, 0) * p.x + (*this)(2, 1) * p.y + (*this)(2, 2) * p.z + (*this)(2, 3),
		};
	}

	static constexpr Matrix4 translation(const Vector3& t) {
		Matrix4 m = identity();
		m(0, 3) = t.x;
		m(1, 3) = t.y;
		m(2, 3) = t.z;
		return m;
	}

	static constexpr Matrix4 scale(const Vector3& s) {
		Matrix4 m = identity();
		m(0, 0) = s.x;
		m(1, 1) = s.y;
		m(2, 2) = s.z;
		return m;
	}

	static Matrix4 rotationX(float angle) {
		const float c = std::cos(angle), s = std::sin(angle);
		Matrix4 m = identity();
		m(1, 1) = c; m(1, 2) = -s;
		m(2, 1) = s; m(2, 2) = c;
		return m;
	}

	static Matrix4 rotationY(float angle) {
		const float c = std::cos(angle), s = std::sin(angle);
		Matrix4 m = identity();
		m(0, 0) = c;  m(0, 2) = s;
		m(2, 0) = -s; m(2, 2) = c;
		return m;
	}

	static Matrix4 rotationZ(float angle) {
		const float c = std::cos(angle), s = std::sin(angle);
		Matrix4 m = identity();
		m(0, 0) = c; m(0, 1) = -s;
		m(1, 0) = s; m(1, 1) = c;
		return m;
	}

	static Matrix4 rotationEuler(const Vector3& degrees) {
		return rotationY(radians(degrees.y)) *
		       rotationX(radians(degrees.x)) *
		       rotationZ(radians(degrees.z));
	}

	static constexpr Matrix4 basis(const Vector3& x, const Vector3& y, const Vector3& z) {
		return {{
			{ x.x, x.y, x.z, 0 },
			{ y.x, y.y, y.z, 0 },
			{ z.x, z.y, z.z, 0 },
			{ 0,   0,   0,   1 },
		}};
	}

	static Matrix4 lookAt(const Vector3& eye, const Vector3& target, const Vector3& world_up) {
		const Vector3 forward = normalize(target - eye);
		const Vector3 right = normalize(cross(forward, world_up));
		const Vector3 down = cross(forward, right);

		Matrix4 m = identity();
		m(0, 0) = right.x;   m(0, 1) = right.y;   m(0, 2) = right.z;   m(0, 3) = -dot(right, eye);
		m(1, 0) = down.x;    m(1, 1) = down.y;    m(1, 2) = down.z;    m(1, 3) = -dot(down, eye);
		m(2, 0) = forward.x; m(2, 1) = forward.y; m(2, 2) = forward.z; m(2, 3) = -dot(forward, eye);
		return m;
	}

	static constexpr Matrix4 orthographic(float left, float right, float top, float bottom,
	                                      float z_near, float z_far) {
		Matrix4 m = identity();
		m(0, 0) = 2.0f / (right - left);
		m(1, 1) = 2.0f / (bottom - top);
		m(2, 2) = 1.0f / (z_far - z_near);
		m(0, 3) = -(right + left) / (right - left);
		m(1, 3) = -(bottom + top) / (bottom - top);
		m(2, 3) = -z_near / (z_far - z_near);
		return m;
	}

	static Matrix4 perspective(float fov_y, float aspect, float z_near, float z_far) {
		const float t = std::tan(fov_y * 0.5f);

		Matrix4 m = {};
		m(0, 0) = 1.0f / (aspect * t);
		m(1, 1) = 1.0f / t;
		m(2, 2) = z_far / (z_far - z_near);
		m(2, 3) = -(z_far * z_near) / (z_far - z_near);
		m(3, 2) = 1.0f;
		return m;
	}
};

} // namespace math
