#include "triggerbot.h"

#include <thread>
#include <chrono>
#include <random>
#include <algorithm>
#include <cmath>
#include <windows.h>
#include <game/game.h>
#include <memory/memory.h>
#include <Offsets/Offsets.hpp>
#include <settings.h>
#include <menu/keybind/keybind.h>
#include <cache/cache.h>
#include <features/aimbot/aimbot.h>
#include <features/silentaim/silentaim.h>
#include <wallcheck/wallcheck.h>
#include <sdk/math/viewproj.h>
#include <render/render.h>
#include <mutex>

// The cache thread reads Da Hood's BodyEffects["K.O"] with the rest of the player.
static bool is_player_knocked(const cache::entity_t& player)
{
	return player.knocked;
}

static void mouse_left_down()
{
	INPUT input{};
	input.type = INPUT_MOUSE;
	input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
	SendInput(1, &input, sizeof(INPUT));
}

static void mouse_left_up()
{
	INPUT input{};
	input.type = INPUT_MOUSE;
	input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
	SendInput(1, &input, sizeof(INPUT));
}

static void mouse_left_click()
{
	mouse_left_down();
	Sleep(15);
	mouse_left_up();
}

// Per-source fire state (0 = aimbot, 1 = silentaim)
struct fire_state_t
{
	float click_timer = 0.f;
	float hold_timer = 0.f;
	bool holding = false;
	std::chrono::high_resolution_clock::time_point last_fire_time{};
	bool first_shot = true;

	// Reaction: the crosshair has to stay on someone this long before the first shot (the delay
	// is drawn once per target, not slept on every pass). Grace: a frame or two off a body's
	// edge doesn't let go of a held trigger.
	bool on = false;
	std::chrono::steady_clock::time_point acquired_at{};
	std::chrono::steady_clock::time_point last_seen{};
	float reaction_s = 0.f;
};
static fire_state_t g_fire_state[2];

static bool mouse_fire_helper(int source, bool on_target, int fire_mode, float clicks_per_second, float hold_duration_val)
{
	auto& state = g_fire_state[source];

	if (!on_target)
	{
		state.on = false;
		state.click_timer = 0.f;
		state.hold_timer = 0.f;
		state.first_shot = true;
		if (state.holding)
		{
			mouse_left_up();
			state.holding = false;
		}
		return false;
	}

	// Calculate delta from last fire call
	auto now = std::chrono::high_resolution_clock::now();
	float delta = std::chrono::duration<float>(now - state.last_fire_time).count();
	state.last_fire_time = now;

	// Clamp delta to avoid huge jumps
	if (delta > 0.5f) delta = 0.0f;

	switch (fire_mode)
	{
	case 0: // Click mode
	{
		if (state.first_shot)
		{
			mouse_left_click();
			state.first_shot = false;
			state.click_timer = 0.f;
			return true;
		}
		float interval = 1.f / clicks_per_second;
		state.click_timer += delta;
		if (state.click_timer >= interval)
		{
			mouse_left_click();
			state.click_timer -= interval;
			return true;
		}
		break;
	}
	case 1: // Hold mode
	{
		if (!state.holding)
		{
			mouse_left_down();
			state.holding = true;
			state.hold_timer = 0.f;
		}
		state.hold_timer += delta;
		if (state.hold_timer >= hold_duration_val)
		{
			mouse_left_up();
			state.holding = false;
			state.hold_timer = 0.f;
			state.first_shot = true;
		}
		return true;
	}
	}

	return false;
}

