#include "aim_target.h"
#include "aim_guard.h"

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <cache/custom_entities/custom_entities.h>
#include <features/esp/rig.h>
#include <game/game.h>
#include <memory/memory.h>
#include <Offsets/Offsets.hpp>
#include <sdk/math/viewproj.h>
#include <sdk/sdk.h>
#include <settings.h>
#include <wallcheck/wallcheck.h>

namespace
{
	using clock = std::chrono::steady_clock;

	// ---- pure: begin (aimsim tests these as they are) ---------------------------------------

	// How a body moves, from its position read every tick. The game only moves it once per
	// frame, so a speed taken between two ticks read zero most of the time and a burst on the
	// tick it moved; the lead shook with it. Speed is taken across the last ~80 ms of real
	// changes (network wobble mostly cancels over that span, and a stop shows within it), then
	// lightly smoothed. A body that stops changing at all slows to a stop within a few frames.
	//
	// Anti-aim: the reported velocity isn't used at all (spoofing it does nothing). A body
	// snapped back and forth every frame ("jitter") is spotted by its direction reversing over
	// and over; then the aim goes to where it averages out over the last 150 ms, with no lead.
	// Only a jump of more than 25 studs (a respawn or a real teleport) starts the history over.
	struct motion_t
	{
		static constexpr int kHistory = 48;
		math::vector3 pos[kHistory]{};   // positions at each change, newest at head
		float at[kHistory]{};            // and when (seconds on this track's own clock)
		int head = -1, count = 0;
		float clock = 0.0f;
		math::vector3 vel{};
		math::vector3 last_step{};       // the previous change, for spotting reversals
		float reversals[8]{};            // when the last reversals were
		int rev_head = 0;
	};

	math::vector3 history_at(const motion_t& m, int back) { return m.pos[(m.head - back + motion_t::kHistory) % motion_t::kHistory]; }
	float history_time(const motion_t& m, int back) { return m.at[(m.head - back + motion_t::kHistory) % motion_t::kHistory]; }

	// Three or more direction reversals of half a stud or more within 0.4 s: jitter anti-aim.
	bool jittering(const motion_t& m)
	{
		int recent = 0;
		for (float t : m.reversals)
			recent += t > 0.0f && m.clock - t < 0.4f;
		return recent >= 3;
	}

	// While jittering: from the newest position to where the body averages out over 150 ms.
	math::vector3 settle(const motion_t& m)
	{
		if (m.count < 2 || !jittering(m))
			return {};
		math::vector3 sum{};
		int n = 0;
		for (int i = 0; i < m.count; ++i)
		{
			if (m.clock - history_time(m, i) > 0.15f && n > 0)
				break;
			sum = sum + history_at(m, i);
			++n;
		}
		return sum * (1.0f / n) - history_at(m, 0);
	}

	void track(motion_t& m, const math::vector3& pos, float dt)
	{
		m.clock += dt;
		if (m.count > 0)
		{
			const math::vector3 last = history_at(m, 0);
			if (pos.x == last.x && pos.y == last.y && pos.z == last.z)
			{
				// No new frame for longer than any frame takes: it has stopped (or isn't updating).
				if (m.clock - history_time(m, 0) > 0.05f)
					m.vel = m.vel * std::exp(-dt / 0.04f);
				return;
			}
			const math::vector3 step = pos - last;
			if (!std::isfinite(step.x) || !std::isfinite(step.y) || !std::isfinite(step.z) || step.length() > 25.0f)
			{
				m.count = 0;   // respawned or teleported: start over from here
				m.vel = {};
				m.last_step = {};
			}
			else
			{
				const float a = step.length(), b = m.last_step.length();
				if (a > 0.5f && b > 0.5f && step.dot(m.last_step) < -0.3f * a * b)
				{
					m.reversals[m.rev_head] = m.clock;
					m.rev_head = (m.rev_head + 1) % 8;
				}
				if (a > 0.05f)
					m.last_step = step;
			}
		}
		m.head = (m.head + 1) % motion_t::kHistory;
		m.pos[m.head] = pos;
		m.at[m.head] = m.clock;
		m.count = (std::min)(m.count + 1, motion_t::kHistory);

		if (jittering(m))
		{
			m.vel = m.vel * 0.5f;   // jitter has no honest speed to lead by
			return;
		}

		// The oldest change still within 80 ms (at least 30 ms back, or there's no speed yet).
		int oldest = -1;
		for (int i = 1; i < m.count; ++i)
		{
			if (m.clock - history_time(m, i) > 0.08f)
				break;
			oldest = i;
		}
		if (oldest < 0 || m.clock - history_time(m, oldest) < 0.03f)
			return;
		math::vector3 v = (pos - history_at(m, oldest)) * (1.0f / (m.clock - history_time(m, oldest)));
		if (v.length() > 120.0f)
			v = v * (120.0f / v.length());   // no one runs this fast: a snap, not a speed
		const float since = m.count > 1 ? m.clock - history_time(m, 1) : 0.0f;
		m.vel = m.vel + (v - m.vel) * (1.0f - std::exp(-(std::max)(since, 0.001f) / 0.03f));
	}

