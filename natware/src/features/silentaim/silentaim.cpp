#include "silentaim.h"

#include <thread>
#include <chrono>
#include <algorithm>
#include <cctype>
#include <random>
#include <windows.h>
#include <game/game.h>
#include <memory/memory.h>
#include <Offsets/Offsets.hpp>
#include <settings.h>
#include <menu/keybind/keybind.h>
#include <cache/bodyparts/bodyparts.h>
#include <cache/cache.h>
#include <cache/custom_entities/custom_entities.h>
#include <features/aimbot/aimbot.h>
#include <wallcheck/wallcheck.h>
#include <sdk/math/viewproj.h>
#include <features/aimbot/aim_guard.h>
#include <mutex>

static std::vector<const char*> PART_NAMES = { "Head", "HumanoidRootPart", "LeftArm", "RightArm", "LeftLeg", "RightLeg" };
static constexpr int PART_COUNT = 6;

static rbx::vector2int16 make_viewport_from_target(const math::vector2& screenSize, const math::vector2& targetScreen)
{
	int vx = static_cast<int>(std::lround(2.0f * targetScreen.x));
	int vy = static_cast<int>(std::lround(2.0f * targetScreen.y));

	vx = std::clamp(vx, 100, static_cast<int>(INT16_MAX));
	vy = std::clamp(vy, 100, static_cast<int>(INT16_MAX));

	return rbx::vector2int16{
		static_cast<int16_t>(vx),
		static_cast<int16_t>(vy)
	};
}

static float get_distance_from_center(const math::vector2& point, const math::vector2& position)
{
	float dx{ position.x - point.x };
	float dy{ position.y - point.y };
	return std::sqrtf(dx * dx + dy * dy);
}

static math::vector3 get_velocity(cache::entity_t& entity)
{
	auto root_it = entity.parts.find("HumanoidRootPart");
	if (root_it == entity.parts.end() || !root_it->second.address)
		return math::vector3{};

	rbx::primitive_t prim = root_it->second.get_primitive();
	if (!prim.address)
		return math::vector3{};

	return memory->read<math::vector3>(prim.address + Offsets::Primitive::AssemblyLinearVelocity);
}

// Prediction without flicks: a velocity spike (a respawn, a teleport, the frame a jump lands)
// mustn't throw the aim studs away. Vertical speed is capped hardest, and the lead itself is
// kept within a body's reach.
static math::vector3 bounded_lead(math::vector3 velocity, float factor_x, float factor_y)
{
	if (!std::isfinite(velocity.x) || !std::isfinite(velocity.y) || !std::isfinite(velocity.z) || velocity.length() > 150.0f)
		return {};
	velocity.y = std::clamp(velocity.y, -16.0f, 24.0f);
	math::vector3 lead{ velocity.x * factor_x, velocity.y * factor_y, velocity.z * factor_x };
	const float flat = std::sqrt(lead.x * lead.x + lead.z * lead.z);
	if (flat > 8.0f)
	{
		lead.x *= 8.0f / flat;
		lead.z *= 8.0f / flat;
	}
	lead.y = std::clamp(lead.y, -2.5f, 3.0f);
	return lead;
}

static math::vector3 apply_prediction(cache::entity_t& entity, const math::vector3& position)
{
	const float factor_x = (20.0f - settings::silentaim::prediction_x) * 0.02f;
	const float factor_y = (20.0f - settings::silentaim::prediction_y) * 0.02f;
	return position + bounded_lead(get_velocity(entity), factor_x, factor_y);
}
// What silent aim is doing, one line per thread, for the menu: which step it got to.
namespace
{
	std::mutex g_status_mtx;
	std::string g_target_status, g_apply_status;

	void set_target_status(std::string text)
	{
		std::lock_guard<std::mutex> lock(g_status_mtx);
		g_target_status = std::move(text);
	}

	void set_apply_status(std::string text)
	{
		std::lock_guard<std::mutex> lock(g_status_mtx);
		g_apply_status = std::move(text);
	}
}

std::string silentaim::status()
{
	if (!settings::silentaim::enabled)
		return "";
	std::lock_guard<std::mutex> lock(g_status_mtx);
	if (g_apply_status.empty())
		return g_target_status;
	return g_target_status + "  \xC2\xB7  " + g_apply_status;
}

// The FOV circle counts whenever it's on or drawn: if you can see the circle, silent aim keeps
// to it.
static bool fov_on()
{
	return settings::silentaim::use_fov || settings::silentaim::draw_fov;
}

static bool is_silent_aim_active()
{
	if (!game::datamodel.address || !game::visengine.address || !settings::silentaim::enabled)
		return false;

	if (settings::silentaim::guncheck)
	{
		if (!silentaim::state.aim_indicator.address)
			return false;

		bool is_visible = memory->read<bool>(silentaim::state.aim_indicator.address + Offsets::GuiObject::Visible);
		if (!is_visible)
			return false;
	}

	if (settings::silentaim::use_aimbot_target)
	{
		if (aimbot::sticky_target.instance.address != 0 || aimbot::player.instance.address != 0)
			return true;
	}

	// No key bound: on whenever Roblox is in front (it used to mean never).
	if (settings::silentaim::keybind == 0)
		return GetForegroundWindow() == game::get_roblox_window();

	static keybind::keybind_t silentaim_kb{};
	silentaim_kb.key = settings::silentaim::keybind;
	silentaim_kb.mode = static_cast<keybind::activation_mode>(settings::silentaim::activation_mode);
	return keybind::is_active(silentaim_kb);
}

