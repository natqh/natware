#include "hitbox_expander.h"

#include <game/game.h>
#include <cache/cache.h>
#include <sdk/sdk.h>
#include <memory/memory.h>
#include <Offsets/Offsets.hpp>
#include <settings.h>
#include <menu/keybind/keybind.h>

#include <atomic>
#include <cmath>
#include <thread>
#include <chrono>
#include <mutex>
#include <cstdio>

// The cache thread reads Da Hood's BodyEffects["K.O"] with the rest of the player.
static bool is_player_knocked(const cache::entity_t& player)
{
	return player.knocked;
}

struct saved_primitive_t
{
	std::uint64_t prim_addr;
	math::vector3 original_size;
	std::uint8_t original_flags;
};

static std::vector<saved_primitive_t> expanded_parts;
static std::atomic<int> g_expanded_count{ 0 };
static std::atomic<int> g_reset_count{ 0 };   // sizes the game put back since the last pass

// The part that gets bigger: 0 Head, 1 HumanoidRootPart (invisible, the default), 2 Torso.
static rbx::part_t target_part_of(const cache::entity_t& entity)
{
	static const char* const head[] = { "Head" };
	static const char* const root[] = { "HumanoidRootPart" };
	static const char* const torso[] = { "UpperTorso", "Torso" };
	const int which = settings::rage::hitbox_expander::target_part;
	const auto pick = [&](auto& names) -> rbx::part_t
	{
		for (const char* name : names)
		{
			auto it = entity.parts.find(name);
			if (it != entity.parts.end() && it->second.address != 0)
				return it->second;
		}
		return rbx::part_t{};
	};
	if (which == 0) return pick(head);
	if (which == 2) return pick(torso);
	return pick(root);
}

static void restore_all()
{
	for (const auto& s : expanded_parts)
	{
		try
		{
			memory->write<math::vector3>(s.prim_addr + Offsets::Primitive::Size, s.original_size);
			memory->write<std::uint8_t>(s.prim_addr + Offsets::Primitive::Flags, s.original_flags);
		}
		catch (...) {}
	}
	expanded_parts.clear();
	g_expanded_count = 0;
}

