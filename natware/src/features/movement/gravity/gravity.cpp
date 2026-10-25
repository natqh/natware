#include "gravity.h"

#include <game/game.h>
#include <cache/cache.h>
#include <sdk/sdk.h>
#include <memory/memory.h>
#include <Offsets/Offsets.hpp>
#include <settings.h>

#include <thread>
#include <chrono>

void movement::gravity::run()
{
	static bool had_override = false;
	static float original_gravity = 0.0f;

	while (true)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(5));

		if (game::workspace.address == 0)
			continue;

		try
		{
			if (settings::movement::gravity::enabled)
			{
				if (!had_override)
				{
					float current = rbx::humanoid_t::read_gravity();
					if (current != 0.0f)
					{
						original_gravity = current;
						had_override = true;
					}
				}

				rbx::humanoid_t::write_gravity(settings::movement::gravity::value);
			}
			else
			{
				if (had_override)
				{
					rbx::humanoid_t::write_gravity(original_gravity);
					had_override = false;
				}
			}
		}
		catch (...) {}
	}
}