	// The lead for a velocity: `factor` seconds ahead, kept within a body's reach (8 studs
	// sideways, 3 up, 2.5 down), with vertical speed capped so a landing doesn't fling it.
	math::vector3 lead_for(math::vector3 vel, float factor_xz, float factor_y)
	{
		if (!std::isfinite(vel.x) || !std::isfinite(vel.y) || !std::isfinite(vel.z))
			return {};
		vel.y = std::clamp(vel.y, -16.0f, 24.0f);
		math::vector3 lead{ vel.x * factor_xz, vel.y * factor_y, vel.z * factor_xz };
		const float flat = std::sqrt(lead.x * lead.x + lead.z * lead.z);
		if (flat > 8.0f)
		{
			lead.x *= 8.0f / flat;
			lead.z *= 8.0f / flat;
		}
		lead.y = std::clamp(lead.y, -2.5f, 3.0f);
		return lead;
	}

	// Of `n` choices `dist` px from the crosshair, the one to aim at: `current` stays unless
	// another is nearer than `ratio` of its distance minus `margin` px; with no current, the
	// nearest. -1 when there are none.
	int keep_or_switch(int current, const float* dist, int n, float ratio, float margin)
	{
		int best = -1;
		for (int i = 0; i < n; ++i)
			if (best < 0 || dist[i] < dist[best])
				best = i;
		if (current < 0 || current >= n || best < 0 || best == current)
			return best;
		return dist[best] < dist[current] * ratio - margin ? best : current;
	}

	// ---- pure: end ---------------------------------------------------------------------------

	// The parts of each aim choice, in the order they're tried (R15 names, then R6; the player
	// cache gives Phantom Forces' parts these names too). Index 0 is "closest", which picks among
	// all of them.
	const std::vector<std::vector<const char*>>& part_kinds()
	{
		static const std::vector<std::vector<const char*>> kinds = {
			{},
			{ "Head" },
			{ "UpperTorso", "Torso", "HumanoidRootPart" },
			{ "LeftUpperArm", "Left Arm" },
			{ "RightUpperArm", "Right Arm" },
			{ "LeftUpperLeg", "Left Leg" },
			{ "RightUpperLeg", "Right Leg" },
		};
		return kinds;
	}

	struct part_hit_t
	{
		std::string name;
		math::vector3 pos{};
		math::vector2 screen{};
		float dist = FLT_MAX;
	};

	struct candidate_t
	{
		const cache::entity_t* entity = nullptr;
		part_hit_t part;
	};

	struct state_t
	{
		std::uint64_t target = 0;
		std::string part;
		clock::time_point last_tick{};
		clock::time_point target_seen{};
		std::unordered_map<std::uint64_t, motion_t> motion;   // by entity
		std::unordered_map<std::uint64_t, std::pair<clock::time_point, bool>> custom_knocked;
		std::string status = "No target";
	} g;
	std::mutex g_status_mtx;

	void set_status(const char* text)
	{
		std::lock_guard<std::mutex> lock(g_status_mtx);
		g.status = text;
	}

	// Da Hood style K.O. on a custom entity's model; read at most 4 times a second, it walks
	// the model's children.
	bool custom_knocked(const settings::custom_entities::custom_entity_t& ce, clock::time_point now)
	{
		auto& [at, value] = g.custom_knocked[ce.instance.address];
		if (now - at < std::chrono::milliseconds(250))
			return value;
		at = now;
		value = false;
		rbx::instance_t instance(ce.instance.address);
		rbx::instance_t model = instance.get_class_name() == "Model" ? instance : instance.find_first_child_by_class("Model");
		if (!model.address)
			return value;
		rbx::instance_t effects = rbx::model_instance_t(model.address).find_first_child("BodyEffects");
		if (!effects.address)
			return value;
		rbx::instance_t ko = effects.find_first_child("K.O");
		if (ko.address && ko.get_class_name() == "BoolValue")
			value = memory->read<bool>(ko.address + Offsets::Misc::Value);
		return value;
	}

