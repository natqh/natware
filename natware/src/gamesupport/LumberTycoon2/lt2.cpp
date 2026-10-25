#include "lt2.h"

#include <memory/memory.h>
#include <Offsets/Offsets.hpp>
#include <sdk/sdk.h>
#include <cache/cache.h>

namespace lt2
{
	// =====================================================================
	// Teleport Locations
	// =====================================================================
	static const std::vector<teleport_location> s_locations = {
		{ "Spawn",              { 155.f,    5.f,    74.f  } },
		{ "Wood Dropoff",       { 315.f,    3.f,    85.f  } },
		{ "Wood R Us",          { 316.f,    5.f,    59.f  } },
		{ "Land Store",         { 258.f,    7.f,    99.f  } },
		{ "Fancy Furnishings",  { 491.f,    5.f, -1720.f  } },
		{ "Link's Logic",       { 4607.f,   9.f,  -798.f  } },
		{ "Bob's Shack",        { 260.f,   12.f, -2542.f  } },
		{ "Shrine of Sight",    { -1585.f, 207.f,  919.f  } },
		{ "Swamp",              { -1209.f, 135.f, -801.f  } },
		{ "Cave Entrance",      { 3581.f, -177.f,  430.f  } },
		{ "Volcano",            { -1585.f, 624.f, 1140.f  } },
		{ "End Times",          { 113.f,  -212.f, -951.f  } },
		{ "Ski Lodge",          { 1244.f,   64.f, 2306.f  } },
		{ "Safari",             { -1200.f, 138.f, -265.f  } },
		{ "Bridge",             { 80.f,     12.f,   12.f  } },
	};

	// =====================================================================
	// Teleport
	// =====================================================================
	const std::vector<teleport_location>& get_teleport_locations()
	{
		return s_locations;
	}

	void teleport_to(const math::vector3& pos)
	{
		auto ri = cache::cached_local_player.parts.find("HumanoidRootPart");
		if (ri == cache::cached_local_player.parts.end() || ri->second.address == 0)
			return;

		uintptr_t local_prim = memory->read<uintptr_t>(ri->second.address + Offsets::BasePart::Primitive);
		if (local_prim == 0)
			return;

		math::vector3 target = pos;
		target.y += 5.0f;

		for (int i = 0; i < 10000; i++)
		{
			memory->write<math::vector3>(local_prim + Offsets::Primitive::Position, target);
			memory->write<math::vector3>(local_prim + Offsets::Primitive::AssemblyLinearVelocity, math::vector3{0, 0, 0});
		}
	}
}
