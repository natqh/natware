#include "aimbot.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>
#include <thread>
#include <game/game.h>
#include <settings.h>
#include <sdk/math/math.h>
#include <sdk/sdk.h>
#include <memory/memory.h>
#include <Offsets/Offsets.hpp>
#include <menu/keybind/keybind.h>
#include <cache/bodyparts/bodyparts.h>
#include <cache/cache.h>
#include <cache/custom_entities/custom_entities.h>
#include <mutex>
#include <cctype>
#include <wallcheck/wallcheck.h>
#include <sdk/math/viewproj.h>
#include "pf_mouse.h"
#include "aim_guard.h"
#include "physical_mouse.h"
#include "aim_target.h"
#include "aim_log.h"

static bool is_jumping(const cache::entity_t& entity)
{
	if (entity.humanoid.address == 0)
		return false;

	rbx::humanoid_t humanoid(entity.humanoid.address);
	int humanoid_state = static_cast<int>(humanoid.get_state());

	if (humanoid_state == 3 || humanoid_state == 5)
		return true;

	auto root_it = entity.parts.find("HumanoidRootPart");
	if (root_it == entity.parts.end() || !root_it->second.address)
		return false;

	try
	{
		std::uint64_t prim_address = memory->read<std::uint64_t>(root_it->second.address + Offsets::BasePart::Primitive);
		if (!prim_address)
			return false;

		math::vector3 velocity = memory->read<math::vector3>(prim_address + Offsets::Primitive::AssemblyLinearVelocity);
		return velocity.y > 5.0f;
	}
	catch (...)
	{
		return false;
	}
}

static math::vector3 get_velocity(cache::entity_t& entity)
{
	auto root_it = entity.parts.find("HumanoidRootPart");
	if (root_it == entity.parts.end() || !root_it->second.address)
		root_it = entity.parts.find("Torso");
	if (root_it == entity.parts.end() || !root_it->second.address)
		root_it = entity.parts.find("Head");

	if (root_it == entity.parts.end() || !root_it->second.address)
	{
		return math::vector3(0.0f, 0.0f, 0.0f);
	}

	rbx::part_t part_copy = root_it->second;
	rbx::primitive_t prim = part_copy.get_primitive();
	
	if (!prim.address)
	{
		auto head_it = entity.parts.find("Head");
		if (head_it != entity.parts.end() && head_it->second.address)
		{
			part_copy = head_it->second;
			prim = part_copy.get_primitive();
		}
		
		if (!prim.address)
			return math::vector3(0.0f, 0.0f, 0.0f);
	}

	try
	{
		return memory->read<math::vector3>(prim.address + Offsets::Primitive::AssemblyLinearVelocity);
	}
	catch (...)
	{
		return math::vector3(0.0f, 0.0f, 0.0f);
	}
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
	math::vector3 velocity = get_velocity(entity);

	float prediction_factor_x;
	float prediction_factor_y;

	if (settings::aimbot::air_prediction_enabled && is_jumping(entity))
	{
		prediction_factor_x = (20.0f - settings::aimbot::air_prediction_x) * 0.02f;
		prediction_factor_y = (20.0f - settings::aimbot::air_prediction_y) * 0.02f;
	}
	else
	{
		prediction_factor_x = (20.0f - settings::aimbot::prediction_x) * 0.02f;
		prediction_factor_y = (20.0f - settings::aimbot::prediction_y) * 0.02f;
	}

	// (z used to be left out, so anyone moving toward or away from you was never led.)
	return position + bounded_lead(velocity, prediction_factor_x, prediction_factor_y);
}

namespace easing {
	float linear(float t) {
		return t;
	}

	float ease_in_quad(float t) {
		return t * t;
	}

	float ease_out_quad(float t) {
		return t * (2 - t);
	}

	float ease_in_out_quad(float t) {
		return t < 0.5 ? 2 * t * t : -1 + (4 - 2 * t) * t;
	}