	// Everyone who could be aimed at this tick: the players, then enabled custom entities.
	void collect(std::vector<cache::entity_t>& out, cache::entity_t& local, clock::time_point now)
	{
		{
			std::lock_guard<std::mutex> lock(cache::mtx);
			out = cache::cached_players;
			local = cache::cached_local_player;
		}
		std::lock_guard<std::mutex> lock(custom_entities::containers_mtx);
		for (const auto& container : settings::custom_entities::containers)
		{
			if (!container.enabled)
				continue;
			for (const auto& ce : container.entities)
			{
				if (!ce.enabled || !ce.instance.address)
					continue;
				if (settings::aimbot::knock_check && custom_knocked(ce, now))
					continue;
				cache::entity_t e{};
				e.instance = ce.instance;
				e.name = ce.name;
				e.display_name = ce.name;
				e.humanoid = rbx::humanoid_t{ 0 };
				e.health = 100.0f;
				e.max_health = 100.0f;
				for (const auto& [name, part] : ce.parts)
					if (part.part.address)
						e.parts[name] = part.part;
				if (!e.parts.empty())
					out.push_back(std::move(e));
			}
		}
	}

	bool valid(const cache::entity_t& e, const cache::entity_t& local, const math::vector3& local_root, bool has_local_root)
	{
		if (!e.instance.address || e.instance.address == local.instance.address)
			return false;
		if (settings::aimbot::teamcheck && e.team != 0 && e.team == local.team)
			return false;
		if (settings::rage::is_whitelisted(e.name))
			return false;
		if (!settings::rage::playerlist::target_name.empty() && e.name != settings::rage::playerlist::target_name)
			return false;
		if (e.humanoid.address)
		{
			if (settings::aimbot::knock_check && e.knocked)
				return false;
			if (settings::aimbot::health_check_enabled && e.health < settings::aimbot::min_health)
				return false;
			if (e.health <= 0.0f && e.max_health > 0.0f)
				return false;
		}
		if (settings::visuals::max_distance_enabled && has_local_root)
		{
			math::vector3 root{};
			if (aim_guard::root_of(e, root) && (root - local_root).length() > settings::visuals::max_distance)
				return false;
		}
		return true;
	}

	// One named part of `e`, read and put on screen.
	bool read_part(const cache::entity_t& e, const char* name, const math::matrix4& view, const math::vector2& dims,
		const math::vector2& origin, part_hit_t& out)
	{
		auto it = e.parts.find(name);
		if (it == e.parts.end() || !it->second.address)
			return false;
		math::vector3 pos{};
		if (!rig::position(it->second.address, pos) || !std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(pos.z)
			|| (pos.x == 0.0f && pos.y == 0.0f && pos.z == 0.0f))
			return false;
		math::vector2 s{};
		if (!game::visengine.world_to_screen(pos, s, dims, view))
			return false;
		out.name = name;
		out.pos = pos;
		out.screen = s;
		out.dist = std::sqrt((s.x - origin.x) * (s.x - origin.x) + (s.y - origin.y) * (s.y - origin.y));
		return true;
	}

	bool airborne(const cache::entity_t& e)
	{
		auto it = g.motion.find(e.instance.address);
		return it != g.motion.end() && it->second.vel.y > 4.0f;
	}

	// The part of `e` to aim at: the chosen kind, or with "closest" the kind nearest the
	// crosshair (keeping the one it had unless another is clearly nearer). Behind a wall, any
	// part that can be seen.
	bool pick_part(const cache::entity_t& e, bool is_current, const math::matrix4& view, const math::vector2& dims,
		const math::vector2& origin, bool walls, const math::vector3& camera, part_hit_t& out)
	{
		int kind = settings::aimbot::target_part;
		if (settings::aimbot::air_part_enabled && airborne(e))
			kind = settings::aimbot::air_part;
		const auto& kinds = part_kinds();
		if (kind < 0 || kind >= static_cast<int>(kinds.size()))
			kind = 0;

		auto visible = [&](const part_hit_t& h) { return !walls || wallcheck->is_visible(camera, h.pos); };

		std::vector<part_hit_t> hits;
		const int first = kind == 0 ? 1 : kind, last = kind == 0 ? static_cast<int>(kinds.size()) - 1 : kind;
		for (int k = first; k <= last; ++k)
			for (const char* name : kinds[k])
			{
				part_hit_t h;
				if (read_part(e, name, view, dims, origin, h))
				{
					if (visible(h))
						hits.push_back(h);
					break;   // the first part of a kind that exists stands for it
				}
			}

		if (hits.empty() && walls)
		{
			// The chosen part is behind something but the rest of them isn't (a head under an
			// arch, an arm in a doorway): any part that can be seen, nearest the crosshair.
			for (const auto& [name, part] : e.parts)
			{
				part_hit_t h;
				if (read_part(e, name.c_str(), view, dims, origin, h) && visible(h))
					hits.push_back(h);
			}
		}
		if (hits.empty())
			return false;

		std::vector<float> dist(hits.size());
		int current = -1;
		for (size_t i = 0; i < hits.size(); ++i)
		{
			dist[i] = hits[i].dist;
			if (is_current && hits[i].name == g.part)
				current = static_cast<int>(i);
		}
		const int chosen = keep_or_switch(current, dist.data(), static_cast<int>(hits.size()), 0.6f, 6.0f);
		out = hits[chosen];
		return true;
	}
}