// The cache thread reads Da Hood's BodyEffects["K.O"] with the rest of the player.
static bool is_player_knocked(const cache::entity_t& player)
{
	return player.knocked;
}

static bool is_custom_entity_knocked(const settings::custom_entities::custom_entity_t& custom_entity)
{
	if (custom_entity.instance.address == 0)
		return false;

	rbx::instance_t instance(custom_entity.instance.address);
	std::string instance_class = instance.get_class_name();

	rbx::model_instance_t model_instance{ 0 };
	if (instance_class == "Model")
	{
		model_instance = rbx::model_instance_t(custom_entity.instance.address);
	}
	else
	{
		rbx::instance_t model_child = instance.find_first_child_by_class("Model");
		if (model_child.address == 0)
			return false;
		model_instance = rbx::model_instance_t(model_child.address);
	}

	if (model_instance.address == 0)
		return false;

	rbx::instance_t body_effects = model_instance.find_first_child("BodyEffects");
	if (body_effects.address == 0)
	{
		std::vector<rbx::instance_t> children = model_instance.get_children();
		for (const auto& child : children)
		{
			if (child.get_name() == "BodyEffects")
			{
				body_effects = child;
				break;
			}
		}
		if (body_effects.address == 0)
			return false;
	}

	rbx::instance_t ko = body_effects.find_first_child("K.O");
	if (ko.address == 0)
		return false;

	std::string ko_class = ko.get_class_name();
	if (ko_class != "BoolValue")
		return false;

	bool value = false;
	try {
		value = memory->read<bool>(ko.address + Offsets::Misc::Value);
	}
	catch (...) {
		value = false;
	}
	return value;
}

static bool is_valid_target(const cache::entity_t& entity, const cache::entity_t& local_player, bool ignore_friendly)
{
	if (entity.instance.address == local_player.instance.address)
		return false;

	if (settings::silentaim::teamcheck && entity.team != 0 && entity.team == local_player.team)
		return false;

	if (settings::rage::is_whitelisted(entity.name))
		return false;

	// Target filter: if a player is targeted, skip everyone else
	if (!settings::rage::playerlist::target_name.empty() && entity.name != settings::rage::playerlist::target_name)
		return false;

	if (settings::silentaim::knock_check && is_player_knocked(entity))
		return false;

	if (settings::silentaim::health_check_enabled && entity.health < settings::silentaim::min_health)
		return false;

	if (settings::visuals::max_distance_enabled)
	{
		auto get_pos = [](const cache::entity_t& e, math::vector3& out) -> bool {
			auto it = e.parts.find("HumanoidRootPart");
			if (it == e.parts.end() || !it->second.address) it = e.parts.find("Head");
			if (it == e.parts.end() || !it->second.address) return false;
			rbx::primitive_t prim = it->second.get_primitive();
			if (!prim.address) return false;
			out = prim.get_position();
			return true;
		};
		math::vector3 target_pos{}, local_pos{};
		if (get_pos(entity, target_pos) && get_pos(local_player, local_pos))
		{
			math::vector3 diff = { target_pos.x - local_pos.x, target_pos.y - local_pos.y, target_pos.z - local_pos.z };
			float dist = std::sqrtf(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);
			if (dist > settings::visuals::max_distance)
				return false;
		}
	}

	(void)ignore_friendly;   // there is no friend list to check against

	// Behind a wall: only when the head and the torso are both hidden from the camera.
	if (settings::silentaim::wallcheck)
	{
		math::vector3 camera{};
		if (math::camera_from_viewproj(game::visengine.get_viewmatrix(), camera))
		{
			bool any = false, seen = false;
			for (const char* name : { "Head", "UpperTorso", "Torso", "HumanoidRootPart" })
			{
				auto it = entity.parts.find(name);
				if (it == entity.parts.end() || !it->second.address)
					continue;
				rbx::primitive_t prim = it->second.get_primitive();
				if (!prim.address)
					continue;
				any = true;
				if (wallcheck->is_visible(camera, prim.get_position()))
				{
					seen = true;
					break;
				}
			}
			if (any && !seen)
				return false;
		}
	}
	return true;
}

