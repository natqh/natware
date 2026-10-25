#include "tickrate.h"

#include <sdk/sdk.h>
#include <memory/memory.h>
#include <settings.h>

#include <thread>
#include <chrono>

namespace movement
{
	namespace tickrate
	{
		static constexpr float DEFAULT_TICKRATE = 240.0f;
		static bool was_enabled = false;

		static void tickrate_thread()
		{
			using namespace std::chrono_literals;

			for (;;)
			{
				std::this_thread::sleep_for(50ms);

				if (memory->get_module_address() == 0)
					continue;

				bool is_enabled = settings::movement::tickrate::enabled;
				if (is_enabled)
				{
					float tickrate_value = settings::movement::tickrate::value;
					if (tickrate_value < 0.0f)
						tickrate_value = 0.0f;
					if (tickrate_value > 1000.0f)
						tickrate_value = 1000.0f;

					rbx::humanoid_t::write_tickrate(tickrate_value);
					was_enabled = true;
				}
				else if (was_enabled)
				{
					rbx::humanoid_t::write_tickrate(DEFAULT_TICKRATE);
					was_enabled = false;
				}
			}
		}

		void run()
		{
			std::thread(tickrate_thread).detach();
		}
	}
}
