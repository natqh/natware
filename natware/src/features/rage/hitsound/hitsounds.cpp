#include "hitsounds.h"

#include <game/game.h>
#include <cache/cache.h>
#include <sdk/math/math.h>
#include <sdk/sdk.h>
#include <memory/memory.h>
#include <Offsets/Offsets.hpp>
#include <settings.h>
#include <features/silentaim/silentaim.h>
#include <features/aimbot/aimbot.h>
#include "../../../ext/hitsounds.h"
#include <windows.h>
#include <unordered_map>
#include <thread>
#include <chrono>
#include <mutex>
#include <string>
#include <cctype>
#include <algorithm>
#include <functional>
#pragma comment(lib, "winmm.lib")

static void play_hitsound()
{
	switch (settings::rage::hitsound_type)
	{
	case 0:
		PlaySoundA(reinterpret_cast<char*>(amongus), NULL, SND_ASYNC | SND_MEMORY);
		break;
	case 1:
		PlaySoundA(reinterpret_cast<char*>(skeet_sound), NULL, SND_ASYNC | SND_MEMORY);
		break;
	case 2:
		PlaySoundA(reinterpret_cast<char*>(beep), NULL, SND_ASYNC | SND_MEMORY);
		break;
	case 3:
		PlaySoundA(reinterpret_cast<char*>(bonk), NULL, SND_ASYNC | SND_MEMORY);
		break;
	case 4:
		PlaySoundA(reinterpret_cast<char*>(bubble), NULL, SND_ASYNC | SND_MEMORY);
		break;
	case 5:
		PlaySoundA(reinterpret_cast<char*>(cod), NULL, SND_ASYNC | SND_MEMORY);
		break;
	case 6:
		PlaySoundA(reinterpret_cast<char*>(csgo), NULL, SND_ASYNC | SND_MEMORY);
		break;
	case 7:
		PlaySoundA(reinterpret_cast<char*>(fairy), NULL, SND_ASYNC | SND_MEMORY);
		break;
	case 8:
		PlaySoundA(reinterpret_cast<char*>(fatality), NULL, SND_ASYNC | SND_MEMORY);
		break;
	case 9:
		PlaySoundA(reinterpret_cast<char*>(osu), NULL, SND_ASYNC | SND_MEMORY);
		break;
	case 10:
		PlaySoundA(reinterpret_cast<char*>(neverlose_sound), NULL, SND_ASYNC | SND_MEMORY);
		break;
	}
}