	float ease_in_cubic(float t) {
		return t * t * t;
	}

	float ease_out_cubic(float t) {
		return (--t) * t * t + 1;
	}

	float ease_in_out_cubic(float t) {
		return t < 0.5 ? 4 * t * t * t : (t - 1) * (2 * t - 2) * (2 * t - 2) + 1;
	}

	float ease_in_sine(float t) {
		return 1.0f - std::cosf((t * 3.14159265f) / 2.0f);
	}

	float ease_out_sine(float t) {
		return std::sinf((t * 3.14159265f) / 2.0f);
	}

	float ease_in_out_sine(float t) {
		return -(std::cosf(3.14159265f * t) - 1.0f) / 2.0f;
	}
}

// Mouse mode steers through the same controller as Phantom Forces' (pf_mouse.cpp): one correction
// per frame minus what's still in flight, and a sensitivity learned from how the view turns.
static void mouse_aim(const math::vector2& target, const math::vector2& origin, const math::matrix4& view, const math::vector2& dims)
{
	CURSORINFO info{};
	info.cbSize = sizeof(info);
	const bool cursor_free = !game::is_phantom_forces && GetCursorInfo(&info) && (info.flags & CURSOR_SHOWING);
	if (!cursor_free)
	{
		pf_mouse::steer_to(target, origin, view, dims);   // the mouse turns the camera
		return;
	}

	// The cursor is free: the mouse moves it, not the camera, and where it lands can be read back
	// right away, so plain pixel moves (part of the way each tick) home in without overshooting.
	static float rem_x = 0.0f, rem_y = 0.0f;
	const float dx = target.x - origin.x, dy = target.y - origin.y;
	if (dx * dx + dy * dy < 1.5f * 1.5f)
	{
		rem_x = rem_y = 0.0f;
		return;
	}
	float kx = 0.35f, ky = 0.35f;
	if (settings::aimbot::smoothing)
	{
		kx = (std::min)(kx, std::clamp(1.0f / settings::aimbot::smoothingx, 0.01f, 1.0f) * 0.1f);
		ky = (std::min)(ky, std::clamp(1.0f / settings::aimbot::smoothingy, 0.01f, 1.0f) * 0.1f);
	}
	rem_x += dx * kx;
	rem_y += dy * ky;
	const int mx = std::clamp(static_cast<int>(std::lround(rem_x)), -400, 400);
	const int my = std::clamp(static_cast<int>(std::lround(rem_y)), -400, 400);
	rem_x -= static_cast<float>(mx);
	rem_y -= static_cast<float>(my);
	if ((mx == 0 && my == 0) || GetForegroundWindow() != game::get_roblox_window())
		return;
	aim_log::note_move(mx, my);
	INPUT input{};
	input.type = INPUT_MOUSE;
	input.mi.dx = mx;
	input.mi.dy = my;
	input.mi.dwFlags = MOUSEEVENTF_MOVE;
	SendInput(1, &input, sizeof(INPUT));
}

static void mouse_aimbot(const aim_target::aim_t& aim, const math::matrix4& view, const math::vector2& dims)
{
	math::vector2 target_screen = aim.screen;
	if (settings::aimbot::offset_enabled)
	{
		target_screen.x += settings::aimbot::offset_x;
		target_screen.y += settings::aimbot::offset_y;
	}
	mouse_aim(target_screen, game::aim_origin(dims), view, dims);
}

