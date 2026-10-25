#include "spin360.h"

#include <game/game.h>
#include <cache/cache.h>
#include <sdk/sdk.h>
#include <memory/memory.h>
#include <Offsets/Offsets.hpp>
#include <settings.h>
#include <sdk/math/math.h>

#include <windows.h>
#include <thread>
#include <chrono>
#include <mutex>
#include <cmath>

void rage::spin360::run()
{
	const float TWO_PI = 6.28318530718f;
	float current_angle = 0.0f;
	auto last_time = std::chrono::steady_clock::now();
	bool was_enabled = false;

	bool toggle_state = false;
	bool was_pressed = false;

	while (true)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(5));

		// keybind check
		bool key_active = true;
		int kb = settings::rage::spin360::keybind;
		int kb_mode = settings::rage::spin360::activation_mode;
		if (kb != 0)
		{
			bool is_down = (GetAsyncKeyState(kb) & 0x8000) != 0;
			if (kb_mode == 1) { // Hold
				key_active = is_down;
			} else if (kb_mode == 0) { // Toggle
				if (is_down && !was_pressed) toggle_state = !toggle_state;
				was_pressed = is_down;
				key_active = toggle_state;
			} else { // Always
				key_active = true;
			}
		}

		if (!settings::rage::spin360::enabled || !key_active || !game::datamodel.address)
		{
			// Restore AutoRotate when disabled
			if (was_enabled)
			{
				try
				{
					cache::entity_t lp;
					{
						std::lock_guard<std::mutex> lock(cache::mtx);
						lp = cache::cached_local_player;
					}
					if (lp.humanoid.address != 0)
						memory->write<bool>(lp.humanoid.address + Offsets::Humanoid::AutoRotate, true);
				}
				catch (...) {}
				was_enabled = false;
			}
			current_angle = 0.0f;
			last_time = std::chrono::steady_clock::now();
			continue;
		}

		cache::entity_t local_player;
		{
			std::lock_guard<std::mutex> lock(cache::mtx);
			local_player = cache::cached_local_player;
		}

		if (local_player.humanoid.address == 0)
			continue;

		auto hrp_it = local_player.parts.find("HumanoidRootPart");
		if (hrp_it == local_player.parts.end() || !hrp_it->second.address)
			continue;

		rbx::primitive_t prim = hrp_it->second.get_primitive();
		if (prim.address == 0)
			continue;

		try
		{
			// Disable AutoRotate so Roblox doesn't fight our rotation
			// (this is what makes it work in shiftlock/aiming)
			memory->write<bool>(local_player.humanoid.address + Offsets::Humanoid::AutoRotate, false);
			was_enabled = true;

			// Smooth angle increment based on elapsed time
			auto now = std::chrono::steady_clock::now();
			float elapsed_sec = std::chrono::duration<float>(now - last_time).count();
			last_time = now;

			if (elapsed_sec <= 0.0f || elapsed_sec > 0.1f)
				elapsed_sec = 0.005f; // clamp to avoid jumps

			current_angle += settings::rage::spin360::speed * TWO_PI * elapsed_sec;
			if (current_angle >= TWO_PI)
				current_angle -= TWO_PI;

			// Read current rotation, extract pitch, apply our yaw while keeping pitch
			math::matrix3 cur_rot = memory->read<math::matrix3>(prim.address + Offsets::Primitive::Rotation);

			// Build yaw-only rotation matrix
			float c = std::cosf(current_angle);
			float s = std::sinf(current_angle);

			// Keep vertical (Y) axis from current rotation, replace horizontal
			math::matrix3 spin{};
			spin.m[0][0] =  c;    spin.m[0][1] = 0.0f; spin.m[0][2] = s;
			spin.m[1][0] =  0.0f; spin.m[1][1] = 1.0f; spin.m[1][2] = 0.0f;
			spin.m[2][0] = -s;    spin.m[2][1] = 0.0f; spin.m[2][2] = c;

			memory->write<math::matrix3>(prim.address + Offsets::Primitive::Rotation, spin);
		}
		catch (...) {}
	}
}