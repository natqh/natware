#pragma once
#include <cstdint>
#include <cache/cache.h>
#include <sdk/math/math.h>

// Keeps the aim from following things a real player can't do. When a target's position jumps
// faster than anyone moves (a teleport, a respawn, or a read of freed memory for a frame), that
// target is left alone for a moment and picked up again where it landed. Positions that make no
// sense (NaN, zero, kilometres away) are refused outright.
namespace aim_guard
{
	// The target's root (HumanoidRootPart, torso or head), or false if it can't be read sanely.
	bool root_of(const cache::entity_t& entity, math::vector3& out);

	// True when it's safe to aim at `entity` this tick; feed it the root from root_of.
	bool steady(std::uint64_t entity, const math::vector3& root);

	// A world point is believable to aim at: finite, not the origin, near the camera.
	bool plausible(const math::vector3& point, const math::vector3& camera);
}