void rage::hitbox_expander::run()
{
	bool was_enabled = false;
	keybind::keybind_t key_state{};

	while (true)
	{
		const bool on = keybind::feature_on(settings::rage::hitbox_expander::enabled,
			settings::rage::hitbox_expander::key, settings::rage::hitbox_expander::key_mode, key_state);
		try
		{
			if (!game::datamodel.address)
			{
				if (was_enabled)
				{
					restore_all();
					was_enabled = false;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				continue;
			}

			if (!on)
			{
				if (was_enabled)
				{
					restore_all();
					was_enabled = false;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				continue;
			}

			was_enabled = true;

			if (settings::rage::hitbox_expander::size_x <= 0.0f ||
				settings::rage::hitbox_expander::size_y <= 0.0f ||
				settings::rage::hitbox_expander::size_z <= 0.0f)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				continue;
			}

			std::vector<cache::entity_t> snapshot;
			std::uint64_t local_address = 0;
			std::string local_name;
			std::uint64_t local_target_address = 0;
			std::uint64_t local_primitive_address = 0;
			{
				std::lock_guard<std::mutex> lock(cache::mtx);
				snapshot = cache::cached_players;
				local_address = cache::cached_local_player.instance.address;
				local_name = cache::cached_local_player.name;

				auto local_part_it = cache::cached_local_player.parts.find("HumanoidRootPart");
				if (local_part_it != cache::cached_local_player.parts.end() && local_part_it->second.address != 0)
				{
					local_target_address = local_part_it->second.address;
					local_primitive_address = memory->read<std::uint64_t>(local_target_address + Offsets::BasePart::Primitive);
				}
			}

			// build set of current player primitive addresses for tracking
			std::vector<saved_primitive_t> new_expanded;
			int resets_this_pass = 0;

			for (auto& entity : snapshot)
			{
				if (!entity.instance.address)
					continue;

				if ((local_address != 0 && entity.instance.address == local_address) ||
					(!local_name.empty() && entity.name == local_name))
					continue;

				if (settings::rage::hitbox_expander::knock_check && is_player_knocked(entity))
					continue;

				rbx::part_t target_part = target_part_of(entity);
				if (target_part.address == 0)
					continue;

				if (local_target_address != 0 && target_part.address == local_target_address)
					continue;

				std::uint64_t primitive_address = memory->read<std::uint64_t>(target_part.address + Offsets::BasePart::Primitive);
				if (!primitive_address)
					continue;

				if (local_primitive_address != 0 && primitive_address == local_primitive_address)
					continue;

				try
				{
					// check if we already have this primitive saved
					bool already_saved = false;
					for (const auto& s : expanded_parts)
					{
						if (s.prim_addr == primitive_address)
						{
							already_saved = true;
							new_expanded.push_back(s);
							break;
						}
					}

					if (!already_saved)
					{
						// save original size and flags before expanding
						math::vector3 orig_size = memory->read<math::vector3>(primitive_address + Offsets::Primitive::Size);
						std::uint8_t orig_flags = memory->read<std::uint8_t>(primitive_address + Offsets::Primitive::Flags);
						new_expanded.push_back({ primitive_address, orig_size, orig_flags });
					}

					math::vector3 new_size = {
						settings::rage::hitbox_expander::size_x,
						settings::rage::hitbox_expander::size_y,
						settings::rage::hitbox_expander::size_z
					};

					// Did our last write stick? If the game keeps putting the size back, say so.
					if (already_saved)
					{
						const math::vector3 now_size = memory->read<math::vector3>(primitive_address + Offsets::Primitive::Size);
						if (std::fabs(now_size.x - new_size.x) > 0.01f || std::fabs(now_size.y - new_size.y) > 0.01f || std::fabs(now_size.z - new_size.z) > 0.01f)
							++resets_this_pass;
					}

					memory->write<math::vector3>(primitive_address + Offsets::Primitive::Size, new_size);

					// No collisions (you'd bump into it), but raycasts must hit it: many games
					// turn CanQuery off on the root part so bullets only hit limbs, which left
					// an expanded root unhittable.
					std::uint8_t primitive_flags = memory->read<std::uint8_t>(primitive_address + Offsets::Primitive::Flags);
					primitive_flags &= ~static_cast<std::uint8_t>(Offsets::PrimitiveFlags::CanCollide);
					primitive_flags |= static_cast<std::uint8_t>(Offsets::PrimitiveFlags::CanQuery);
					memory->write<std::uint8_t>(primitive_address + Offsets::Primitive::Flags, primitive_flags);
				}
				catch (...)
				{
					continue;
				}
			}

			// restore any primitives that are no longer in current player list
			for (const auto& old : expanded_parts)
			{
				bool still_active = false;
				for (const auto& cur : new_expanded)
				{
					if (cur.prim_addr == old.prim_addr)
					{
						still_active = true;
						break;
					}
				}
				if (!still_active)
				{
					try
					{
						memory->write<math::vector3>(old.prim_addr + Offsets::Primitive::Size, old.original_size);
						memory->write<std::uint8_t>(old.prim_addr + Offsets::Primitive::Flags, old.original_flags);
					}
					catch (...) {}
				}
			}

			expanded_parts = new_expanded;
			g_expanded_count = static_cast<int>(expanded_parts.size());
			g_reset_count = resets_this_pass;

			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		catch (const std::exception& e)
		{
			(void)e;
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
	}
}

std::string rage::hitbox_expander::status()
{
	if (!settings::rage::hitbox_expander::enabled)
		return "";
	char buf[160];
	if (g_reset_count > 0)
		snprintf(buf, sizeof(buf), "Expanded on %d players, but the game keeps resetting %d of them", g_expanded_count.load(), g_reset_count.load());
	else
		snprintf(buf, sizeof(buf), "Expanded on %d players (sizes holding)", g_expanded_count.load());
	return buf;
}
