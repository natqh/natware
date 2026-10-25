#include "hittracers.h"

#include <game/game.h>
#include <cache/cache.h>
#include <sdk/math/math.h>
#include <sdk/sdk.h>
#include <memory/memory.h>
#include <Offsets/Offsets.hpp>
#include <settings.h>
#include <features/silentaim/silentaim.h>
#include <features/aimbot/aimbot.h>
#include <render/render.h>
#include <cache/bodyparts/bodyparts.h>
#include <unordered_map>
#include <thread>
#include <chrono>
#include <mutex>
#include <algorithm>
#include <cmath>
#include <string>
#include <cctype>
#include <Windows.h>
#include <functional>
#include "../../../ext/imgui/imgui.h"

static std::vector<rage::hit_tracer_data_t> g_hit_tracers;
static std::mutex g_hit_tracers_mtx;
static std::unordered_map<std::uint64_t, float> g_previous_healths;
static std::unordered_map<std::uint64_t, int> g_previous_ammo;

static math::vector3 get_camera_position()
{
	if (!game::datamodel.address)
		return math::vector3{};

	std::uint64_t workspace = memory->read<std::uint64_t>(game::datamodel.address + Offsets::DataModel::Workspace);
	if (workspace == 0)
		return math::vector3{};

	std::uint64_t camera = memory->read<std::uint64_t>(workspace + Offsets::Workspace::CurrentCamera);
	if (camera == 0)
		return math::vector3{};

	return memory->read<math::vector3>(camera + Offsets::Camera::Position);
}

static math::vector3 get_local_player_position()
{
	math::vector3 local_pos{};
	{
		std::lock_guard<std::mutex> lock(cache::mtx);
		if (cache::cached_local_player.instance.address != 0)
		{
			auto root_it = cache::cached_local_player.parts.find("HumanoidRootPart");
			if (root_it != cache::cached_local_player.parts.end() && root_it->second.address)
			{
				rbx::part_t part = root_it->second;
				rbx::primitive_t prim = part.get_primitive();
				if (prim.address)
				{
					local_pos = prim.get_position();
				}
			}
		}
	}
	return local_pos;
}

static bool try_get_beam_positions(math::vector3& start_pos, math::vector3& end_pos)
{
	try
	{
		rbx::instance_t gun_beam(0);

		if (cache::cached_local_player.instance.address != 0)
		{
			rbx::player_t local_player(cache::cached_local_player.instance.address);
			rbx::instance_t character = local_player.get_model_instance();

			if (character.address != 0)
			{
				gun_beam = character.find_first_child("GunBeam");
			}
		}

		if (gun_beam.address == 0 && game::workspace.address != 0)
		{
			gun_beam = game::workspace.find_first_child("GunBeam");
		}

		if (gun_beam.address == 0 && game::datamodel.address != 0)
		{
			rbx::instance_t replicated_storage = game::datamodel.find_first_child_by_class("ReplicatedStorage");
			if (replicated_storage.address != 0)
			{
				gun_beam = replicated_storage.find_first_child("GunBeam");
			}
		}

		if (gun_beam.address != 0)
		{
			rbx::instance_t attachment0 = gun_beam.find_first_child("Attachment0");
			rbx::instance_t attachment1 = gun_beam.find_first_child("Attachment1");

			if (attachment0.address != 0 && attachment1.address != 0)
			{
				std::uint64_t parent0 = memory->read<std::uint64_t>(attachment0.address + Offsets::Instance::Parent);
				std::uint64_t parent1 = memory->read<std::uint64_t>(attachment1.address + Offsets::Instance::Parent);

				if (parent0 != 0 && parent1 != 0)
				{
					rbx::part_t part0(parent0);
					rbx::part_t part1(parent1);

					rbx::primitive_t prim0 = part0.get_primitive();
					rbx::primitive_t prim1 = part1.get_primitive();

					if (prim0.address != 0 && prim1.address != 0)
					{
						start_pos = prim0.get_position();
						end_pos = prim1.get_position();
						return true;
					}
				}
			}
		}
	}
	catch (...) {}

	return false;
}

