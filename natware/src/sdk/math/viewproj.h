#pragma once
#include <cmath>
#include "math.h"

// Reading the camera back out of the visual engine's view-projection matrix (rows multiply column
// vectors, clip = M * (p, 1), as in world_to_screen).
namespace math
{
	// The camera is the one point where clip x, y and w are all zero: rows 0, 1 and 3 of the
	// matrix, solved by Cramer's rule.
	inline bool camera_from_viewproj(const matrix4& vp, vector3& out)
	{
		const vector3 r0{ vp.m[0][0], vp.m[0][1], vp.m[0][2] };
		const vector3 r1{ vp.m[1][0], vp.m[1][1], vp.m[1][2] };
		const vector3 r3{ vp.m[3][0], vp.m[3][1], vp.m[3][2] };
		const vector3 c0 = r1.cross(r3), c1 = r3.cross(r0), c2 = r0.cross(r1);
		const float det = r0.dot(c0);
		if (!std::isfinite(det) || std::fabs(det) < 1e-9f)
			return false;
		out = (c0 * -vp.m[0][3] + c1 * -vp.m[1][3] + c2 * -vp.m[3][3]) * (1.0f / det);
		return std::isfinite(out.x) && std::isfinite(out.y) && std::isfinite(out.z);
	}

	// The ray from the camera through a point given in normalised device coordinates (-1..1,
	// y up). Every point on it has clip.x = ndc_x * w and clip.y = ndc_y * w, so it runs along
	// the line both planes share; w grows in front of the camera.
	inline bool ray_from_viewproj(const matrix4& vp, float ndc_x, float ndc_y, vector3& origin, vector3& dir)
	{
		if (!camera_from_viewproj(vp, origin))
			return false;
		const vector3 r0{ vp.m[0][0], vp.m[0][1], vp.m[0][2] };
		const vector3 r1{ vp.m[1][0], vp.m[1][1], vp.m[1][2] };
		const vector3 r3{ vp.m[3][0], vp.m[3][1], vp.m[3][2] };
		dir = (r0 - r3 * ndc_x).cross(r1 - r3 * ndc_y).normalized();
		if (r3.dot(dir) < 0.f)
			dir = dir * -1.f;
		return dir.length() > 0.5f;
	}

	// Where a world point lands in normalised device coordinates; false behind the camera.
	inline bool ndc_of(const matrix4& vp, const vector3& p, float& ndc_x, float& ndc_y)
	{
		const vector4 c = vp.multiply({ p.x, p.y, p.z, 1.f });
		if (c.w < 0.1f)
			return false;
		ndc_x = c.x / c.w;
		ndc_y = c.y / c.w;
		return true;
	}
}