static bool get_target_position(const cache::entity_t& entity, const math::matrix4& view, const math::vector2& dims, const math::vector2& mouse_pos, math::vector3& out_pos)
{
	if (settings::silentaim::target_part == 0)
	{
		float best_distance_squared = FLT_MAX;
		math::vector3 best_pos;
		bool found = false;

		for (const auto& part_pair : entity.parts)
		{
			const std::string& part_name = part_pair.first;
			rbx::part_t part = part_pair.second;

			if (!part.address)
				continue;

			rbx::primitive_t prim = part.get_primitive();
			if (!prim.address)
				continue;

			math::vector3 part_pos = prim.get_position();
			math::vector3 part_size = prim.get_size();
			math::matrix3 part_rot = prim.get_rotation();

			if (part_size.x == 0.f && part_size.y == 0.f && part_size.z == 0.f)
				continue;

			const int num_samples_per_edge = 4;   // 96 points a part, not 600
			const float step = 1.0f / static_cast<float>(num_samples_per_edge - 1);
			math::vector3 half_size_local = { part_size.x * 0.5f, part_size.y * 0.5f, part_size.z * 0.5f };

			struct face_definition_t
			{
				int normal_axis_idx;
				float offset_sign;
				int tangent1_axis_idx;
				int tangent2_axis_idx;
			};

			const face_definition_t faces[6] = {
				{ 0,  1.0f, 1, 2 },
				{ 0, -1.0f, 1, 2 },
				{ 1,  1.0f, 0, 2 },
				{ 1, -1.0f, 0, 2 },
				{ 2,  1.0f, 0, 1 },
				{ 2, -1.0f, 0, 1 }
			};

			for (const auto& face : faces)
			{
				int n_idx = face.normal_axis_idx;
				int t1_idx = face.tangent1_axis_idx;
				int t2_idx = face.tangent2_axis_idx;
				float offset = face.offset_sign * half_size_local[n_idx];

				for (int i = 0; i < num_samples_per_edge; ++i)
				{
					for (int j = 0; j < num_samples_per_edge; ++j)
					{
						float u = i * step;
						float v = j * step;

						math::vector3 local_point{};
						local_point[n_idx] = offset;

						float t1_local = u * part_size[t1_idx] - half_size_local[t1_idx];
						float t2_local = v * part_size[t2_idx] - half_size_local[t2_idx];
						local_point[t1_idx] = t1_local;
						local_point[t2_idx] = t2_local;

						math::vector3 rotated_offset = part_rot * local_point;
						math::vector3 world_point = part_pos + rotated_offset;

						math::vector2 screen_point;
						if (game::visengine.world_to_screen(world_point, screen_point, dims, view))
						{
							math::vector2 client_screen_point = screen_point;
							client_screen_point.x -= game::window_offset_x;
							client_screen_point.y -= game::window_offset_y;
							if (client_screen_point.x > 0.0f && client_screen_point.y > 0.0f)
							{
								float dx = mouse_pos.x - client_screen_point.x;
								float dy = mouse_pos.y - client_screen_point.y;
								float distance_squared = dx * dx + dy * dy;

								if (distance_squared < best_distance_squared)
								{
									best_distance_squared = distance_squared;
									best_pos = world_point;
									found = true;
								}
							}
						}
					}
				}
			}
		}

		if (!found)
			return false;

		out_pos = best_pos;
	}
	else if (settings::silentaim::target_part == 1)
	{
		float best_distance = FLT_MAX;
		math::vector3 best_pos;
		bool found = false;

		for (int i = 0; i < PART_COUNT; ++i)
		{
			math::vector3 pos;
			if (bodyparts::get_part_position(entity, PART_NAMES[i], pos))
			{
				math::vector2 screen_pos;
				if (game::visengine.world_to_screen(pos, screen_pos, dims, view))
				{
					math::vector2 client_screen_pos = screen_pos;
					client_screen_pos.x -= game::window_offset_x;
					client_screen_pos.y -= game::window_offset_y;
					float distance = get_distance_from_center(mouse_pos, client_screen_pos);
					if (distance < best_distance)
					{
						best_distance = distance;
						best_pos = pos;
						found = true;
					}
				}
			}
		}

		if (!found)
			return false;

		out_pos = best_pos;
	}
	else if (settings::silentaim::target_part >= 2 && settings::silentaim::target_part <= 7)
	{
		if (!bodyparts::get_part_position(entity, PART_NAMES[settings::silentaim::target_part - 2], out_pos))
			return false;
	}

	if (settings::silentaim::enable_prediction)
		out_pos = apply_prediction(const_cast<cache::entity_t&>(entity), out_pos);

	return true;
}

