#pragma once
#include <cmath>
#include <intrin.h>
#include <algorithm>

namespace math
{
	struct vector2 final
	{
		float x{ 0.f }, y{ 0.f };
	};

	struct vector3 final
	{
		float x{ 0.f }, y{ 0.f }, z{ 0.f };

		float& operator[](int index)
		{
			return (&x)[index];
		}

		const float& operator[](int index) const
		{
			return (&x)[index];
		}

		float length() const
		{
			return std::sqrtf(x * x + y * y + z * z);
		}

		vector3 normalized() const
		{
			float len = length();
			if (len < 0.0001f)
				return { 0.f, 0.f, 0.f };
			float inv_len = 1.0f / len;
			return { x * inv_len, y * inv_len, z * inv_len };
		}

		float dot(const vector3& other) const
		{
			return x * other.x + y * other.y + z * other.z;
		}

		vector3 cross(const vector3& other) const
		{
			return {
				y * other.z - z * other.y,
				z * other.x - x * other.z,
				x * other.y - y * other.x
			};
		}

		float distance(const vector3& other) const
		{
			float dx = x - other.x;
			float dy = y - other.y;
			float dz = z - other.z;
			return std::sqrtf(dx * dx + dy * dy + dz * dz);
		}
	};

	inline vector3 operator+(const vector3& a, const vector3& b)
	{
		return { a.x + b.x, a.y + b.y, a.z + b.z };
	}

	inline vector3 operator-(const vector3& a, const vector3& b)
	{
		return { a.x - b.x, a.y - b.y, a.z - b.z };
	}

	inline vector3 operator*(const vector3& v, float scalar)
	{
		return { v.x * scalar, v.y * scalar, v.z * scalar };
	}

	inline vector3 operator*(float scalar, const vector3& v)
	{
		return { v.x * scalar, v.y * scalar, v.z * scalar };
	}

	struct vector4 final
	{
		float x{ 0.f }, y{ 0.f }, z{ 0.f }, w{ 0.f };
	};

	struct matrix3 final
	{
		float m[3][3];

		float* data()
		{
			return &m[0][0];
		}

		const float* data() const
		{
			return &m[0][0];
		}

		static matrix3 identity()
		{
			matrix3 result{};
			result.m[0][0] = 1.f; result.m[0][1] = 0.f; result.m[0][2] = 0.f;
			result.m[1][0] = 0.f; result.m[1][1] = 1.f; result.m[1][2] = 0.f;
			result.m[2][0] = 0.f; result.m[2][1] = 0.f; result.m[2][2] = 1.f;
			return result;
		}

		vector3 forward() const
		{
			return { -m[0][2], -m[1][2], -m[2][2] };
		}

		vector3 right() const
		{
			return { -m[0][0], -m[1][0], -m[2][0] };
		}

		vector3 up() const
		{
			return { m[0][1], m[1][1], m[2][1] };
		}
	};

	inline vector3 operator*(const matrix3& m, const vector3& v)
	{
		return
		{
			m.m[0][0] * v.x + m.m[0][1] * v.y + m.m[0][2] * v.z,
			m.m[1][0] * v.x + m.m[1][1] * v.y + m.m[1][2] * v.z,
			m.m[2][0] * v.x + m.m[2][1] * v.y + m.m[2][2] * v.z
		};
	}

	inline matrix3 matrix3_from_axis_angle(const vector3& axis, float angle)
	{
		float c = std::cosf(angle);
		float s = std::sinf(angle);
		float t = 1.0f - c;
		float x = axis.x, y = axis.y, z = axis.z;
		matrix3 result{};
		result.m[0][0] = t * x * x + c; result.m[0][1] = t * x * y - s * z; result.m[0][2] = t * x * z + s * y;
		result.m[1][0] = t * x * y + s * z; result.m[1][1] = t * y * y + c; result.m[1][2] = t * y * z - s * x;
		result.m[2][0] = t * x * z - s * y; result.m[2][1] = t * y * z + s * x; result.m[2][2] = t * z * z + c;
		return result;
	}

	struct matrix4 final
	{
		float m[4][4];

		float* data()
		{
			return &m[0][0];
		}

		const float* data() const
		{
			return &m[0][0];
		}

		vector4 multiply(const vector4& v) const
		{
			return {
				m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z + m[0][3] * v.w,
				m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z + m[1][3] * v.w,
				m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z + m[2][3] * v.w,
				m[3][0] * v.x + m[3][1] * v.y + m[3][2] * v.z + m[3][3] * v.w
			};
		}
	};

	struct cframe final
	{
		math::vector3 position;
		math::matrix3 rotation;

		cframe() = default;

		cframe(const math::vector3& pos, const math::matrix3& rot)
			: position(pos), rotation(rot)
		{
		}
	};
}