static bool try_get_target_point(math::vector3& end_pos)
{
	try
	{
		if (cache::cached_local_player.humanoid.address != 0)
		{
			end_pos = memory->read<math::vector3>(cache::cached_local_player.humanoid.address + Offsets::Humanoid::TargetPoint);
			return true;
		}
	}
	catch (...) {}

	return false;
}

static void add_tracer(const math::vector3& start, const math::vector3& end)
{
	rage::hit_tracer_data_t tracer_data;
	tracer_data.hit_position = end;
	tracer_data.source_position = start;
	tracer_data.timestamp = std::chrono::steady_clock::now();

	{
		std::lock_guard<std::mutex> lock(g_hit_tracers_mtx);
		g_hit_tracers.push_back(tracer_data);
	}
}

static void handle_health_detection()
{
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

	auto it = g_previous_healths.find(entity_addr);
	if (it == g_previous_healths.end())
	{
		g_previous_healths[entity_addr] = current_health;
		return;
	}

	float previous_health = it->second;
	if (current_health < previous_health && (previous_health - current_health) > 0.1f)
	{
		math::vector3 hit_position{};
		bool got_hit_position = false;

		if (silentaim::state.target.instance.address == entity_addr && silentaim::state.data_ready)
		{
			hit_position = silentaim::state.target_world_pos;
			got_hit_position = true;
		}
		else
		{
			if (bodyparts::get_part_position(target, "Head", hit_position))
			{
				got_hit_position = true;
			}
			else if (bodyparts::get_part_position(target, "HumanoidRootPart", hit_position))
			{
				got_hit_position = true;
			}
		}

		if (got_hit_position)
		{
			math::vector3 source_position = get_camera_position();
			if (source_position.x == 0.0f && source_position.y == 0.0f && source_position.z == 0.0f)
			{
				source_position = get_local_player_position();
			}

			add_tracer(source_position, hit_position);
		}
	}

	g_previous_healths[entity_addr] = current_health;

	for (auto it = g_previous_healths.begin(); it != g_previous_healths.end();)
	{
		if (it->first != entity_addr)
		{
			it = g_previous_healths.erase(it);
		}
		else
		{
			++it;
		}
	}
}

static void handle_click_detection()
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
		math::vector3 start_pos = {};
		math::vector3 end_pos = {};
		bool found_positions = try_get_beam_positions(start_pos, end_pos);

		if (!found_positions)
		{
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

			if (!tool_name.empty() && game::camera != 0)
			{
				bool found_tool_pos = false;

				try
				{
					rbx::player_t local_player_typed(cache::cached_local_player.instance.address);
					rbx::instance_t character = local_player_typed.get_model_instance();

					if (character.address != 0)
					{
						rbx::instance_t tool = character.find_first_child(tool_name);

						if (tool.address != 0)
						{
							rbx::instance_t handle = tool.find_first_child("Handle");
							if (handle.address == 0) handle = tool.find_first_child("Grip");
							if (handle.address == 0) handle = tool.find_first_child("GripPart");

							if (handle.address != 0)
							{
								rbx::part_t handle_part(handle.address);
								rbx::primitive_t handle_prim = handle_part.get_primitive();
								if (handle_prim.address != 0)
								{
									start_pos = handle_prim.get_position();
									found_tool_pos = true;
								}
							}
						}
					}
				}
				catch (...) {}

				if (!found_tool_pos)
				{
					start_pos = get_camera_position();
				}

				if (try_get_target_point(end_pos))
				{
					found_positions = true;
				}
			}
		}

		if (found_positions)
		{
			add_tracer(start_pos, end_pos);
		}
	}

	last_click_state = is_clicking;
}

static void handle_ammo_detection()
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
			math::vector3 start_pos = {};
			math::vector3 end_pos = {};
			bool found_positions = try_get_beam_positions(start_pos, end_pos);

			if (!found_positions)
			{
				rbx::instance_t handle = tool.find_first_child("Handle");
				if (handle.address == 0) handle = tool.find_first_child("Grip");
				if (handle.address == 0) handle = tool.find_first_child("GripPart");

				if (handle.address != 0)
				{
					rbx::part_t handle_part(handle.address);
					rbx::primitive_t handle_prim = handle_part.get_primitive();
					if (handle_prim.address != 0)
					{
						start_pos = handle_prim.get_position();
					}
				}
				else if (game::camera != 0)
				{
					start_pos = get_camera_position();
				}

				if (try_get_target_point(end_pos))
				{
					found_positions = true;
				}
			}

			if (found_positions)
			{
				add_tracer(start_pos, end_pos);
			}
		}

		last_ammo = current_ammo;
	}
	catch (...)
	{
		last_ammo = -1;
		last_tool_addr = 0;
	}
}