static cache::entity_t get_closest_to_mouse()
{
	if (!game::visengine.address || !game::datamodel.address)
		return cache::entity_t{};

	const bool ignore_friendly = (settings::silentaim::priorities & (1 << 0)) != 0;
	const bool prioritise_hostile = (settings::silentaim::priorities & (1 << 1)) != 0;

	const math::matrix4 view = game::visengine.get_viewmatrix();

	HWND roblox_window = game::get_roblox_window();
	math::vector2 dims = game::visengine.get_dimensions();
	if (roblox_window)
	{
		RECT client_rect{};
		if (GetClientRect(roblox_window, &client_rect))
		{
			dims.x = static_cast<float>(client_rect.right - client_rect.left);
			dims.y = static_cast<float>(client_rect.bottom - client_rect.top);
		}
	}

	// Where you're aiming, in Roblox client coordinates: the crosshair when the game has locked
	// the mouse (first person, shift lock, Phantom Forces), otherwise the cursor. The hidden OS
	// cursor sits anywhere in those games, which is why targets got picked around the wrong spot.
	const math::vector2 aim_screen = game::aim_origin(game::visengine.get_dimensions());
	const math::vector2 mouse_pos{ aim_screen.x - game::window_offset_x, aim_screen.y - game::window_offset_y };

	std::vector<cache::entity_t> entities_snapshot;
	cache::entity_t local_player_snapshot;
	{
		std::lock_guard<std::mutex> lock(cache::mtx);
		entities_snapshot = cache::cached_players;
		local_player_snapshot = cache::cached_local_player;
	}

	cache::entity_t best_target{};
	float best_distance = FLT_MAX;
	cache::entity_t best_hostile_target{};
	float best_hostile_distance = FLT_MAX;

	for (const auto& entity : entities_snapshot)
	{
		if (settings::silentaim::knock_check && is_player_knocked(entity))
			continue;

		if (!is_valid_target(entity, local_player_snapshot, ignore_friendly))
			continue;

		math::vector3 target_pos;
		if (!get_target_position(entity, view, dims, mouse_pos, target_pos))
			continue;

		math::vector2 screen_pos{};
		if (!game::visengine.world_to_screen(target_pos, screen_pos, dims, view))
			continue;

		math::vector2 client_screen_pos = screen_pos;
		client_screen_pos.x -= game::window_offset_x;
		client_screen_pos.y -= game::window_offset_y;

		const float distance = get_distance_from_center(mouse_pos, client_screen_pos);
		if (fov_on() && distance > settings::silentaim::fov)
			continue;

		if (prioritise_hostile)
		{
			if (distance < best_hostile_distance)
			{
				best_hostile_distance = distance;
				best_hostile_target = entity;
			}
		}
		else if (distance < best_distance)
		{
			best_distance = distance;
			best_target = entity;
		}
	}

	std::vector<settings::custom_entities::custom_container_t> containers_snapshot;
	{
		std::lock_guard<std::mutex> lock(custom_entities::containers_mtx);
		containers_snapshot = settings::custom_entities::containers;
	}

	for (const auto& container : containers_snapshot)
	{
		if (!container.enabled) continue;

		for (const auto& custom_entity : container.entities)
		{
			if (!custom_entity.enabled) continue;
			if (!custom_entity.instance.address) continue;

			if (settings::silentaim::knock_check && is_custom_entity_knocked(custom_entity))
				continue;

			if (custom_entity.root_part.position.x == 0 && custom_entity.root_part.position.y == 0 && custom_entity.root_part.position.z == 0)
				continue;

			std::vector<math::vector3> target_parts;

			if (custom_entity.head.part.address)
			{
				math::vector3 pos = custom_entity.head.position;
				if (pos.x != 0 || pos.y != 0 || pos.z != 0)
					target_parts.push_back(pos);
			}

			if (custom_entity.root_part.part.address)
			{
				math::vector3 pos = custom_entity.root_part.position;
				if (pos.x != 0 || pos.y != 0 || pos.z != 0)
					target_parts.push_back(pos);
			}

			if (target_parts.empty())
				continue;

			int hitbox_index = 0;
			if (settings::silentaim::target_part >= 1 && settings::silentaim::target_part <= 6)
			{
				hitbox_index = settings::silentaim::target_part - 1;
				if (hitbox_index == 0 && target_parts.size() > 0) hitbox_index = 0;
				else if (hitbox_index == 1 && target_parts.size() > 1) hitbox_index = 1;
				else hitbox_index = 0;
			}
			else
			{
				float best_part_distance = FLT_MAX;
				int best_part_index = 0;
				for (size_t i = 0; i < target_parts.size(); ++i)
				{
					math::vector2 screen_pos;
					if (game::visengine.world_to_screen(target_parts[i], screen_pos, dims, view))
					{
						math::vector2 client_screen_pos = screen_pos;
						client_screen_pos.x -= game::window_offset_x;
						client_screen_pos.y -= game::window_offset_y;
						float distance = get_distance_from_center(mouse_pos, client_screen_pos);
						if (distance < best_part_distance)
						{
							best_part_distance = distance;
							best_part_index = static_cast<int>(i);
						}
					}
				}
				hitbox_index = best_part_index;
			}

			if (hitbox_index >= static_cast<int>(target_parts.size()))
				hitbox_index = 0;

			math::vector3 target_pos = target_parts[hitbox_index];

			if (isnan(target_pos.x) || isnan(target_pos.y) || isnan(target_pos.z) ||
				(target_pos.x == 0 && target_pos.y == 0 && target_pos.z == 0))
				continue;

			if (settings::silentaim::enable_prediction)
			{
				cache::entity_t temp_entity;
				temp_entity.instance = custom_entity.instance;
				temp_entity.parts.clear();
				for (const auto& part_pair : custom_entity.parts)
				{
					if (part_pair.second.part.address != 0)
					{
						try
						{
							rbx::part_t part_copy = part_pair.second.part;
							rbx::primitive_t test_prim = part_copy.get_primitive();
							if (test_prim.address != 0)
							{
								temp_entity.parts[part_pair.first] = part_pair.second.part;
							}
						}
						catch (...)
						{
							continue;
						}
					}
				}
				target_pos = apply_prediction(temp_entity, target_pos);
			}

			math::vector2 screen_pos;
			if (!game::visengine.world_to_screen(target_pos, screen_pos, dims, view))
				continue;

			math::vector2 client_screen_pos = screen_pos;
			client_screen_pos.x -= game::window_offset_x;
			client_screen_pos.y -= game::window_offset_y;

			const float distance = get_distance_from_center(mouse_pos, client_screen_pos);
			if (fov_on() && distance > settings::silentaim::fov)
				continue;

			cache::entity_t custom_entity_wrapper;
			custom_entity_wrapper.instance = custom_entity.instance;
			custom_entity_wrapper.name = custom_entity.name;
			custom_entity_wrapper.display_name = custom_entity.name;
			custom_entity_wrapper.humanoid = rbx::humanoid_t{ 0 };
			custom_entity_wrapper.rig_type = 0;
			custom_entity_wrapper.team = 0;
			custom_entity_wrapper.health = 0.0f;
			custom_entity_wrapper.max_health = 0.0f;
			custom_entity_wrapper.knocked = false;

			custom_entity_wrapper.parts.clear();
			for (const auto& part_pair : custom_entity.parts)
			{
				if (part_pair.second.part.address != 0)
				{
					try
					{
						rbx::part_t part_copy = part_pair.second.part;
						rbx::primitive_t test_prim = part_copy.get_primitive();
						if (test_prim.address != 0)
						{
							custom_entity_wrapper.parts[part_pair.first] = part_pair.second.part;
						}
					}
					catch (...)
					{
						continue;
					}
				}
			}

			if (prioritise_hostile)
			{
				if (distance < best_hostile_distance)
				{
					best_hostile_distance = distance;
					best_hostile_target = custom_entity_wrapper;
				}
			}
			else if (distance < best_distance)
			{
				best_distance = distance;
				best_target = custom_entity_wrapper;
			}
		}
	}

	return (prioritise_hostile && best_hostile_target.instance.address) ? best_hostile_target : best_target;
}

