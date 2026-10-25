#pragma once
#include <cache/cache.h>
#include <sdk/math/math.h>

namespace triggerbot
{
	// Which feature owns this triggerbot: 0 = aimbot, 1 = silentaim
	inline int active_source{ -1 };

	// Current target for rendering
	inline cache::entity_t current_target{};
	inline math::vector2 current_target_screen{};
	inline bool has_target{ false };

	void run_aimbot();
	void run_silentaim();
}
