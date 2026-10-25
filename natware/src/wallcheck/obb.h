#pragma once
#include <windows.h>
#include <sdk/math/math.h>

namespace rbx
{
	// A part's box: its center, half size, and its local X/Y/Z axes in world space. A CFrame keeps
	// its basis vectors in the rotation's columns (RightVector, UpVector, -LookVector), the same
	// way `rotation * v` reads them everywhere else.
	struct obb final
	{
		math::vector3 center;
		math::vector3 half_size;
		math::vector3 axes[3];

		obb(const math::vector3& center, const math::vector3& size, const math::cframe& cf)
			: center(center), half_size(size * 0.5f)
		{
			for (int i = 0; i < 3; ++i)
				axes[i] = math::vector3{ cf.rotation.m[0][i], cf.rotation.m[1][i], cf.rotation.m[2][i] };
		}

		// True when p is inside the box (or on its surface).
		bool contains(const math::vector3& p) const
		{
			const math::vector3 d = p - center;
			for (int i = 0; i < 3; ++i)
				if (fabsf(axes[i].dot(d)) > half_size[i])
					return false;
			return true;
		}

		// The ray (origin, unit dir) against the box over [0, max_t]. On a hit, t_enter is where
		// the ray enters it (0 when it starts inside).
		bool raycast(const math::vector3& origin, const math::vector3& dir, float max_t, float& t_enter) const
		{
			float tmin = 0.0f;
			float tmax = max_t;
			const math::vector3 p = center - origin;

			for (int i = 0; i < 3; ++i)
			{
				const float e = axes[i].dot(p);
				const float f = axes[i].dot(dir);

				if (fabsf(f) > 1e-6f)
				{
					float t1 = (e + half_size[i]) / f;
					float t2 = (e - half_size[i]) / f;
					if (t1 > t2) std::swap(t1, t2);
					if (t1 > tmin) tmin = t1;
					if (t2 < tmax) tmax = t2;
					if (tmax < tmin)
						return false;
				}
				else if (-e - half_size[i] > 0.0f || -e + half_size[i] < 0.0f)
				{
					return false;
				}
			}

			t_enter = tmin;
			return true;
		}

		bool intersects(const math::vector3& origin, const math::vector3& dir, float max_t) const
		{
			float t = 0.0f;
			return raycast(origin, dir, max_t, t);
		}

		// Half extents of the world-aligned box around this one.
		math::vector3 aabb_half() const
		{
			math::vector3 r{};
			for (int j = 0; j < 3; ++j)
				r[j] = fabsf(axes[0][j]) * half_size.x + fabsf(axes[1][j]) * half_size.y + fabsf(axes[2][j]) * half_size.z;
			return r;
		}
	};
}