// The "closest" target part: the one nearest the crosshair on screen. It used to be the one
// nearest the camera, which jumped to the legs whenever they were a little closer than the head
// (someone walking toward you, or below you). It also keeps the part it has unless another is
// clearly nearer, so it can't flip between two.
static bool closest_part_on_screen(const cache::entity_t& e, const math::matrix4& view, const math::vector2& dims, math::vector3& out)
{
	static std::uint64_t last_entity = 0;
	static std::string last_part;
	static const char* const types[] = { "Head", "HumanoidRootPart", "LeftArm", "RightArm", "LeftLeg", "RightLeg" };
	const math::vector2 origin = game::aim_origin(dims);

	float best = FLT_MAX, kept = FLT_MAX;
	const char* best_type = nullptr;
	math::vector3 best_pos{}, kept_pos{};
	for (const char* type : types)
	{
		math::vector3 pos{};
		if (!bodyparts::get_part_position(e, type, pos))
			continue;
		math::vector2 s{};
		if (!game::visengine.world_to_screen(pos, s, dims, view))
			continue;
		const float d = std::sqrt((s.x - origin.x) * (s.x - origin.x) + (s.y - origin.y) * (s.y - origin.y));
		if (d < best)
		{
			best = d;
			best_type = type;
			best_pos = pos;
		}
		if (e.instance.address == last_entity && last_part == type)
		{
			kept = d;
			kept_pos = pos;
		}
	}
	if (!best_type)
		return false;
	if (kept < FLT_MAX && kept <= best * 1.25f + 8.0f)
	{
		out = kept_pos;
		return true;
	}
	last_entity = e.instance.address;
	last_part = best_type;
	out = best_pos;
	return true;
}

static math::matrix3 look_at(const math::vector3& from, const math::vector3& to)
{
	math::vector3 forward = (to - from);
	float length = forward.length();
	if (length < 0.0001f)
	{
		return math::matrix3::identity();
	}
	forward = forward * (1.0f / length);

	math::vector3 world_up(0.0f, 1.0f, 0.0f);
	math::vector3 right = world_up.cross(forward);
	length = right.length();
	if (length < 0.0001f)
	{
		world_up = math::vector3(0.0f, 0.0f, 1.0f);
		right = world_up.cross(forward);
		length = right.length();
		if (length < 0.0001f)
		{
			return math::matrix3::identity();
		}
	}
	right = right * (1.0f / length);

	math::vector3 up = forward.cross(right);

	math::matrix3 result{};
	result.m[0][0] = -right.x;
	result.m[0][1] = up.x;
	result.m[0][2] = -forward.x;
	result.m[1][0] = -right.y;
	result.m[1][1] = up.y;
	result.m[1][2] = -forward.y;
	result.m[2][0] = -right.z;
	result.m[2][1] = up.z;
	result.m[2][2] = -forward.z;

	return result;
}

