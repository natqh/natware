#include "fog.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>

#include <memory/memory.h>
#include <Offsets/Offsets.hpp>
#include <sdk/math/math.h>
#include <game/game.h>
#include <settings.h>

// Roblox ignores Lighting's FogStart/FogEnd/FogColor whenever Lighting holds an Atmosphere,
// and most games have one. So fog is set in both places: the Lighting values for games
// without an Atmosphere, and the Atmosphere's density and colour for games with one. The
// game's own values are kept and put back when fog is turned off.
namespace lighting
{
	namespace fog
	{
		namespace
		{
			struct lighting_saved_t
			{
				std::uint64_t address = 0;
				float start = 0.f, end = 0.f;
				math::vector3 color{};
			};

			struct atmosphere_saved_t
			{
				std::uint64_t address = 0;
				float density = 0.f, offset = 0.f, haze = 0.f, glare = 0.f;
				math::vector3 color{};
			};

			lighting_saved_t g_lighting;
			atmosphere_saved_t g_atmosphere;
			std::atomic<int> g_state{ 0 };   // 0 off/none, 1 Lighting fog, 2 Atmosphere

			void restore()
			{
				if (g_lighting.address)
				{
					memory->write<float>(g_lighting.address + Offsets::Lighting::FogStart, g_lighting.start);
					memory->write<float>(g_lighting.address + Offsets::Lighting::FogEnd, g_lighting.end);
					memory->write<math::vector3>(g_lighting.address + Offsets::Lighting::FogColor, g_lighting.color);
				}
				if (g_atmosphere.address)
				{
					memory->write<float>(g_atmosphere.address + Offsets::Atmosphere::Density, g_atmosphere.density);
					memory->write<float>(g_atmosphere.address + Offsets::Atmosphere::Offset, g_atmosphere.offset);
					memory->write<float>(g_atmosphere.address + Offsets::Atmosphere::Haze, g_atmosphere.haze);
					memory->write<float>(g_atmosphere.address + Offsets::Atmosphere::Glare, g_atmosphere.glare);
					memory->write<math::vector3>(g_atmosphere.address + Offsets::Atmosphere::Color, g_atmosphere.color);
				}
				g_lighting = {};
				g_atmosphere = {};
			}

			// Fog end in studs to an Atmosphere density: 150 studs is a thick wall of fog (1.0),
			// 500 about Roblox's default look (0.3), and 5000 almost clear.
			float density_for(float fog_end)
			{
				return std::clamp(150.0f / (std::max)(fog_end, 1.0f), 0.0f, 1.0f);
			}

			void fog_thread()
			{
				std::uint64_t last_datamodel = 0;
				while (true)
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(100));

					try
					{
						const std::uint64_t datamodel = game::datamodel.address;
						if (datamodel != last_datamodel)
						{
							// A new place: the saved values belonged to the old one.
							g_lighting = {};
							g_atmosphere = {};
							last_datamodel = datamodel;
						}
						if (datamodel == 0)
						{
							g_state = 0;
							continue;
						}

						if (!settings::lighting::fog::enabled)
						{
							if (g_lighting.address || g_atmosphere.address)
								restore();
							g_state = 0;
							continue;
						}

						rbx::instance_t lighting = game::datamodel.find_first_child_by_class("Lighting");
						if (lighting.address == 0)
						{
							g_state = 0;
							continue;
						}
						rbx::instance_t atmosphere = lighting.find_first_child_by_class("Atmosphere");

						if (g_lighting.address != lighting.address)
						{
							g_lighting.address = lighting.address;
							g_lighting.start = memory->read<float>(lighting.address + Offsets::Lighting::FogStart);
							g_lighting.end = memory->read<float>(lighting.address + Offsets::Lighting::FogEnd);
							g_lighting.color = memory->read<math::vector3>(lighting.address + Offsets::Lighting::FogColor);
						}
						if (atmosphere.address && g_atmosphere.address != atmosphere.address)
						{
							g_atmosphere.address = atmosphere.address;
							g_atmosphere.density = memory->read<float>(atmosphere.address + Offsets::Atmosphere::Density);
							g_atmosphere.offset = memory->read<float>(atmosphere.address + Offsets::Atmosphere::Offset);
							g_atmosphere.haze = memory->read<float>(atmosphere.address + Offsets::Atmosphere::Haze);
							g_atmosphere.glare = memory->read<float>(atmosphere.address + Offsets::Atmosphere::Glare);
							g_atmosphere.color = memory->read<math::vector3>(atmosphere.address + Offsets::Atmosphere::Color);
						}

						const float end = (std::max)(settings::lighting::fog::fog_end, 1.0f);
						const float start = std::clamp(settings::lighting::fog::fog_start, 0.0f, end - 0.5f);
						const math::vector3 color{ settings::lighting::fog::fog_r, settings::lighting::fog::fog_g, settings::lighting::fog::fog_b };

						memory->write<float>(lighting.address + Offsets::Lighting::FogStart, start);
						memory->write<float>(lighting.address + Offsets::Lighting::FogEnd, end);
						memory->write<math::vector3>(lighting.address + Offsets::Lighting::FogColor, color);

						if (atmosphere.address)
						{
							memory->write<float>(atmosphere.address + Offsets::Atmosphere::Density, density_for(end));
							memory->write<float>(atmosphere.address + Offsets::Atmosphere::Offset, 0.0f);
							memory->write<float>(atmosphere.address + Offsets::Atmosphere::Haze, 0.0f);
							memory->write<float>(atmosphere.address + Offsets::Atmosphere::Glare, 0.0f);
							memory->write<math::vector3>(atmosphere.address + Offsets::Atmosphere::Color, color);
						}
						g_state = atmosphere.address ? 2 : 1;
					}
					catch (...) {}
				}
			}
		}

		void run()
		{
			static bool initialized = false;
			if (!initialized)
			{
				std::thread(fog_thread).detach();
				initialized = true;
			}
		}

		const char* status()
		{
			if (!settings::lighting::fog::enabled)
				return "";
			switch (g_state.load())
			{
			case 1: return "Applied to Lighting";
			case 2: return "This game uses an Atmosphere: fog end sets its density";
			default: return "Waiting for the game's Lighting";
			}
		}
	}
}
