#pragma once
#include <windows.h>
#include <sdk/sdk.h>

namespace game
{
	inline rbx::instance_t datamodel{};
	inline rbx::visualengine_t visengine{};
	inline rbx::instance_t workspace{};
	inline rbx::instance_t players{};
	inline rbx::instance_t local_player{};
	inline rbx::instance_t local_character{};
	inline std::uint64_t camera{};
	inline HWND roblox_window = nullptr;
	inline float window_offset_x = 0.0f;
	inline float window_offset_y = 0.0f;
	inline bool is_phantom_forces = false;
	inline bool is_murder_mystery_2 = false;
	inline bool is_lumber_tycoon_2 = false;

	HWND get_roblox_window();
	void update_window_offset();

	// Where you aim from, in screen pixels: the cursor, or the middle of Roblox's view when the
	// game has hidden and locked the cursor (first person, shift lock, Phantom Forces), which is
	// where the crosshair is.
	math::vector2 aim_origin(const math::vector2& dims);
}