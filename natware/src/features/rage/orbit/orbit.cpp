#include "orbit.h"
#include <menu/keybind/keybind.h>

#include <game/game.h>
#include <cache/cache.h>
#include <sdk/math/math.h>
#include <sdk/sdk.h>
#include <memory/memory.h>
#include <Offsets/Offsets.hpp>
#include <settings.h>
#include <features/aimbot/aimbot.h>
#include <features/silentaim/silentaim.h>

#include <windows.h>
#include <thread>
#include <chrono>
#include <mutex>
#include <cmath>
#include <random>

void rage::orbit::run()
{
	static float angle = 0.0f;
	const float TWO_PI = 6.28318530718f;
	static bool is_spectating = false;
	static std::uint64_t original_subject = 0;
	static std::random_device rd;
	static std::mt19937 gen(rd());
	static std::uniform_real_distribution<float> x_dist(-1.0f, 1.0f);
	static std::uniform_real_distribution<float> y_dist(-1.0f, 1.0f);

	keybind::keybind_t key_state{};
	while (true) {
		const bool orbit_on = keybind::feature_on(settings::movement::orbit::enabled,
			settings::movement::orbit::key, settings::movement::orbit::key_mode, key_state);
		if (!orbit_on || !game::datamodel.address) {
			if (is_spectating && game::camera != 0) {
				memory->write<std::uint64_t>(game::camera + Offsets::Camera::CameraSubject, original_subject);
				is_spectating = false;
				original_subject = 0;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
			continue;
		}

		cache::entity_t target{};
		{
			std::lock_guard<std::mutex> lock(cache::mtx);

			if (settings::movement::orbit::orbit_type == 1)
			{
				if (silentaim::state.target.instance.address != 0)
				{
					for (const auto& entity : cache::cached_players)
					{
						if (entity.instance.address == silentaim::state.target.instance.address)
						{
							target = entity;
							break;
						}
					}
				}
			}
			else
			{
				if (aimbot::sticky_target.instance.address != 0)
				{
					for (const auto& entity : cache::cached_players)
					{
						if (entity.instance.address == aimbot::sticky_target.instance.address)
						{
							target = entity;
							break;
						}
					}
				}
				else if (aimbot::player.instance.address != 0)
				{
					for (const auto& entity : cache::cached_players)
					{
						if (entity.instance.address == aimbot::player.instance.address)
						{
							target = entity;
							break;
						}
					}
				}
			}
		}

		if (target.instance.address == 0 || settings::rage::is_whitelisted(target.name)) {
			if (is_spectating && game::camera != 0) {
				memory->write<std::uint64_t>(game::camera + Offsets::Camera::CameraSubject, original_subject);
				is_spectating = false;
				original_subject = 0;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
			continue;
		}

		try {
			auto local_character = cache::cached_local_player.instance;
			if (!local_character.address) {
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
				continue;
			}

			auto local_root_it = cache::cached_local_player.parts.find("HumanoidRootPart");
			if (local_root_it == cache::cached_local_player.parts.end() || local_root_it->second.address == 0) {
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
				continue;
			}

			uintptr_t local_primitive = memory->read<uintptr_t>(local_root_it->second.address + Offsets::BasePart::Primitive);
			if (!local_primitive) {
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
				continue;
			}

			auto target_uppertorso_it = target.parts.find("UpperTorso");
			if (target_uppertorso_it == target.parts.end() || target_uppertorso_it->second.address == 0) {
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
				continue;
			}

			uintptr_t target_primitive = memory->read<uintptr_t>(target_uppertorso_it->second.address + Offsets::BasePart::Primitive);
			if (!target_primitive) {
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
				continue;
			}

			math::vector3 target_pos = memory->read<math::vector3>(target_primitive + Offsets::Primitive::Position);

			angle += settings::movement::orbit::speed * 0.05f;
			if (angle > TWO_PI) {
				angle -= TWO_PI;
			}

			math::vector3 new_local_pos;
			float base_x = settings::movement::orbit::radius * std::cosf(angle);
			float base_z = settings::movement::orbit::radius * std::sinf(angle);
			
			if (settings::movement::orbit::randomize)
			{
				float random_x_offset = x_dist(gen) * settings::movement::orbit::randomize_x;
				float random_y_offset = y_dist(gen) * settings::movement::orbit::randomize_y;
				base_x += random_x_offset;
				base_z += random_y_offset;
			}
			
			new_local_pos.x = target_pos.x + base_x;
			new_local_pos.y = target_pos.y + settings::movement::orbit::height_offset;
			new_local_pos.z = target_pos.z + base_z;

			math::matrix3 current_rotation = memory->read<math::matrix3>(local_primitive + Offsets::Primitive::Rotation);
			
			for (int i = 0; i < 10000; i++)
			{
				memory->write<math::vector3>(local_primitive + Offsets::Primitive::Position, new_local_pos);
				memory->write<math::matrix3>(local_primitive + Offsets::Primitive::Rotation, current_rotation);
				memory->write<math::vector3>(local_primitive + Offsets::Primitive::AssemblyLinearVelocity, math::vector3(0.0f, 0.0f, 0.0f));
				memory->write<math::vector3>(local_primitive + Offsets::Primitive::AssemblyAngularVelocity, math::vector3(0.0f, 0.0f, 0.0f));
			}

			if (settings::movement::orbit::spectate_target && game::camera != 0) {
				auto target_head_it = target.parts.find("Head");
				if (target_head_it != target.parts.end() && target_head_it->second.address != 0) {
					if (!is_spectating) {
						original_subject = memory->read<std::uint64_t>(game::camera + Offsets::Camera::CameraSubject);
						is_spectating = true;
					}
					memory->write<std::uint64_t>(game::camera + Offsets::Camera::CameraSubject, target_head_it->second.address);
				}
			}
			else {
				if (is_spectating && game::camera != 0) {
					memory->write<std::uint64_t>(game::camera + Offsets::Camera::CameraSubject, original_subject);
					is_spectating = false;
					original_subject = 0;
				}
			}

		}
		catch (...) {
		}
		std::this_thread::sleep_for(std::chrono::microseconds(100));
	}
}
