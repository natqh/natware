#pragma once
#include <cstdint>
#include <string>
#include <cache/cache.h>
#include <sdk/math/math.h>

// Who the aimbot aims at and where, for every mode (Mouse, Camera, Phantom Forces). Picking a
// target from scratch each tick made the aim stutter: two players at a similar distance traded
// places, the "closest" part flipped between head and torso as the crosshair came near, and the
// prediction lead shook with the game's frame-to-frame velocity. Here:
//  - the target is kept until it's gone, or (without sticky aim) another is clearly nearer the
//    crosshair; a target that ducks out of sight is waited on briefly before switching;
//  - the aimed part is kept the same way;
//  - the target's speed is measured from where its body was each time it actually moved, and
//    smoothed over about 80 ms, so the lead is steady.
namespace aim_target
{
	struct aim_t
	{
		cache::entity_t target;    // a copy of the player (or custom entity) aimed at
		std::string part;          // the part aimed at
		math::vector3 body{};      // where that part is now
		math::vector3 point{};     // ...plus the prediction lead: aim here
		math::vector2 screen{};    // `point` on screen
		float distance_px = 0.0f;  // `body` from the crosshair (or cursor)
		bool hold = false;         // it just teleported or respawned: don't move the aim yet
	};

	// One tick: false when there's nothing to aim at (or the current target is being waited on).
	// `origin` is where the aim is now on screen (game::aim_origin).
	bool update(const math::matrix4& view, const math::vector2& dims, const math::vector2& origin, aim_t& out);

	// The aim key was released: forget the target, its part and its motion.
	void reset();

	// What the last update decided, for the menu.
	std::string status();
}
