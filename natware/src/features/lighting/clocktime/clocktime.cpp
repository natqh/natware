#include "clocktime.h"

#include <thread>
#include <chrono>
#include <cmath>
#include <vector>
#include <algorithm>

#include <memory/memory.h>
#include <Offsets/Offsets.hpp>
#include <sdk/sdk.h>
#include <sdk/math/math.h>
#include <game/game.h>
#include <settings.h>

namespace lighting
{
	namespace clocktime
	{
		struct color_key { float time; math::vector3 color; };

		static math::vector3 interpolate_color(float time, const std::vector<color_key>& keys)
		{
			if (keys.empty()) return { 1.f, 1.f, 1.f };
			if (time <= keys.front().time) return keys.front().color;
			if (time >= keys.back().time) return keys.back().color;
			for (size_t i = 0; i < keys.size() - 1; ++i)
			{
				if (time >= keys[i].time && time <= keys[i + 1].time)
				{
					float t = (time - keys[i].time) / (keys[i + 1].time - keys[i].time);
					float r = keys[i].color.x + t * (keys[i + 1].color.x - keys[i].color.x);
					float g = keys[i].color.y + t * (keys[i + 1].color.y - keys[i].color.y);
					float b = keys[i].color.z + t * (keys[i + 1].color.z - keys[i].color.z);
					return { r, g, b };
				}
			}
			return keys.back().color;
		}

		static void clocktime_thread()
		{
			static const std::vector<color_key> sky_ambient_keys = {
				{ 0.f, { 0.f, 0.f, 0.f } },
				{ (6.f - 3.f) * 3600.f, { 0.f, 0.f, 0.f } },
				{ (6.f - 2.f) * 3600.f, { 0.21f, 0.21f, 0.28f } },
				{ (6.f - 0.5f) * 3600.f, { 0.4f, 0.3f, 0.3f } },
				{ 6.f * 3600.f, { 0.3f, 0.2f, 0.3f } },
				{ 7.f * 3600.f, { 1.f, 1.f, 1.f } },
				{ 17.f * 3600.f, { 1.f, 1.f, 1.f } },
				{ 18.f * 3600.f, { 0.4f, 0.3f, 0.2f } },
				{ 18.33f * 3600.f, { 0.3f, 0.2f, 0.3f } },
				{ 20.f * 3600.f, { 0.3f, 0.2f, 0.3f } },
				{ 21.f * 3600.f, { 0.f, 0.f, 0.f } },
				{ 24.f * 3600.f, { 0.f, 0.f, 0.f } }
			};

			static const std::vector<color_key> light_color_keys = {
				{ 0.f, { 0.2f, 0.2f, 0.2f } },
				{ 5.f * 3600.f, { 0.1f, 0.1f, 0.1f } },
				{ 6.f * 3600.f, { 0.f, 0.f, 0.f } },
				{ 6.25f * 3600.f, { 0.6f, 0.6f, 0.f } },
				{ 7.f * 3600.f, { 0.75f, 0.75f, 0.75f } },
				{ 17.f * 3600.f, { 0.75f, 0.75f, 0.75f } },
				{ 17.5f * 3600.f, { 0.1f, 0.1f, 0.075f } },
				{ 18.f * 3600.f, { 0.1f, 0.05f, 0.05f } },
				{ 18.5f * 3600.f, { 0.1f, 0.1f, 0.1f } },
				{ 24.f * 3600.f, { 0.2f, 0.2f, 0.2f } }
			};

			float last_time_value = -1.0f;
			bool captured = false;
			math::vector3 old_direction, old_color, old_sun, old_moon, old_top, old_bottom;
			int old_source = 0;
			float old_clocktime = 0.f;

			const float pi = 3.1415927f;
			const float day = 86400.f;
			const float solar_year = 31558152.96f;
			const float half_solar_year = 182.6282f;
			const float earth_tilt = 0.4101523742186675f;
			const float moon_tilt = 0.089803443f;
			const float moon_phase_interval = 2551442.8f;

			while (true)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(50));