static void camera_aimbot(math::vector3 target_pos)
{
	if (game::datamodel.address == 0)
	{
		return;
	}

	std::uint64_t workspace = memory->read<std::uint64_t>(game::datamodel.address + Offsets::DataModel::Workspace);
	if (workspace == 0)
	{
		return;
	}

	std::uint64_t camera = memory->read<std::uint64_t>(workspace + Offsets::Workspace::CurrentCamera);
	if (camera == 0)
	{
		return;
	}

	math::vector3 camera_pos = memory->read<math::vector3>(camera + Offsets::Camera::Position);
	if (!aim_guard::plausible(target_pos, camera_pos))
		return;

	math::vector3 direction = target_pos - camera_pos;
	float distance = direction.length();
	if (distance < 0.1f)
	{
		return;
	}

	if (settings::aimbot::offset_enabled)
	{
		math::matrix3 current_rot = memory->read<math::matrix3>(camera + Offsets::Camera::Rotation);
		math::vector3 right = current_rot.right();
		math::vector3 up = current_rot.up();

		math::matrix4 view = game::visengine.get_viewmatrix();
		math::vector2 dims = game::visengine.get_dimensions();

		float fov_rad = 1.0472f;
		float scale_x = distance * std::tanf(fov_rad * 0.5f) * 2.0f / dims.x;
		float scale_y = distance * std::tanf(fov_rad * 0.5f) * 2.0f / dims.y;

		target_pos = target_pos + right * (settings::aimbot::offset_x * scale_x) + up * (-settings::aimbot::offset_y * scale_y);
	}

	math::matrix3 target_rot = look_at(camera_pos, target_pos);
	math::matrix3 current_rot = memory->read<math::matrix3>(camera + Offsets::Camera::Rotation);

	math::vector3 current_forward = current_rot.forward();
	math::vector3 target_forward = target_rot.forward();

	float current_yaw = std::atan2f(-current_forward.x, -current_forward.z);
	float current_pitch = std::asinf(current_forward.y);

	float target_yaw = std::atan2f(-target_forward.x, -target_forward.z);
	float target_pitch = std::asinf(target_forward.y);

	float yaw_diff = target_yaw - current_yaw;
	if (yaw_diff > 3.14159f)
	{
		yaw_diff -= 6.28318f;
	}
	else if (yaw_diff < -3.14159f)
	{
		yaw_diff += 6.28318f;
	}

	// Never aim within 5 degrees of straight up or down: the look-at would have no clear "up"
	// there and could spin the camera.
	target_pitch = std::clamp(target_pitch, -1.48f, 1.48f);
	float pitch_diff = target_pitch - current_pitch;

	float total_angle_diff = std::sqrtf(yaw_diff * yaw_diff + pitch_diff * pitch_diff);
	if (total_angle_diff < 0.001f)
	{
		return;
	}

	// The camera faces along (yaw, pitch); builds a proper rotation for any blend of the two.
	auto rotation_for = [&](float yaw, float pitch)
	{
		pitch = std::clamp(pitch, -1.48f, 1.48f);
		const math::vector3 forward{ -std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch) };
		return look_at(camera_pos, camera_pos + forward);
	};

	if (settings::aimbot::smoothing) {
		static auto last_time = std::chrono::steady_clock::now();
		auto now = std::chrono::steady_clock::now();
		float dt = std::chrono::duration<float>(now - last_time).count();
		last_time = now;
		dt = max(0.0001f, min(dt, 0.1f));

		float tx = 1.0f / settings::aimbot::smoothingx;
		float ty = 1.0f / settings::aimbot::smoothingy;

		tx = max(0.01f, min(1.0f, tx));
		ty = max(0.01f, min(1.0f, ty));

		float smooth_factor_x, smooth_factor_y;

		switch (settings::aimbot::smoothing_style) {
		case 1: smooth_factor_x = easing::linear(tx); smooth_factor_y = easing::linear(ty); break;
		case 2: smooth_factor_x = easing::ease_in_quad(tx); smooth_factor_y = easing::ease_in_quad(ty); break;
		case 3: smooth_factor_x = easing::ease_out_quad(tx); smooth_factor_y = easing::ease_out_quad(ty); break;
		case 4: smooth_factor_x = easing::ease_in_out_quad(tx); smooth_factor_y = easing::ease_in_out_quad(ty); break;
		case 5: smooth_factor_x = easing::ease_in_cubic(tx); smooth_factor_y = easing::ease_in_cubic(ty); break;
		case 6: smooth_factor_x = easing::ease_out_cubic(tx); smooth_factor_y = easing::ease_out_cubic(ty); break;
		case 7: smooth_factor_x = easing::ease_in_out_cubic(tx); smooth_factor_y = easing::ease_in_out_cubic(ty); break;
		case 8: smooth_factor_x = easing::ease_in_sine(tx); smooth_factor_y = easing::ease_in_sine(ty); break;
		case 9: smooth_factor_x = easing::ease_out_sine(tx); smooth_factor_y = easing::ease_out_sine(ty); break;
		case 10: smooth_factor_x = easing::ease_in_out_sine(tx); smooth_factor_y = easing::ease_in_out_sine(ty); break;
		default: smooth_factor_x = tx; smooth_factor_y = ty; break;
		}

		smooth_factor_x = 1.0f - powf(1.0f - smooth_factor_x, dt * 60.0f);
		smooth_factor_y = 1.0f - powf(1.0f - smooth_factor_y, dt * 60.0f);

		// Turn part of the way in yaw and pitch (blending the matrices number by number made a
		// skewed, non-rotation matrix on big turns).
		memory->write<math::matrix3>(camera + Offsets::Camera::Rotation,
			rotation_for(current_yaw + yaw_diff * smooth_factor_x, current_pitch + pitch_diff * smooth_factor_y));
	}
	else {
		memory->write<math::matrix3>(camera + Offsets::Camera::Rotation, rotation_for(target_yaw, target_pitch));
	}
}

