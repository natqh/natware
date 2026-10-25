#pragma once

#include <string>
#include <vector>
#include <sdk/math/math.h>

namespace lt2
{
	struct teleport_location
	{
		const char* name;
		math::vector3 position;
	};

	const std::vector<teleport_location>& get_teleport_locations();
	void teleport_to(const math::vector3& pos);
}
