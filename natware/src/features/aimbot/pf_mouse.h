#pragma once
#include <string>
#include <sdk/math/math.h>

// Phantom Forces' Mouse aim mode. PF locks the mouse, turns its own camera from raw mouse input
// at its own sensitivity (with a separate aim-down-sights multiplier), and moves characters by
// CFrame, so the generic mouse mode got all of it wrong. This one:
//  - aims from the crosshair (the middle of the view), not the hidden cursor;
//  - sends one correction per frame the game draws, minus what earlier moves still have in
//    flight, so a correction is never doubled while the view catches up;
//  - learns how far the camera turns per mouse count by measuring the view after each move, kept
//    per field of view so hip fire and ADS each get their own;
namespace pf_mouse
{
	void reset();   // the key was released: drop the moves in flight

	// One tick of mouse steering toward `target` (screen px) from `origin` (the crosshair or
	// cursor). PF's mode uses it, and so does the Mouse mode in every other game.
	void steer_to(const math::vector2& target, const math::vector2& origin, const math::matrix4& view, const math::vector2& dims);

	std::string status();   // one line for the menu
}