				try
				{
					if (game::datamodel.address == 0) continue;

					auto lighting = game::datamodel.find_first_child_by_class("Lighting");
					if (lighting.address == 0) continue;

					if (!settings::lighting::clocktime::enabled)
					{
						if (captured)
						{
							memory->write<math::vector3>(lighting.address + Offsets::Lighting::LightDirection, old_direction);
							memory->write<math::vector3>(lighting.address + Offsets::Lighting::LightColor, old_color);
							memory->write<math::vector3>(lighting.address + Offsets::Lighting::SunPosition, old_sun);
							memory->write<math::vector3>(lighting.address + Offsets::Lighting::MoonPosition, old_moon);
							memory->write<math::vector3>(lighting.address + Offsets::Lighting::GradientTop, old_top);
							memory->write<math::vector3>(lighting.address + Offsets::Lighting::GradientBottom, old_bottom);
							memory->write<int>(lighting.address + Offsets::Lighting::Source, old_source);
							memory->write<float>(lighting.address + Offsets::Lighting::ClockTime, old_clocktime);
							captured = false;
						}
						last_time_value = -1.0f;
						continue;
					}

					if (!captured)
					{
						old_direction = memory->read<math::vector3>(lighting.address + Offsets::Lighting::LightDirection);
						old_color = memory->read<math::vector3>(lighting.address + Offsets::Lighting::LightColor);
						old_sun = memory->read<math::vector3>(lighting.address + Offsets::Lighting::SunPosition);
						old_moon = memory->read<math::vector3>(lighting.address + Offsets::Lighting::MoonPosition);
						old_top = memory->read<math::vector3>(lighting.address + Offsets::Lighting::GradientTop);
						old_bottom = memory->read<math::vector3>(lighting.address + Offsets::Lighting::GradientBottom);
						old_source = memory->read<int>(lighting.address + Offsets::Lighting::Source);
						old_clocktime = memory->read<float>(lighting.address + Offsets::Lighting::ClockTime);
						captured = true;
					}

					float time_hours = settings::lighting::clocktime::clock_time;
					if (time_hours == last_time_value) continue;
					last_time_value = time_hours;

					float time_seconds = time_hours * 3600.0f;
					float geographic_lat_rad = (memory->read<float>(lighting.address + Offsets::Lighting::GeographicLatitude) * pi) / 180.0f;
					float diffuse_scale = memory->read<float>(lighting.address + Offsets::Lighting::EnvironmentDiffuseScale);

					float time_of_day = time_seconds - std::floorf(time_seconds / day) * day;
					float source_angle = (time_of_day * 2.0f * pi) / day;

					math::vector3 sun_pos(std::sinf(source_angle), -std::cosf(source_angle), 0.0f);
					math::vector3 moon_pos(std::sinf(source_angle + pi), -std::cosf(source_angle + pi), 0.0f);

					float day_of_year = (time_seconds - std::floorf(time_seconds / solar_year) * solar_year) / day;
					float sun_offset = -earth_tilt * std::cosf((day_of_year - half_solar_year) * pi / half_solar_year) - geographic_lat_rad;
					math::vector3 sun_axis = math::vector3(0, 0, 1).cross(sun_pos).normalized();
					math::matrix3 sun_rotation = math::matrix3_from_axis_angle(sun_axis, sun_offset);
					math::vector3 true_sun_position = sun_rotation * sun_pos;

					float moon_phase = std::floorf(time_seconds / moon_phase_interval) + 0.5f;
					float curr_moon_phase = moon_phase * 2.0f * pi;
					math::vector3 moon_pos_with_phase(std::sinf(curr_moon_phase + source_angle), -std::cosf(curr_moon_phase + source_angle), 0.0f);
					float moon_offset = ((-earth_tilt + moon_tilt) * std::sinf(moon_phase * 4.0f)) - geographic_lat_rad;
					math::vector3 moon_axis = math::vector3(0, 0, 1).cross(moon_pos_with_phase).normalized();
					math::matrix3 moon_rotation = math::matrix3_from_axis_angle(moon_axis, moon_offset);
					math::vector3 true_moon_position = moon_rotation * moon_pos_with_phase;

					int source = (true_sun_position.y > -0.3f) ? 0 : 1;
					math::vector3 light_direction = (source == 0) ? true_sun_position : true_moon_position;
					math::vector3 light_color = interpolate_color(time_of_day, light_color_keys);
					math::vector3 sky_ambient = interpolate_color(time_of_day, sky_ambient_keys);

					float min_diffuse = diffuse_scale * 0.35f;
					light_color.x = (std::max)(light_color.x, min_diffuse);
					light_color.y = (std::max)(light_color.y, min_diffuse);
					light_color.z = (std::max)(light_color.z, min_diffuse);

					memory->write<math::vector3>(lighting.address + Offsets::Lighting::LightDirection, light_direction);
					memory->write<math::vector3>(lighting.address + Offsets::Lighting::LightColor, light_color);
					memory->write<math::vector3>(lighting.address + Offsets::Lighting::SunPosition, true_sun_position);
					memory->write<math::vector3>(lighting.address + Offsets::Lighting::MoonPosition, true_moon_position);
					memory->write<math::vector3>(lighting.address + Offsets::Lighting::GradientTop, sky_ambient);
					memory->write<math::vector3>(lighting.address + Offsets::Lighting::GradientBottom, sky_ambient);
					memory->write<int>(lighting.address + Offsets::Lighting::Source, source);
					memory->write<float>(lighting.address + Offsets::Lighting::ClockTime, time_hours);
				}
				catch (...) {}
			}
		}

		void run()
		{
			static bool initialized = false;
			if (!initialized)
			{
				std::thread(clocktime_thread).detach();
				initialized = true;
			}
		}
	}
}