static float get_world_distance(const cache::entity_t& entity, const cache::entity_t& local)
{
	auto target_it = entity.parts.find("HumanoidRootPart");
	if (target_it == entity.parts.end() || !target_it->second.address)
		target_it = entity.parts.find("Torso");
	if (target_it == entity.parts.end() || !target_it->second.address)
		target_it = entity.parts.find("Head");
	if (target_it == entity.parts.end() || !target_it->second.address)
		return FLT_MAX;

	auto local_it = local.parts.find("HumanoidRootPart");
	if (local_it == local.parts.end() || !local_it->second.address)
		local_it = local.parts.find("Torso");
	if (local_it == local.parts.end() || !local_it->second.address)
		return FLT_MAX;

	rbx::part_t target_part{ target_it->second };
	rbx::primitive_t target_prim = target_part.get_primitive();
	if (!target_prim.address) return FLT_MAX;

	rbx::part_t local_part{ local_it->second };
	rbx::primitive_t local_prim = local_part.get_primitive();
	if (!local_prim.address) return FLT_MAX;

	try
	{
		math::vector3 target_pos = target_prim.get_position();
		math::vector3 local_pos = local_prim.get_position();
		return local_pos.distance(target_pos);
	}
	catch (...)
	{
		return FLT_MAX;
	}
}

static bool get_entity_world_pos(const cache::entity_t& entity, math::vector3& out_pos)
{
	auto it = entity.parts.find("HumanoidRootPart");
	if (it == entity.parts.end() || !it->second.address)
		it = entity.parts.find("Head");
	if (it == entity.parts.end() || !it->second.address)
		return false;

	rbx::part_t part{ it->second };
	rbx::primitive_t prim = part.get_primitive();
	if (!prim.address) return false;

	try
	{
		out_pos = prim.get_position();
		return !(std::isnan(out_pos.x) || out_pos.x == 0.0f && out_pos.y == 0.0f && out_pos.z == 0.0f);
	}
	catch (...)
	{
		return false;
	}
}

