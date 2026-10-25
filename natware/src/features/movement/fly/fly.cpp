#include "fly.h"

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
#include <mutex>

static void fly_hack()
{
	if (game::datamodel.address == 0)
		return;

	std::lock_guard<std::mutex> lock(cache::mtx);

	if (cache::cached_local_player.instance.address == 0)
		return;

	auto root_it = cache::cached_local_player.parts.find("HumanoidRootPart");
	if (root_it == cache::cached_local_player.parts.end() || root_it->second.address == 0)
		return;

	if (game::camera == 0)
		return;

	rbx::humanoid_t::write_gravity(0.0f);

	try
	{
		rbx::primitive_t prim = root_it->second.get_primitive();
		if (prim.address == 0)
			return;

		math::matrix3 rotation = memory->read<math::matrix3>(game::camera + Offsets::Camera::Rotation);

		math::vector3 move_direction(0.0f, 0.0f, 0.0f);
		bool is_moving = false;

		if (GetAsyncKeyState('W') & 0x8000)
		{
			move_direction.z -= 1.0f;
			is_moving = true;
		}
		if (GetAsyncKeyState('S') & 0x8000)
		{
			move_direction.z += 1.0f;
			is_moving = true;
		}
		if (GetAsyncKeyState('A') & 0x8000)
		{
			move_direction.x -= 1.0f;
			is_moving = true;
		}
		if (GetAsyncKeyState('D') & 0x8000)
		{
			move_direction.x += 1.0f;
			is_moving = true;
		}
		if (GetAsyncKeyState(VK_SPACE) & 0x8000)
		{
			move_direction.y += 1.0f;
			is_moving = true;
		}
		if (GetAsyncKeyState(VK_LCONTROL) & 0x8000)
		{
			move_direction.y -= 1.0f;
			is_moving = true;
		}

		if (!is_moving)
		{
			math::vector3 current_position = prim.get_position();

			if (settings::movement::flyhack::mode == 0)
			{
				if (game::datamodel.address != 0)
				{
					uint64_t workspace = memory->read<uint64_t>(game::datamodel.address + Offsets::DataModel::Workspace);
					if (workspace != 0)
					{
						uint64_t world = memory->read<uint64_t>(workspace + Offsets::Workspace::ReadOnlyGravity);
						if (world != 0)
						{
							memory->write<float>(world + Offsets::World::Gravity, 0.0f);
						}
					}
				}
				for (int i = 0; i < 10000; i++)
				{
					rbx::humanoid_t::write_gravity(0.0f);
					memory->write<math::vector3>(prim.address + Offsets::Primitive::Position, current_position);
					memory->write<math::vector3>(prim.address + Offsets::Primitive::AssemblyLinearVelocity, math::vector3(0.0f, 0.0f, 0.0f));
				}
			}
			else if (settings::movement::flyhack::mode == 1 || settings::movement::flyhack::mode == 2)
			{
				for (int i = 0; i < 10000; i++)
				{
					memory->write<math::vector3>(prim.address + Offsets::Primitive::Position, current_position);
					memory->write<math::vector3>(prim.address + Offsets::Primitive::AssemblyLinearVelocity, math::vector3(0.0f, 0.0f, 0.0f));
				}
			}
			return;
		}

		if (move_direction.length() > 0.0f)
		{
			move_direction = move_direction.normalized();
			math::vector3 rotated_direction = rotation * move_direction;
			math::vector3 current_position = prim.get_position();
			math::matrix3 current_rotation = prim.get_rotation();

			if (settings::movement::flyhack::mode == 0)
			{
				if (game::datamodel.address != 0)
				{
					uint64_t workspace = memory->read<uint64_t>(game::datamodel.address + Offsets::DataModel::Workspace);
					if (workspace != 0)
					{
						uint64_t world = memory->read<uint64_t>(workspace + Offsets::Workspace::ReadOnlyGravity);
						if (world != 0)
						{
							memory->write<float>(world + Offsets::World::Gravity, 0.0f);
						}
					}
				}
				math::vector3 new_velocity = rotated_direction * settings::movement::flyhack::speed;
				for (int i = 0; i < 10000; i++)
				{
					rbx::humanoid_t::write_gravity(0.0f);
					memory->write<math::vector3>(prim.address + Offsets::Primitive::AssemblyLinearVelocity, new_velocity);
				}
			}
			else if (settings::movement::flyhack::mode == 1)
			{
				math::vector3 new_position = current_position + (rotated_direction * (settings::movement::flyhack::speed / 165.0f));
				for (int i = 0; i < 10000; i++)
				{
					memory->write<math::vector3>(prim.address + Offsets::Primitive::Position, new_position);
					memory->write<math::vector3>(prim.address + Offsets::Primitive::AssemblyLinearVelocity, math::vector3(0.0f, 0.0f, 0.0f));
				}
			}
			else if (settings::movement::flyhack::mode == 2)
			{
				math::vector3 new_position = current_position + (rotated_direction * (settings::movement::flyhack::speed / 165.0f));
				for (int i = 0; i < 10000; i++)
				{
					memory->write<math::vector3>(prim.address + Offsets::Primitive::Position, new_position);
					memory->write<math::matrix3>(prim.address + Offsets::Primitive::Rotation, current_rotation);
					memory->write<math::vector3>(prim.address + Offsets::Primitive::AssemblyLinearVelocity, math::vector3(0.0f, 0.0f, 0.0f));
				}
			}
		}
	}
	catch (...)
	{
		return;
	}
}

void movement::fly::run()
{
	keybind::keybind_t flyhack_kb{};
	static bool flyhack_was_active = false;

	for (;;)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(2));

		flyhack_kb.key = settings::movement::flyhack::keybind;
		flyhack_kb.mode = static_cast<keybind::activation_mode>(settings::movement::flyhack::activation_mode);

		bool flyhack_is_active = settings::movement::flyhack::enabled && keybind::is_active(flyhack_kb);

		if (flyhack_is_active)
		{
			for (int i = 0; i < 25; i++)
			{
				rbx::humanoid_t::write_gravity(0.0f);
			}
			fly_hack();
			flyhack_was_active = true;
		}
		else if (flyhack_was_active)
		{
			rbx::humanoid_t::write_gravity(196.2f);
			flyhack_was_active = false;
		}
	}
}