// ============================================================================
// Silent Aim (PF only) - Camera Part rotation method
// Writes a look-at rotation matrix to Camera's child "Part" primitive.
// PF reads bullet direction from this Part, so it redirects shots without
// visibly moving the camera. The m[1][1]=0.01f trick prevents visual snap.
// ============================================================================
static std::mutex g_pf_silent_mtx;
static std::string g_pf_silent_status;
static void set_pf_silent_status(const char* text)
{
	std::lock_guard<std::mutex> lock(g_pf_silent_mtx);
	g_pf_silent_status = text;
}

std::string aimbot::status()
{
	if (!settings::aimbot::enabled)
		return "";
	if (game::is_phantom_forces && settings::aimbot::mode == 2)
	{
		std::lock_guard<std::mutex> lock(g_pf_silent_mtx);
		return g_pf_silent_status.empty() ? "Hold your aim key" : g_pf_silent_status;
	}
	// Who it's on (aim_target), and in Mouse mode how far the sensitivity learning has got.
	const std::string target = aim_target::status();
	if (settings::aimbot::mode == 0 && target == "Locked on")
	{
		const std::string mouse = pf_mouse::status();
		if (!mouse.empty())
			return mouse;
	}
	return target;
}

static void pf_silent_aimbot()
{
	if (!game::is_phantom_forces)
		return;

	if (aimbot::player.instance.address == 0 || game::datamodel.address == 0)
		return;

	std::uint64_t workspace = memory->read<std::uint64_t>(game::datamodel.address + Offsets::DataModel::Workspace);
	if (workspace == 0)
		return;

	std::uint64_t camera = memory->read<std::uint64_t>(workspace + Offsets::Workspace::CurrentCamera);
	if (camera == 0)
		return;

	rbx::instance_t camera_instance(camera);
	rbx::instance_t camera_part_inst = camera_instance.find_first_child("Part");
	if (camera_part_inst.address == 0)
	{
		set_pf_silent_status("The camera has no \"Part\" to turn (PF may have changed)");
		return;
	}

	rbx::part_t camera_part(camera_part_inst.address);
	rbx::primitive_t cam_prim = camera_part.get_primitive();
	if (cam_prim.address == 0)
		return;

	math::vector3 camera_pos = memory->read<math::vector3>(camera + Offsets::Camera::Position);

	// Get target position using selected target part
	int target_part_to_use = settings::aimbot::target_part;
	if (settings::aimbot::air_part_enabled && aimbot::player.humanoid.address != 0 && is_jumping(aimbot::player))
		target_part_to_use = settings::aimbot::air_part;

	math::vector3 target_pos;
	bool found_part = false;

	switch (target_part_to_use)
	{
	case 1: found_part = bodyparts::get_part_position(aimbot::player, "Head", target_pos); break;
	case 2: found_part = bodyparts::get_part_position(aimbot::player, "HumanoidRootPart", target_pos); break;
	case 3: found_part = bodyparts::get_part_position(aimbot::player, "LeftArm", target_pos); break;
	case 4: found_part = bodyparts::get_part_position(aimbot::player, "RightArm", target_pos); break;
	case 5: found_part = bodyparts::get_part_position(aimbot::player, "LeftLeg", target_pos); break;
	case 6: found_part = bodyparts::get_part_position(aimbot::player, "RightLeg", target_pos); break;
	default:
		found_part = closest_part_on_screen(aimbot::player, game::visengine.get_viewmatrix(), game::visengine.get_dimensions(), target_pos);
		break;
	}

	if (!found_part)
		return;

	if (std::isnan(target_pos.x) || std::isnan(target_pos.y) || std::isnan(target_pos.z) ||
		(target_pos.x == 0.0f && target_pos.y == 0.0f && target_pos.z == 0.0f))
		return;

	if (settings::aimbot::enable_prediction)
		target_pos = apply_prediction(aimbot::player, target_pos);

	// Head offset
	target_pos.y += settings::aimbot::ai_silent_y_offset;

	auto look_at_matrix = [](const math::vector3& from, const math::vector3& to) -> math::matrix3
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

	math::matrix3 target_matrix = look_at_matrix(camera_pos, target_pos);
	target_matrix.m[1][1] = 0.01f;

	memory->write<math::matrix3>(cam_prim.address + Offsets::Primitive::Rotation, target_matrix);
	set_pf_silent_status("Turning the camera part at the target");

	std::this_thread::sleep_for(std::chrono::milliseconds(2));
}