void rage::hittracers_detector_thread()
{
	using namespace std::chrono_literals;

	for (;;)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(10));

		if (!settings::rage::hit_tracers || !game::datamodel.address)
		{
			std::this_thread::sleep_for(50ms);
			continue;
		}

		int method = settings::visuals::hit_tracers_method;

		if (method == 0)
		{

			handle_health_detection();
		}
		else if (method == 1)
		{

			if (settings::globals::is_game_active)
			{
				handle_click_detection();
			}
		}
		else if (method == 2)
		{

			if (settings::globals::is_game_active)
			{
				handle_ammo_detection();
			}
		}
	}
}

void rage::draw_hit_tracers()
{
	if (!settings::rage::hit_tracers || !game::visengine.address)
		return;

	const math::matrix4 view = game::visengine.get_viewmatrix();
	const math::vector2 dims = game::visengine.get_dimensions();
	const float duration = settings::rage::hit_tracers_duration;

	auto now = std::chrono::steady_clock::now();

	std::lock_guard<std::mutex> lock(g_hit_tracers_mtx);

	for (auto it = g_hit_tracers.begin(); it != g_hit_tracers.end();)
	{
		float elapsed = static_cast<float>(std::chrono::duration_cast<std::chrono::milliseconds>(now - it->timestamp).count()) / 1000.0f;

		if (elapsed > duration)
		{
			it = g_hit_tracers.erase(it);
			continue;
		}

		math::vector2 source_screen{};
		math::vector2 hit_screen{};

		bool source_visible = game::visengine.world_to_screen(it->source_position, source_screen, dims, view);
		bool hit_visible = game::visengine.world_to_screen(it->hit_position, hit_screen, dims, view);

		if (source_visible && hit_visible)
		{
			float alpha = 1.0f - (elapsed / duration);
			alpha = (std::max)(0.0f, (std::min)(1.0f, alpha));

			ImDrawList* draw = ImGui::GetBackgroundDrawList();
			ImU32 tracer_color = IM_COL32(
				static_cast<int>(settings::rage::hit_tracers_color[0] * 255.f * alpha),
				static_cast<int>(settings::rage::hit_tracers_color[1] * 255.f * alpha),
				static_cast<int>(settings::rage::hit_tracers_color[2] * 255.f * alpha),
				static_cast<int>(settings::rage::hit_tracers_color[3] * 255.f * alpha)
			);

			ImVec2 source_pos(source_screen.x, source_screen.y);
			ImVec2 hit_pos(hit_screen.x, hit_screen.y);

			draw->AddLine(source_pos, hit_pos, tracer_color, 1.5f);

			float rotation_speed = 5.0f;
			float rotation_angle = elapsed * rotation_speed;
			float crosshair_size = 8.0f;
			float line_thickness = 2.0f;

			float cos_angle = std::cosf(rotation_angle);
			float sin_angle = std::sinf(rotation_angle);

			ImVec2 crosshair_points[4];
			crosshair_points[0] = ImVec2(hit_pos.x - crosshair_size * cos_angle, hit_pos.y - crosshair_size * sin_angle);
			crosshair_points[1] = ImVec2(hit_pos.x + crosshair_size * cos_angle, hit_pos.y + crosshair_size * sin_angle);
			crosshair_points[2] = ImVec2(hit_pos.x + crosshair_size * sin_angle, hit_pos.y - crosshair_size * cos_angle);
			crosshair_points[3] = ImVec2(hit_pos.x - crosshair_size * sin_angle, hit_pos.y + crosshair_size * cos_angle);

			draw->AddLine(crosshair_points[0], crosshair_points[1], tracer_color, line_thickness);
			draw->AddLine(crosshair_points[2], crosshair_points[3], tracer_color, line_thickness);
		}

		++it;
	}
}