bool aim_target::update(const math::matrix4& view, const math::vector2& dims, const math::vector2& origin, aim_t& out)
{
	const clock::time_point now = clock::now();
	const float dt = g.last_tick.time_since_epoch().count() ? std::clamp(std::chrono::duration<float>(now - g.last_tick).count(), 0.0f, 0.1f) : 0.0f;
	g.last_tick = now;

	std::vector<cache::entity_t> entities;
	cache::entity_t local;
	collect(entities, local, now);
	math::vector3 local_root{};
	const bool has_local_root = aim_guard::root_of(local, local_root);

	math::vector3 camera{};
	const bool has_camera = math::camera_from_viewproj(view, camera);
	const bool walls = settings::aimbot::wallcheck && has_camera;

	// Everyone's speed is kept up to date, so a target switched to already has its lead.
	for (const cache::entity_t& e : entities)
	{
		math::vector3 root{};
		if (e.instance.address && aim_guard::root_of(e, root))
			track(g.motion[e.instance.address], root, dt);
	}

	std::vector<candidate_t> candidates;
	candidates.reserve(entities.size());
	bool current_alive = false;
	for (const cache::entity_t& e : entities)
	{
		if (!valid(e, local, local_root, has_local_root))
			continue;
		const bool is_current = e.instance.address == g.target;
		current_alive = current_alive || is_current;
		candidate_t c;
		c.entity = &e;
		if (!pick_part(e, is_current, view, dims, origin, walls, camera, c.part))
			continue;
		// FOV: the current target gets a little slack at the edge, so it isn't dropped and
		// picked up again as it crosses the circle.
		const float fov = settings::aimbot::fov * (is_current ? 1.1f : 1.0f);
		if (settings::aimbot::use_fov && c.part.dist > fov)
			continue;
		candidates.push_back(c);
	}

	int current = -1;
	std::vector<float> dist(candidates.size());
	for (size_t i = 0; i < candidates.size(); ++i)
	{
		dist[i] = candidates[i].part.dist;
		if (candidates[i].entity->instance.address == g.target)
			current = static_cast<int>(i);
	}

	// The target ducked out of view or behind a wall for a moment: wait for it rather than
	// swinging to someone else and back.
	if (current < 0 && current_alive && g.target && now - g.target_seen < std::chrono::milliseconds(150))
	{
		set_status("Waiting for the target to show again");
		return false;
	}

	const int chosen = settings::aimbot::sticky_aim && current >= 0
		? current
		: keep_or_switch(current, dist.data(), static_cast<int>(candidates.size()), 0.6f, 10.0f);
	if (chosen < 0)
	{
		g.target = 0;
		g.part.clear();
		set_status("No target in view");
		return false;
	}

	const candidate_t& c = candidates[chosen];
	const cache::entity_t& e = *c.entity;
	g.target = e.instance.address;
	g.part = c.part.name;
	g.target_seen = now;

	out.target = e;
	out.part = c.part.name;
	out.body = c.part.pos;
	out.distance_px = c.part.dist;
	const motion_t& motion = g.motion[e.instance.address];
	const bool jitter = jittering(motion);
	out.point = c.part.pos + settle(motion);
	if (settings::aimbot::enable_prediction && !jitter)
	{
		const bool air = settings::aimbot::air_prediction_enabled && airborne(e);
		const float fx = (20.0f - (air ? settings::aimbot::air_prediction_x : settings::aimbot::prediction_x)) * 0.02f;
		const float fy = (20.0f - (air ? settings::aimbot::air_prediction_y : settings::aimbot::prediction_y)) * 0.02f;
		out.point = out.point + lead_for(motion.vel, fx, fy);
	}
	out.screen = c.part.screen;
	game::visengine.world_to_screen(out.point, out.screen, dims, view);

	// Teleported, respawned, or a bad read: leave them a moment instead of whipping across.
	math::vector3 root{};
	// (Jitter anti-aim snaps far enough to look like one; it's handled by settling instead.)
	const bool steady = aim_guard::root_of(e, root) && aim_guard::steady(e.instance.address, root);
	out.hold = (!steady && !jitter) || (has_camera && !aim_guard::plausible(out.point, camera));
	if (out.hold)
		set_status("Target moved too fast (teleport?), waiting");
	else
		set_status(jitter ? "Locked on (target is jittering: aiming at its average)" : "Locked on");
	return true;
}

void aim_target::reset()
{
	g.target = 0;
	g.part.clear();
	g.motion.clear();
	g.last_tick = {};
	set_status("No target");
}

std::string aim_target::status()
{
	std::lock_guard<std::mutex> lock(g_status_mtx);
	return g.status;
}