static void handle_hitsound_health_detection()
{
	static std::unordered_map<std::uint64_t, float> previous_healths;

	cache::entity_t target{};
	{
		std::lock_guard<std::mutex> lock(cache::mtx);

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

	if (target.instance.address == 0 || target.humanoid.address == 0)
	{
		return;
	}

	std::uint64_t entity_addr = target.instance.address;
	float current_health = target.health;

	auto it = previous_healths.find(entity_addr);
	if (it == previous_healths.end())
	{
		previous_healths[entity_addr] = current_health;
		return;
	}

	float previous_health = it->second;
	if (current_health < previous_health && (previous_health - current_health) > 0.1f)
	{
		play_hitsound();
	}

	previous_healths[entity_addr] = current_health;

	for (auto it = previous_healths.begin(); it != previous_healths.end();)
	{
		if (it->first != entity_addr)
		{
			it = previous_healths.erase(it);
		}
		else
		{
			++it;
		}
	}
}

static void handle_hitsound_click_detection()
{
	static bool last_click_state = false;

	if (!settings::globals::is_game_active)
	{
		last_click_state = false;
		return;
	}

	HWND rblxWnd = FindWindowA(nullptr, "Roblox");
	if (!rblxWnd || GetForegroundWindow() != rblxWnd)
	{
		last_click_state = false;
		return;
	}

	bool is_clicking = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

	if (is_clicking && !last_click_state)
	{
		play_hitsound();
	}

	last_click_state = is_clicking;
}

static void handle_hitsound_ammo_detection()
{
	static int last_ammo = -1;
	static std::uint64_t last_tool_addr = 0;

	std::string tool_name;
	{
		std::lock_guard<std::mutex> lock(cache::mtx);
		if (cache::cached_local_player.instance.address != 0)
		{
			rbx::player_t local_player(cache::cached_local_player.instance.address);
			rbx::model_instance_t model = local_player.get_model_instance();
			if (model.address != 0)
			{
				rbx::instance_t tool_instance = model.find_first_child_by_class("Tool");
				if (tool_instance.address != 0)
				{
					tool_name = tool_instance.get_name();
				}
			}
		}
	}

	if (tool_name.empty())
	{
		last_ammo = -1;
		last_tool_addr = 0;
		return;
	}

	try
	{
		rbx::player_t local_player(cache::cached_local_player.instance.address);
		rbx::instance_t character = local_player.get_model_instance();

		if (character.address == 0)
		{
			last_ammo = -1;
			last_tool_addr = 0;
			return;
		}

		rbx::instance_t tool = character.find_first_child(tool_name);

		if (tool.address == 0)
		{
			last_ammo = -1;
			last_tool_addr = 0;
			return;
		}

		if (tool.address != last_tool_addr)
		{
			last_tool_addr = tool.address;
			last_ammo = -1;
		}

		rbx::instance_t ammo_value(0);

		auto is_ammo_name = [](const std::string& name) -> bool
			{
				std::string lower_name = name;
				std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				return lower_name.find("ammo") != std::string::npos ||
					lower_name.find("mag") != std::string::npos ||
					lower_name.find("clip") != std::string::npos ||
					lower_name.find("bullet") != std::string::npos ||
					lower_name.find("round") != std::string::npos ||
					lower_name == "currentammo" ||
					lower_name == "ammoleft" ||
					lower_name == "ammocount";
			};

		std::function<rbx::instance_t(rbx::instance_t, int)> find_ammo_recursive;
		find_ammo_recursive = [&](rbx::instance_t parent, int depth) -> rbx::instance_t
			{
				if (depth > 3 || parent.address == 0) return rbx::instance_t(0);

				try
				{
					auto children = parent.get_children();

					for (const auto& child : children)
					{
						try
						{
							std::string class_name = child.get_class_name();
							if (class_name == "IntValue" || class_name == "NumberValue")
							{
								std::string name = child.get_name();
								if (is_ammo_name(name))
								{
									return child;
								}
							}
						}
						catch (...) {}
					}

					for (const auto& child : children)
					{
						try
						{
							std::string class_name = child.get_class_name();
							std::string name = child.get_name();

							if (class_name == "Script" || class_name == "LocalScript" ||
								class_name == "ModuleScript" || class_name == "Folder" ||
								class_name == "Configuration" || name == "Settings" ||
								name == "Config" || name == "Data")
							{
								rbx::instance_t result = find_ammo_recursive(child, depth + 1);
								if (result.address != 0) return result;
							}
						}
						catch (...) {}
					}
				}
				catch (...) {}

				return rbx::instance_t(0);
			};

		ammo_value = tool.find_first_child("Ammo");
		if (ammo_value.address == 0) ammo_value = tool.find_first_child("CurrentAmmo");
		if (ammo_value.address == 0) ammo_value = tool.find_first_child("AmmoLeft");
		if (ammo_value.address == 0) ammo_value = tool.find_first_child("Magazine");
		if (ammo_value.address == 0) ammo_value = tool.find_first_child("Mag");

		if (ammo_value.address == 0)
		{
			rbx::instance_t script = tool.find_first_child("Script");
			if (script.address != 0)
			{
				ammo_value = script.find_first_child("Ammo");
				if (ammo_value.address == 0) ammo_value = script.find_first_child("CurrentAmmo");
			}
		}

		if (ammo_value.address == 0)
		{
			rbx::instance_t local_script = tool.find_first_child("LocalScript");
			if (local_script.address != 0)
			{
				ammo_value = local_script.find_first_child("Ammo");
				if (ammo_value.address == 0) ammo_value = local_script.find_first_child("CurrentAmmo");
			}
		}

		if (ammo_value.address == 0)
		{
			const char* container_names[] = { "Settings", "Config", "Configuration", "Data", "Values" };
			for (const char* container_name : container_names)
			{
				rbx::instance_t container = tool.find_first_child(container_name);
				if (container.address != 0)
				{
					ammo_value = container.find_first_child("Ammo");
					if (ammo_value.address == 0) ammo_value = container.find_first_child("CurrentAmmo");
					if (ammo_value.address != 0) break;
				}
			}
		}

		if (ammo_value.address == 0)
		{
			ammo_value = find_ammo_recursive(tool, 0);
		}

		if (ammo_value.address == 0)
		{
			return;
		}

		try
		{
			std::string class_name = ammo_value.get_class_name();
			if (class_name != "IntValue" && class_name != "NumberValue")
			{
				return;
			}
		}
		catch (...)
		{
			return;
		}

		int current_ammo = memory->read<int>(ammo_value.address + 0xD0);

		if (last_ammo != -1 && current_ammo < last_ammo)
		{
			play_hitsound();
		}

		last_ammo = current_ammo;
	}
	catch (...)
	{
		last_ammo = -1;
		last_tool_addr = 0;
	}
}

void rage::hitsounds_detector_thread()
{
	using namespace std::chrono_literals;

	for (;;)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(10));

		if (!settings::rage::hitsounds || !game::datamodel.address)
		{
			std::this_thread::sleep_for(50ms);
			continue;
		}

		int method = settings::rage::hitsound_method;

		if (method == 0)
		{

			handle_hitsound_health_detection();
		}
		else if (method == 1)
		{

			if (settings::globals::is_game_active)
			{
				handle_hitsound_click_detection();
			}
		}
		else if (method == 2)
		{

			if (settings::globals::is_game_active)
			{
				handle_hitsound_ammo_detection();
			}
		}
	}
}