static void targeting_thread()
{
	using namespace std::chrono_literals;

	static std::int32_t aim_indicator_check_counter = 0;

	for (;;)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(10));

		if (!game::datamodel.address || !game::visengine.address)
		{
			std::this_thread::sleep_for(100ms);
			continue;
		}

		if ((settings::silentaim::enabled || silentaim::state.has_original_sizes) && aim_indicator_check_counter++ % 10 == 0)
		{
			try
			{
				rbx::instance_t local_player{};
				{
					std::lock_guard<std::mutex> lock(cache::mtx);
					local_player = cache::cached_local_player.instance;
				}
				rbx::instance_t player_gui = local_player.address ? local_player.find_first_child("PlayerGui") : rbx::instance_t{};

				// No PlayerGui yet means no gun indicator; targeting below still runs.
				rbx::instance_t aim_frame{};
				std::vector<rbx::instance_t> children = player_gui.address ? player_gui.get_children() : std::vector<rbx::instance_t>{};

				for (auto& child : children)
				{
					if (!child.address)
						continue;

					std::string child_name = child.get_name();
					if (child_name == "Aim")
					{
						aim_frame = child;
						break;
					}

					std::string child_class = child.get_class_name();
					if (child_class == "Frame" || child_class == "ScreenGui" || child_class == "GuiObject")
					{
						std::string child_lower = child_name;
						std::transform(child_lower.begin(), child_lower.end(), child_lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

						if (child_lower.find("main") != std::string::npos)
						{
							std::vector<rbx::instance_t> grandchildren = child.get_children();
							for (auto& grandchild : grandchildren)
							{
								if (grandchild.address)
								{
									std::string grandchild_name = grandchild.get_name();
									if (grandchild_name == "Aim")
									{
										aim_frame = grandchild;
										break;
									}
								}
							}

							if (aim_frame.address)
								break;
						}
					}
				}

				if (aim_frame.address != silentaim::state.aim_indicator.address)
				{
					if (silentaim::state.aim_indicator.address && silentaim::state.has_original_sizes)
					{
						memory->write<math::vector2>(silentaim::state.aim_indicator.address + Offsets::GuiObject::Size, silentaim::state.original_size);
						for (const auto& [child_addr, original_size] : silentaim::state.original_children_sizes)
						{
							memory->write<math::vector2>(child_addr + Offsets::GuiObject::Size, original_size);
						}
					}

					silentaim::state.aim_indicator = aim_frame;
					silentaim::state.has_original_sizes = false;
					silentaim::state.original_children_sizes.clear();

					if (silentaim::state.aim_indicator.address)
					{
						silentaim::state.original_size = memory->read<math::vector2>(silentaim::state.aim_indicator.address + Offsets::GuiObject::Size);
						std::vector<rbx::instance_t> children = silentaim::state.aim_indicator.get_children();
						for (auto& child : children)
						{
							math::vector2 child_size = memory->read<math::vector2>(child.address + Offsets::GuiObject::Size);
							silentaim::state.original_children_sizes.push_back({ child.address, child_size });
						}
						silentaim::state.has_original_sizes = true;
					}
				}

				if (settings::silentaim::enabled && silentaim::state.aim_indicator.address && silentaim::state.has_original_sizes)
				{
					memory->write<math::vector2>(silentaim::state.aim_indicator.address + Offsets::GuiObject::Size, { 0, 0 });

					std::vector<rbx::instance_t> children = silentaim::state.aim_indicator.get_children();
					for (auto& child : children)
					{
						memory->write<math::vector2>(child.address + Offsets::GuiObject::Size, { 0, 0 });
					}
				}
				else if (silentaim::state.aim_indicator.address && silentaim::state.has_original_sizes)
				{
					memory->write<math::vector2>(silentaim::state.aim_indicator.address + Offsets::GuiObject::Size, silentaim::state.original_size);
					for (const auto& [child_addr, original_size] : silentaim::state.original_children_sizes)
					{
						memory->write<math::vector2>(child_addr + Offsets::GuiObject::Size, original_size);
					}
				}
			}
			catch (...)
			{
				silentaim::state.aim_indicator = rbx::instance_t{};
			}
		}

		if (!settings::silentaim::enabled && silentaim::state.aim_indicator.address && silentaim::state.has_original_sizes)
		{
			memory->write<math::vector2>(silentaim::state.aim_indicator.address + Offsets::GuiObject::Size, silentaim::state.original_size);
			for (const auto& [child_addr, original_size] : silentaim::state.original_children_sizes)
			{
				memory->write<math::vector2>(child_addr + Offsets::GuiObject::Size, original_size);
			}
		}

		const bool active = is_silent_aim_active();
		if (!active)
		{
			set_target_status(GetForegroundWindow() != game::get_roblox_window() ? "Roblox isn't in front"
				: settings::silentaim::guncheck && !silentaim::state.aim_indicator.address ? "Gun check: no gun crosshair found"
				: "Hold your key to aim");
			silentaim::state.data_ready = false;
			silentaim::state.target = cache::entity_t{};
			silentaim::state.target_world_pos = math::vector3{};

			if (game::camera != 0 && silentaim::state.viewport_spoofed)
			{
				silentaim::state.viewport_spoofed = false;
				rbx::camera_t camera{ game::camera };
				math::vector2 dims = game::visengine.get_dimensions();
				HWND rw = game::get_roblox_window();
				if (rw)
				{
					RECT cr{};
					if (GetClientRect(rw, &cr))
					{
						dims.x = static_cast<float>(cr.right - cr.left);
						dims.y = static_cast<float>(cr.bottom - cr.top);
					}
				}
				rbx::vector2int16 vp;
				vp.x = static_cast<int16_t>(dims.x);
				vp.y = static_cast<int16_t>(dims.y);
				camera.set_viewport(vp);
			}

			continue;
		}

		const bool ignore_friendly = (settings::silentaim::priorities & (1 << 0)) != 0;

		if (settings::silentaim::sticky_aim && silentaim::state.target.instance.address)
		{
			cache::entity_t refreshed_target{};
			cache::entity_t local_player_snapshot{};
			bool target_found = false;
			{
				std::lock_guard<std::mutex> lock(cache::mtx);
				local_player_snapshot = cache::cached_local_player;
				for (const auto& entity : cache::cached_players)
				{
					if (entity.instance.address == silentaim::state.target.instance.address)
					{
						refreshed_target = entity;
						target_found = true;
						break;
					}
				}
			}

			if (target_found)
			{
				if (is_valid_target(refreshed_target, local_player_snapshot, ignore_friendly))
				{
					silentaim::state.target = refreshed_target;
				}
				else
				{
					silentaim::state.target = cache::entity_t{};
					silentaim::state.target_world_pos = math::vector3{};
				}
			}
			else
			{
				silentaim::state.target = cache::entity_t{};
				silentaim::state.target_world_pos = math::vector3{};
				silentaim::state.data_ready = false;
				continue;
			}
		}
		else if (!settings::silentaim::sticky_aim)
		{
			silentaim::state.target = cache::entity_t{};
			silentaim::state.target_world_pos = math::vector3{};
		}

		if (!silentaim::state.target.instance.address)
		{
			cache::entity_t new_target{};

			if (settings::silentaim::use_aimbot_target)
			{
				cache::entity_t local_player_snapshot{};
				{
					std::lock_guard<std::mutex> lock(cache::mtx);
					local_player_snapshot = cache::cached_local_player;

					if (aimbot::sticky_target.instance.address != 0)
					{
						for (const auto& entity : cache::cached_players)
						{
							if (entity.instance.address == aimbot::sticky_target.instance.address)
							{
								new_target = entity;
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
								new_target = entity;
								break;
							}
						}
					}
				}

				if (new_target.instance.address)
				{
					const bool ignore_friendly = (settings::silentaim::priorities & (1 << 0)) != 0;
					if (!is_valid_target(new_target, local_player_snapshot, ignore_friendly))
					{
						new_target = cache::entity_t{};
					}
				}
			}

			if (!new_target.instance.address)
			{
				new_target = get_closest_to_mouse();
			}

			if (new_target.instance.address)
				silentaim::state.target = new_target;
			else
			{
				set_target_status("No target in range");
				silentaim::state.data_ready = false;
				continue;
			}
		}

		if (!silentaim::state.target.instance.address || !game::visengine.address)
		{
			silentaim::state.data_ready = false;
			continue;
		}

		const math::matrix4 view = game::visengine.get_viewmatrix();

		HWND roblox_window = game::get_roblox_window();
		math::vector2 dims = game::visengine.get_dimensions();
		if (roblox_window)
		{
			RECT client_rect{};
			if (GetClientRect(roblox_window, &client_rect))
			{
				dims.x = static_cast<float>(client_rect.right - client_rect.left);
				dims.y = static_cast<float>(client_rect.bottom - client_rect.top);
			}
		}

		const math::vector2 aim_screen = game::aim_origin(game::visengine.get_dimensions());
		const math::vector2 mouse_pos{ aim_screen.x - game::window_offset_x, aim_screen.y - game::window_offset_y };
		const POINT client_cursor{ static_cast<LONG>(mouse_pos.x), static_cast<LONG>(mouse_pos.y) };

		// Teleported, respawned or a bad read: hold off for a moment.
		{
			math::vector3 root{};
			if (!aim_guard::root_of(silentaim::state.target, root) || !aim_guard::steady(silentaim::state.target.instance.address, root))
			{
				silentaim::state.data_ready = false;
				continue;
			}
		}

		math::vector3 target_pos;
		if (!get_target_position(silentaim::state.target, view, dims, mouse_pos, target_pos))
		{
			silentaim::state.target_world_pos = math::vector3{};
			silentaim::state.data_ready = false;
			continue;
		}

		silentaim::state.target_world_pos = target_pos;

		math::vector2 screen_target_pos{};
		if (!game::visengine.world_to_screen(target_pos, screen_target_pos, dims, view))
		{
			silentaim::state.target_world_pos = math::vector3{};
			silentaim::state.data_ready = false;
			continue;
		}

		math::vector2 client_target_pos = screen_target_pos;
		client_target_pos.x -= game::window_offset_x;
		client_target_pos.y -= game::window_offset_y;

		// Only ever inside the circle: a target that leaves it is dropped (sticky aim just stops
		// it switching to someone else while it's inside).
		if (fov_on())
		{
			const float distance = get_distance_from_center(mouse_pos, client_target_pos);
			if (distance > settings::silentaim::fov)
			{
				silentaim::state.target = cache::entity_t{};
				silentaim::state.target_world_pos = math::vector3{};
				silentaim::state.data_ready = false;
				continue;
			}
		}

		if (game::camera != 0)
		{
			rbx::camera_t camera{ game::camera };
			camera.set_viewport(make_viewport_from_target(dims, client_target_pos));
			silentaim::state.viewport_spoofed = true;
		}

		silentaim::state.spoof_pos_x = static_cast<std::uint64_t>(client_cursor.x);
		silentaim::state.spoof_pos_y = static_cast<std::uint64_t>(client_cursor.y);
		silentaim::state.target_screen_pos = client_target_pos;
		silentaim::state.data_ready = true;
		set_target_status("Target: " + silentaim::state.target.name);
	}
}

