#include "jump.h"

#include <game/game.h>
#include <cache/cache.h>
#include <sdk/math/math.h>
#include <sdk/sdk.h>
#include <memory/memory.h>
#include <Offsets/Offsets.hpp>
#include <settings.h>
#include <menu/keybind/keybind.h>

#include <windows.h>
#include <thread>
#include <chrono>

void movement::jump::run()
{
	keybind::keybind_t jumppower_kb{};
	bool was_active = false;
	float original_jump_power = 50.0f;
	bool captured_original = false;

	for (;;)
	{
		// Run faster when nojumpcooldown is on to beat the game's writes
		int sleep_ms = settings::movement::nojumpcooldown::enabled ? 1 : 10;
		std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));

		if (!settings::movement::jumphack::enabled && !settings::movement::nojumpcooldown::enabled && !was_active)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(40));
			continue;
		}

		// Find humanoid
		std::uint64_t humanoid_addr = 0;
		try
		{
			if (game::local_character.address != 0)
			{
				rbx::instance_t humanoid_inst = game::local_character.find_first_child("Humanoid");
				humanoid_addr = humanoid_inst.address;
			}
		}
		catch (...) {}

		if (humanoid_addr == 0)
			humanoid_addr = cache::cached_local_player.humanoid.address;

		if (humanoid_addr == 0)
			continue;

		// Jump power hack
		if (settings::movement::jumphack::enabled)
		{
			jumppower_kb.key = settings::movement::jumphack::keybind;
			jumppower_kb.mode = static_cast<keybind::activation_mode>(settings::movement::jumphack::activation_mode);

			bool active = keybind::is_active(jumppower_kb);

			if (active)
			{
				if (!captured_original)
				{
					original_jump_power = memory->read<float>(humanoid_addr + Offsets::Humanoid::JumpPower);
					captured_original = true;
				}

				memory->write<bool>(humanoid_addr + Offsets::Humanoid::UseJumpPower, true);
				memory->write<float>(humanoid_addr + Offsets::Humanoid::JumpPower, settings::movement::jumphack::value);
				was_active = true;
			}
			else if (was_active)
			{
				memory->write<float>(humanoid_addr + Offsets::Humanoid::JumpPower, original_jump_power);
				was_active = false;
				captured_original = false;
			}
		}
		else if (was_active)
		{
			memory->write<float>(humanoid_addr + Offsets::Humanoid::JumpPower, original_jump_power);
			was_active = false;
			captured_original = false;
		}

		// No jump cooldown: keep JumpPower at 50 every tick.
		// Da Hood sets JumpPower=0 via script after 3 jumps.
		// Internal scripts hook __newindex to intercept the write.
		// External: just keep overwriting back to 50 faster than the game.
		if (settings::movement::nojumpcooldown::enabled && !was_active)
		{
			try {
				memory->write<bool>(humanoid_addr + Offsets::Humanoid::UseJumpPower, true);
				memory->write<float>(humanoid_addr + Offsets::Humanoid::JumpPower, 50.0f);
			} catch (...) {}
		}
	}
}
