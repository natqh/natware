#include "aim_guard.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <unordered_map>

#include <features/esp/rig.h>

namespace
{
	using clock = std::chrono::steady_clock;

	struct track_t
	{
		math::vector3 pos{};
		clock::time_point at{};          // last time it was looked at
		clock::time_point changed_at{};  // last time its position actually changed
		clock::time_point hold_until{};
	};

	// One set per thread (the aimbot's and silent aim's each keep their own), so no lock.
	thread_local std::unordered_map<std::uint64_t, track_t> g_tracks;
	thread_local clock::time_point g_last_sweep{};

	bool finite3(const math::vector3& v)
	{
		return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
	}

	// Faster than anyone runs, falls or flies in a game (studs per second). Positions arrive in
	// network steps, so speed is measured between real changes, not between 1 ms ticks.
	constexpr float kMaxSpeed = 150.0f;
	constexpr float kMinJump = 8.0f;
	// How long a target that just jumped is left alone.
	constexpr auto kHold = std::chrono::milliseconds(220);
}

bool aim_guard::root_of(const cache::entity_t& entity, math::vector3& out)
{
	for (const char* name : { "HumanoidRootPart", "UpperTorso", "Torso", "Head" })
	{
		auto it = entity.parts.find(name);
		if (it == entity.parts.end() || !it->second.address)
			continue;
		if (rig::position(it->second.address, out) && finite3(out) && !(out.x == 0.0f && out.y == 0.0f && out.z == 0.0f))
			return true;
	}
	return false;
}

bool aim_guard::steady(std::uint64_t entity, const math::vector3& root)
{
	const clock::time_point now = clock::now();
	if (now - g_last_sweep > std::chrono::seconds(10))
	{
		for (auto it = g_tracks.begin(); it != g_tracks.end();)
			it = now - it->second.at > std::chrono::seconds(10) ? g_tracks.erase(it) : std::next(it);
		g_last_sweep = now;
	}

	track_t& t = g_tracks[entity];
	const bool known = t.at.time_since_epoch().count() != 0 && now - t.at < std::chrono::milliseconds(500);
	const float moved = known ? (root - t.pos).length() : 0.0f;
	if (known && moved > 0.01f)
	{
		// Live positions update at least every 0.1 s, so a big step from someone standing still
		// is measured over that, not over the whole time they stood there.
		const float dt = std::clamp(std::chrono::duration<float>(now - t.changed_at).count(), 0.016f, 0.1f);
		// A jump of several studs faster than anyone moves: a teleport, a respawn or a bad read.
		if (moved > kMinJump && moved / dt > kMaxSpeed)
			t.hold_until = now + kHold;
	}
	if (!known || moved > 0.01f)
	{
		t.pos = root;
		t.changed_at = now;
	}
	t.at = now;
	return now >= t.hold_until;
}

bool aim_guard::plausible(const math::vector3& point, const math::vector3& camera)
{
	if (!finite3(point) || (point.x == 0.0f && point.y == 0.0f && point.z == 0.0f))
		return false;
	return (point - camera).length() < 4000.0f;
}
