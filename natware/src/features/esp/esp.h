#pragma once
#include <imgui/imgui.h>
#include <sdk/math/math.h>

namespace rig { struct body_t; }

namespace esp
{
	void run();

	// Bones of an R6 or R15 body as lines with a dark shadow under them.
	void draw_skeleton(ImDrawList* draw, const rig::body_t& body, const math::matrix4& view, const math::vector2& dims, ImU32 color, float thickness);
}
