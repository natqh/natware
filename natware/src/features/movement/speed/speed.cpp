#include "speed.h"

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

static void speed_hack()
{
	if (cache::cached_local_player.instance.address == 0)
		return;

	auto root_it = cache::cached_local_player.parts.find("HumanoidRootPart");
	if (root_it == cache::cached_local_player.parts.end() || root_it->second.address == 0)
		return;

	rbx::primitive_t prim = root_it->second.get_primitive();
	if (prim.address == 0)
		return;

	if (game::camera == 0)
		return;

	math::matrix3 rotation = memory->read<math::matrix3>(game::camera + Offsets::Camera::Rotation);

	math::vector3 move_direction(0.0f, 0.0f, 0.0f);
	if (GetAsyncKeyState('W') & 0x8000) move_direction.z -= 1.0f;
	if (GetAsyncKeyState('S') & 0x8000) move_direction.z += 1.0f;
	if (GetAsyncKeyState('A') & 0x8000) move_direction.x -= 1.0f;
	if (GetAsyncKeyState('D') & 0x8000) move_direction.x += 1.0f;

	if (move_direction.length() > 0.0f)
	{
		move_direction = move_direction.normalized();
		math::vector3 rotated_direction = rotation * move_direction;
		rotated_direction.y = 0.0f;
		rotated_direction = rotated_direction.normalized();

		math::vector3 current_velocity = memory->read<math::vector3>(prim.address + Offsets::Primitive::AssemblyLinearVelocity);

		if (settings::movement::speedhack::mode == 0)
		{
			math::vector3 new_velocity = rotated_direction * settings::movement::speedhack::speed;
			new_velocity.y = current_velocity.y;
			for (int i = 0; i < 10000; i++)
			{
				memory->write<math::vector3>(prim.address + Offsets::Primitive::AssemblyLinearVelocity, new_velocity);
			}
		}
		else if (settings::movement::speedhack::mode == 1)
		{
			if (cache::cached_local_player.humanoid.address != 0)
			{
				for (int i = 0; i < 25000; i++)
				{
					memory->write<float>(cache::cached_local_player.humanoid.address + Offsets::Humanoid::Walkspeed, settings::movement::speedhack::speed);
					memory->write<float>(cache::cached_local_player.humanoid.address + Offsets::Humanoid::WalkspeedCheck, settings::movement::speedhack::speed);
				}
			}
		}
	}
}

void movement::speed::run()
{
	keybind::keybind_t speedhack_kb{};
	bool speedhack_was_active = false;

	for (;;)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(1));

		speedhack_kb.key = settings::movement::speedhack::keybind;
		speedhack_kb.mode = static_cast<keybind::activation_mode>(settings::movement::speedhack::activation_mode);

		bool speedhack_is_active = settings::movement::speedhack::enabled && keybind::is_active(speedhack_kb);
		if (speedhack_is_active)
		{
			speed_hack();
			speedhack_was_active = true;
		}

		else if (speedhack_was_active && settings::movement::speedhack::mode == 1)
		{
			if (cache::cached_local_player.humanoid.address != 0)
			{
				for (int i = 0; i < 100; i++)
				{
					memory->write<float>(cache::cached_local_player.humanoid.address + Offsets::Humanoid::Walkspeed, 16.0f);
					memory->write<float>(cache::cached_local_player.humanoid.address + Offsets::Humanoid::WalkspeedCheck, 16.0f);
				}
			}
			speedhack_was_active = false;
		}
		else
		{
			speedhack_was_active = false;
		}
	}
}
