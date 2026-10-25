#include "shadows.h"

#include <thread>
#include <chrono>
#include <memory/memory.h>
#include <Offsets/Offsets.hpp>
#include <game/game.h>
#include <settings.h>

namespace lighting
{
	namespace shadows
	{
		static void shadows_thread()
		{
			bool shadows_captured = false;
			float old_global_shadows = 0.f;

			while (true)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(200));

				try
				{
					if (game::datamodel.address == 0) continue;

					auto lighting = game::datamodel.find_first_child_by_class("Lighting");
					if (lighting.address == 0) continue;

					if (!settings::lighting::shadows::disable)
					{
						if (shadows_captured)
						{
							memory->write<float>(lighting.address + Offsets::Lighting::GlobalShadows, old_global_shadows);
							shadows_captured = false;
						}
						continue;
					}

					if (!shadows_captured)
					{
						old_global_shadows = memory->read<float>(lighting.address + Offsets::Lighting::GlobalShadows);
						shadows_captured = true;
					}

					memory->write<float>(lighting.address + Offsets::Lighting::GlobalShadows, 100000.0f);
				}
				catch (...) {}
			}
		}

		void run()
		{
			static bool initialized = false;
			if (!initialized)
			{
				std::thread(shadows_thread).detach();
				initialized = true;
			}
		}
	}
}