void aimbot::run()
{
	using namespace std::chrono_literals;

	keybind::keybind_t aimbot_kb{};
	aimbot_kb.key = settings::aimbot::keybind;
	aimbot_kb.mode = static_cast<keybind::activation_mode>(settings::aimbot::activation_mode);

	for (;;)
	{
		if (!settings::aimbot::enabled)
		{
			aimbot::sticky_target = cache::entity_t{};
			aimbot::last_target = cache::entity_t{};
			aim_target::reset();
			Sleep(1);
			continue;
		}

		aimbot_kb.key = settings::aimbot::keybind;
		aimbot_kb.mode = static_cast<keybind::activation_mode>(settings::aimbot::activation_mode);

		if (!keybind::is_active(aimbot_kb))
		{
			aimbot::sticky_target = cache::entity_t{};
			aimbot::player = cache::entity_t{};
			aimbot::last_target = cache::entity_t{};
			pf_mouse::reset();
			aim_target::reset();
			Sleep(1);
			continue;
		}

		// One target and aim point for every mode (aim_target.cpp), then the mode moves the aim.
		const math::matrix4 view = game::visengine.get_viewmatrix();
		const math::vector2 dims = game::visengine.get_dimensions();
		aim_target::aim_t aim;
		const bool have_aim = dims.x >= 2.0f && dims.y >= 2.0f && aim_target::update(view, dims, game::aim_origin(dims), aim);
		if (aim_log::recording())
		{
			aim_log::row_t row;
			row.have_target = have_aim;
			row.mode = settings::aimbot::mode;
			row.status = aim_target::status();
			row.origin = game::aim_origin(dims);
			if (have_aim)
			{
				row.target = aim.target.name;
				row.part = aim.part;
				row.body = aim.body;
				row.point = aim.point;
				row.screen = aim.screen;
				row.distance_px = aim.distance_px;
				row.hold = aim.hold;
			}
			aim_log::tick(row);
		}
		if (!have_aim)
		{
			aimbot::player = cache::entity_t{};
			aimbot::sticky_target = cache::entity_t{};
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
			continue;
		}
		aimbot::player = aim.target;
		aimbot::last_target = aim.target;
		aimbot::sticky_target = settings::aimbot::sticky_aim ? aim.target : cache::entity_t{};
		if (aim.hold)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
			continue;
		}

		// Silent Aim only exists in Phantom Forces; elsewhere that choice runs as Camera. The
		// setting itself is left as the user picked it, so it's still there back in PF.
		const int mode = (settings::aimbot::mode == 2 && !game::is_phantom_forces) ? 1 : settings::aimbot::mode;
		if (mode == 0)
		{
			mouse_aimbot(aim, view, dims);
		}
		else if (mode == 1)
		{
			camera_aimbot(aim.point);
		}
		else if (mode == 2)
		{
			pf_silent_aimbot();
		}

		// About 1000 updates a second (1 ms timer period): far more than any frame rate, without
		// spinning a core or starving the render thread of the cache lock.
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}