static bool finite3(const math::vector3& v)
{
	return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// The camera, from the view-projection matrix itself: the one point where clip x, y and w are
// all zero (rows 0, 1 and 3 of the matrix, solved by Cramer's rule).
static bool get_camera_position(const math::matrix4& vp, math::vector3& out_pos)
{
	return math::camera_from_viewproj(vp, out_pos);
}

// The ray from the camera through where you aim: the cursor, or the crosshair when the game has
// locked the mouse (Phantom Forces, first person).
static bool get_cursor_ray(const math::matrix4& vp, const math::vector2& dims, math::vector3& origin, math::vector3& dir)
{
	if (dims.x <= 0.f || dims.y <= 0.f)
		return false;
	const math::vector2 aim = game::aim_origin(dims);
	const float nx = (aim.x - game::window_offset_x - dims.x * 0.5f) / (dims.x * 0.5f);
	const float ny = -(aim.y - game::window_offset_y - dims.y * 0.5f) / (dims.y * 0.5f);
	return math::ray_from_viewproj(vp, nx, ny, origin, dir);
}

// Cheap first pass: is this point within `radius` studs of the ray (and not behind the camera)?
static bool near_ray(const math::vector3& p, const math::vector3& origin, const math::vector3& dir, float radius)
{
	const math::vector3 v = p - origin;
	const float along = v.dot(dir);
	if (along < -radius)
		return false;
	return v.dot(v) - along * along <= radius * radius;
}

// Where the cursor ray first meets the target's rig: each body part (head, torso, arms, legs)
// is its own box, turned the way the part is turned, so the gaps between limbs and the space
// around the character don't count. Accessories aren't body parts, and invisible parts like the
// HumanoidRootPart are left out unless the rig has nothing else.
static bool ray_hits_rig(const cache::entity_t& entity, const math::vector3& origin, const math::vector3& dir, float& hit_t)
{
	constexpr float kMaxRay = 5000.f;

	struct part_box_t
	{
		rbx::obb box;
		bool shown;
	};
	std::vector<part_box_t> boxes;
	boxes.reserve(entity.parts.size());
	bool any_shown = false;

	for (const auto& [name, part] : entity.parts)
	{
		if (!part.address)
			continue;

		rbx::primitive_t prim = part.get_primitive();
		if (!prim.address)
			continue;

		const math::vector3 size = prim.get_size();
		const math::vector3 pos = prim.get_position();
		if (!finite3(size) || !finite3(pos) || size.x <= 0.f || size.y <= 0.f || size.z <= 0.f)
			continue;
		if ((std::max)((std::max)(size.x, size.y), size.z) > 40.f)
			continue;   // not a body part

		const float transparency = memory->read<float>(part.address + Offsets::BasePart::Transparency);
		const bool shown = !(std::isfinite(transparency) && transparency >= 0.99f);
		rbx::obb box(pos, size, math::cframe(pos, prim.get_rotation()));
		// A few pixels of give, growing with distance: what's drawn lags what's read by a frame,
		// and a far body is only a few pixels wide, so an exact test kept missing shots that
		// were on screen.
		const float margin = std::clamp((pos - origin).length() * 0.004f, 0.05f, 0.6f);
		box.half_size = box.half_size + math::vector3{ margin, margin, margin };
		boxes.push_back({ box, shown });
		any_shown = any_shown || shown;
	}

	bool hit = false;
	for (const part_box_t& b : boxes)
	{
		if (any_shown && !b.shown)
			continue;

		float t = 0.f;
		if (b.box.raycast(origin, dir, kMaxRay, t) && (!hit || t < hit_t))
		{
			hit_t = t;
			hit = true;
		}
	}
	return hit;
}

// For the silent-aim triggerbot (no crosshair needed): visible when the head or the torso can
// be seen from the camera.
static bool entity_visible(const cache::entity_t& entity, const math::vector3& camera)
{
	static const char* const sight_points[] = { "Head", "UpperTorso", "Torso", "HumanoidRootPart" };
	bool any = false;
	for (const char* name : sight_points)
	{
		auto it = entity.parts.find(name);
		if (it == entity.parts.end() || !it->second.address)
			continue;

		rbx::primitive_t prim = it->second.get_primitive();
		if (!prim.address)
			continue;

		const math::vector3 pos = prim.get_position();
		if (!finite3(pos))
			continue;

		any = true;
		if (wallcheck->is_visible(camera, pos))
			return true;
	}
	return !any;   // nothing to test against: don't block
}

static bool is_in_fov(const cache::entity_t& entity, const math::matrix4& view, const math::vector2& dims, float fov)
{
	POINT cursor{};
	if (!GetCursorPos(&cursor))
		return false;

	const math::vector2 cursor_pos{ static_cast<float>(cursor.x), static_cast<float>(cursor.y) };

	static const char* check_parts[] = { "Head", "HumanoidRootPart" };
	for (const char* name : check_parts)
	{
		auto it = entity.parts.find(name);
		if (it == entity.parts.end() || !it->second.address) continue;

		rbx::part_t part{ it->second };
		rbx::primitive_t prim = part.get_primitive();
		if (!prim.address) continue;

		math::vector3 pos_3d;
		try
		{
			pos_3d = prim.get_position();
			if (std::isnan(pos_3d.x)) continue;
		}
		catch (...) { continue; }

		math::vector2 pos_2d{};
		if (!game::visengine.world_to_screen(pos_3d, pos_2d, dims, view))
			continue;

		float dx = cursor_pos.x - pos_2d.x;
		float dy = cursor_pos.y - pos_2d.y;
		float dist = std::sqrtf(dx * dx + dy * dy);

		if (dist <= fov)
			return true;
	}

	return false;
}

// Shared triggerbot logic with delta-time fire modes
static void triggerbot_tick(
	int source,
	bool enabled,
	float reaction_ms,
	bool max_dist_enabled,
	float max_dist,
	bool use_teamcheck,
	bool use_knockcheck,
	bool use_healthcheck,
	float min_health,
	bool use_wallcheck,
	bool use_fov,
	float fov,
	int fire_mode,
	float clicks_per_second,
	float hold_duration_val)
{
	if (!enabled) return;

	// don't fire if Roblox isn't the foreground window or if the cheat menu is open
	HWND fg = GetForegroundWindow();
	HWND roblox = game::get_roblox_window();
	if (!roblox || fg != roblox) return;
	if (render->running) return;

	math::vector2 dims = game::visengine.get_dimensions();
	math::matrix4 view = game::visengine.get_viewmatrix();
	if (dims.x == 0 || dims.y == 0) return;

	std::vector<cache::entity_t> entities_snapshot;
	cache::entity_t local_snapshot;
	{
		std::lock_guard<std::mutex> lock(cache::mtx);
		entities_snapshot = cache::cached_players;
		local_snapshot = cache::cached_local_player;
	}

	math::vector3 ray_origin{}, ray_dir{};
	const bool has_ray = get_cursor_ray(view, dims, ray_origin, ray_dir);

	math::vector3 camera_pos{};
	const bool has_camera = get_camera_position(view, camera_pos);

	const cache::entity_t* target = nullptr;
	math::vector2 target_screen{};
	float target_t = FLT_MAX;

	for (const cache::entity_t& entity : entities_snapshot)
	{
		if (!entity.instance.address) continue;
		if (entity.instance.address == local_snapshot.instance.address) continue;

		if (use_teamcheck && entity.team != 0 && entity.team == local_snapshot.team)
			continue;

		if (settings::rage::is_whitelisted(entity.name))
			continue;

		if (entity.health <= 0.f)
			continue;

		if (use_healthcheck && entity.health < min_health)
			continue;

		if (max_dist_enabled && local_snapshot.instance.address != 0)
		{
			float dist = get_world_distance(entity, local_snapshot);
			if (dist > max_dist) continue;
		}

		if (use_fov && fov > 0.0f)
		{
			// Silent aim redirects the shot, so a target inside the FOV circle is enough.
			if (!is_in_fov(entity, view, dims, fov))
				continue;
			if (is_player_knocked(entity))
				continue;
			if (use_wallcheck && has_camera && !entity_visible(entity, camera_pos))
				continue;

			math::vector3 root{};
			if (get_entity_world_pos(entity, root))
				game::visengine.world_to_screen(root, target_screen, dims, view);
			target = &entity;
			break;
		}

		// Aimbot: only when the crosshair is on one of the target's body parts, and the nearest
		// such target wins.
		if (!has_ray)
			continue;

		math::vector3 root{};
		if (!get_entity_world_pos(entity, root) || !near_ray(root, ray_origin, ray_dir, 12.f))
			continue;

		float t = 0.f;
		if (!ray_hits_rig(entity, ray_origin, ray_dir, t) || t >= target_t)
			continue;

		if (is_player_knocked(entity))
			continue;

		// The wall check runs to the exact spot under the crosshair.
		const math::vector3 hit = ray_origin + ray_dir * t;
		if (use_wallcheck && !wallcheck->is_visible(ray_origin, hit))
			continue;

		target = &entity;
		target_t = t;
		game::visengine.world_to_screen(hit, target_screen, dims, view);
	}

	const bool found_target = target != nullptr;
	fire_state_t& state = g_fire_state[source];
	const auto now = std::chrono::steady_clock::now();
	if (found_target)
	{
		triggerbot::current_target = *target;
		triggerbot::current_target_screen = target_screen;
		triggerbot::has_target = true;

		if (!state.on)
		{
			// Just got on someone: the reaction delay (with +/- 20% jitter) starts now.
			static std::mt19937 rng(std::random_device{}());
			const float jitter = reaction_ms * 0.2f;
			std::uniform_real_distribution<float> dist_r(-jitter, jitter);
			state.on = true;
			state.acquired_at = now;
			state.reaction_s = reaction_ms > 0.0f ? (std::max)(0.0f, reaction_ms + dist_r(rng)) / 1000.0f : 0.0f;
		}
		state.last_seen = now;
		if (std::chrono::duration<float>(now - state.acquired_at).count() >= state.reaction_s)
			mouse_fire_helper(source, true, fire_mode, clicks_per_second, hold_duration_val);
		return;
	}

	// Off target for a moment (the crosshair slid over a body's edge): a held trigger stays
	// down and the reaction isn't started over; longer than that and it lets go.
	constexpr float kGrace = 0.08f;
	if (state.on && std::chrono::duration<float>(now - state.last_seen).count() < kGrace)
		return;
	state.on = false;
	mouse_fire_helper(source, false, 0, 0, 0);
	triggerbot::has_target = false;
}

void triggerbot::run_aimbot()
{
	keybind::keybind_t kb{};

	for (;;)
	{
		if (!settings::aimbot::triggerbot::enabled)
		{
			mouse_fire_helper(0, false, 0, 0, 0);
			triggerbot::has_target = false;
			Sleep(5);
			continue;
		}

		// Keybind check
		kb.key = settings::aimbot::triggerbot::keybind;
		kb.mode = static_cast<keybind::activation_mode>(settings::aimbot::triggerbot::activation_mode);
		if (kb.key != 0 && !keybind::is_active(kb))
		{
			mouse_fire_helper(0, false, 0, 0, 0);
			triggerbot::has_target = false;
			Sleep(5);
			continue;
		}

		triggerbot::active_source = 0;

		triggerbot_tick(
			0,
			settings::aimbot::triggerbot::enabled,
			settings::aimbot::triggerbot::reaction_ms,
			settings::aimbot::triggerbot::max_distance_enabled,
			settings::aimbot::triggerbot::max_distance,
			settings::aimbot::teamcheck,
			settings::aimbot::knock_check,
			settings::aimbot::health_check_enabled,
			settings::aimbot::min_health,
			settings::aimbot::triggerbot::wallcheck,
			false,
			0.0f,
			settings::aimbot::triggerbot::fire_mode,
			settings::aimbot::triggerbot::clicks_per_second,
			settings::aimbot::triggerbot::hold_duration
		);

		Sleep(2);   // the timer runs at 1 ms: about 500 checks a second
	}
}

void triggerbot::run_silentaim()
{
	keybind::keybind_t kb{};

	for (;;)
	{
		if (!settings::silentaim::enabled || !settings::silentaim::triggerbot::enabled)
		{
			mouse_fire_helper(1, false, 0, 0, 0);
			triggerbot::has_target = false;
			Sleep(5);
			continue;
		}

		// Keybind check
		kb.key = settings::silentaim::triggerbot::keybind;
		kb.mode = static_cast<keybind::activation_mode>(settings::silentaim::triggerbot::activation_mode);
		if (kb.key != 0 && !keybind::is_active(kb))
		{
			mouse_fire_helper(1, false, 0, 0, 0);
			triggerbot::has_target = false;
			Sleep(5);
			continue;
		}

		triggerbot::active_source = 1;

		triggerbot_tick(
			1,
			settings::silentaim::triggerbot::enabled,
			settings::silentaim::triggerbot::reaction_ms,
			settings::silentaim::triggerbot::max_distance_enabled,
			settings::silentaim::triggerbot::max_distance,
			settings::silentaim::teamcheck,
			settings::silentaim::knock_check,
			settings::silentaim::health_check_enabled,
			settings::silentaim::min_health,
			settings::silentaim::triggerbot::wallcheck,
			settings::silentaim::use_fov,
			settings::silentaim::fov,
			settings::silentaim::triggerbot::fire_mode,
			settings::silentaim::triggerbot::clicks_per_second,
			settings::silentaim::triggerbot::hold_duration
		);

		Sleep(2);   // the timer runs at 1 ms: about 500 checks a second
	}
}