static void aim_application_thread()
{
	using namespace std::chrono_literals;

	std::uint64_t mouse_service = 0;
	rbx::c_silent_help mouse_helper{};
	bool mouse_initialized = false;

	std::uint64_t last_spoof_pos_x = 0;
	std::uint64_t last_spoof_pos_y = 0;
	math::vector2 last_target_pos{};
	auto last_valid_target_time = std::chrono::steady_clock::now();
	bool had_valid_target = false;

	for (;;)
	{
		const bool active = is_silent_aim_active();
		const bool has_target = silentaim::state.data_ready && silentaim::state.target.instance.address != 0;
		const auto current_time = std::chrono::steady_clock::now();

		if (!active)
		{
			mouse_service = 0;
			mouse_initialized = false;
			had_valid_target = false;
			std::this_thread::sleep_for(5ms);
			continue;
		}

		// A new server has a new MouseService.
		static std::uint64_t last_datamodel = 0;
		if (game::datamodel.address != last_datamodel)
		{
			last_datamodel = game::datamodel.address;
			mouse_service = 0;
			mouse_initialized = false;
		}

		if (!game::datamodel.address)
		{
			mouse_service = 0;
			mouse_initialized = false;
			std::this_thread::sleep_for(10ms);
			continue;
		}

		if (has_target)
		{
			had_valid_target = true;
			last_valid_target_time = current_time;
			last_spoof_pos_x = silentaim::state.spoof_pos_x;
			last_spoof_pos_y = silentaim::state.spoof_pos_y;
			last_target_pos = silentaim::state.target_screen_pos;
		}
		else if (had_valid_target)
		{
			// Keep aiming at the last spot briefly while the target is re-read.
			auto time_since = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - last_valid_target_time);
			if (time_since.count() > 60)
			{
				had_valid_target = false;
				std::this_thread::sleep_for(1ms);
				continue;
			}
		}
		else
		{
			std::this_thread::sleep_for(1ms);
			continue;
		}

		math::vector2 target_pos = has_target ? silentaim::state.target_screen_pos : last_target_pos;
		if (target_pos.x < 0.0f || target_pos.y < 0.0f || target_pos.x > 10000.0f || target_pos.y > 10000.0f)
		{
			std::this_thread::sleep_for(10ms);
			continue;
		}

		// Hit chance (non-PF only)
		if (settings::silentaim::hitchance_enabled && !game::is_phantom_forces)
		{
			static std::mt19937 sa_hc_rng(std::random_device{}());
			std::uniform_real_distribution<float> dist(0.0f, 100.0f);
			if (dist(sa_hc_rng) > settings::silentaim::hitchance)
			{
				std::this_thread::sleep_for(5ms);
				continue;
			}
		}

		try
		{
			// PF: write rotation to Camera's child Part primitive
			if (game::is_phantom_forces)
			{
				if (!game::datamodel.address || game::camera == 0)
				{
					std::this_thread::sleep_for(10ms);
					continue;
				}

				std::uint64_t workspace_addr = memory->read<std::uint64_t>(game::datamodel.address + Offsets::DataModel::Workspace);
				if (workspace_addr == 0) { std::this_thread::sleep_for(10ms); continue; }

				std::uint64_t cam = memory->read<std::uint64_t>(workspace_addr + Offsets::Workspace::CurrentCamera);
				if (cam == 0) { std::this_thread::sleep_for(10ms); continue; }

				rbx::instance_t camera_instance(cam);
				rbx::instance_t camera_part_inst = camera_instance.find_first_child("Part");
				if (camera_part_inst.address == 0)
				{
					set_apply_status("PF: the camera has no \"Part\" to turn");
					std::this_thread::sleep_for(10ms);
					continue;
				}

				rbx::part_t camera_part(camera_part_inst.address);
				rbx::primitive_t cam_prim = camera_part.get_primitive();
				if (cam_prim.address == 0) { std::this_thread::sleep_for(10ms); continue; }

				math::vector3 camera_pos = memory->read<math::vector3>(cam + Offsets::Camera::Position);
				math::vector3 target_world = silentaim::state.target_world_pos;

				if (std::isnan(target_world.x) || std::isnan(target_world.y) || std::isnan(target_world.z) ||
					(target_world.x == 0.0f && target_world.y == 0.0f && target_world.z == 0.0f))
				{
					std::this_thread::sleep_for(2ms);
					continue;
				}

				// PF look_at (matrix layout: right.y is positive)
				auto pf_look_at = [](const math::vector3& from, const math::vector3& to) -> math::matrix3
				{
					math::vector3 dir = to - from;
					float len = dir.length();
					if (len < 0.0001f) return math::matrix3::identity();
					math::vector3 forward = dir * (1.0f / len);
					math::vector3 world_up(0.0f, 1.0f, 0.0f);
					math::vector3 right = world_up.cross(forward);
					len = right.length();
					if (len < 0.0001f) return math::matrix3::identity();
					right = right * (1.0f / len);
					math::vector3 up = forward.cross(right);
					math::matrix3 r{};
					r.m[0][0] = -right.x;  r.m[0][1] = up.x;  r.m[0][2] = -forward.x;
					r.m[1][0] =  right.y;  r.m[1][1] = up.y;  r.m[1][2] = -forward.y;
					r.m[2][0] = -right.z;  r.m[2][1] = up.z;  r.m[2][2] = -forward.z;
					return r;
				};

				math::matrix3 target_matrix = pf_look_at(camera_pos, target_world);

				memory->write<math::matrix3>(cam_prim.address + Offsets::Primitive::Rotation, target_matrix);
				set_apply_status("PF: turning the camera part at the target");
				std::this_thread::sleep_for(2ms);
				continue;
			}

			if (!mouse_service)
			{
				rbx::instance_t mouse_service_instance = game::datamodel.find_first_child("MouseService");
				if (!mouse_service_instance.address)
					mouse_service_instance = game::datamodel.find_first_child_by_class("MouseService");
				mouse_service = mouse_service_instance.address;
				if (!mouse_service)
				{
					set_apply_status("MouseService not found");
					static bool warned_once = false;
					if (!warned_once) { printf("[SA] MouseService not found in DataModel\n"); warned_once = true; }
					std::this_thread::sleep_for(100ms);
					continue;
				}
				else
				{
					printf("[SA] MouseService found at 0x%llx\n", (unsigned long long)mouse_service);
				}
			}

			if (!mouse_initialized)
			{
				mouse_helper.initialize_mouse_service(mouse_service);
				mouse_initialized = true;
			}

			// The same client coordinates either way (the grace path used to add 58 px).
			mouse_helper.write_mouse_position(mouse_service, target_pos.x, target_pos.y);
			set_apply_status("Redirecting the mouse position");
			std::this_thread::sleep_for(1ms);

		}
		catch (...)
		{
			mouse_service = 0;
			mouse_initialized = false;
			std::this_thread::sleep_for(10ms);
		}
	}
}

void silentaim::run()
{
	std::thread(targeting_thread).detach();
	std::thread(aim_application_thread).detach();
}