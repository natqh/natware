#include "pf_mouse.h"
#include "aimbot.h"
#include "aim_guard.h"
#include "physical_mouse.h"
#include "aim_log.h"

#include <windows.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <cache/cache.h>
#include <features/esp/rig.h>
#include <game/game.h>
#include <sdk/math/viewproj.h>
#include <settings.h>
#include <wallcheck/wallcheck.h>

namespace
{
	using clock = std::chrono::steady_clock;

	// ---- how the camera turns -------------------------------------------------------------

	// Radians the camera turns per mouse count (x, y), learned from the view after each move. PF
	// has its own sensitivity and an aim-down-sights multiplier, so it's kept per field of view
	// (in 4-degree steps): hip fire and every scope each get their own.
	struct gain_t
	{
		float rad_per_count[2] = { 0.0f, 0.0f };
		int samples = 0;
		float sxy = 0.0f, sxx = 0.0f;   // running least squares: turn seen against counts sent
	};
	std::map<int, gain_t> g_gains;
	constexpr float kStartGuess = 0.0016f;

	gain_t& gain_for(int fov_bucket)
	{
		auto it = g_gains.find(fov_bucket);
		if (it != g_gains.end())
			return it->second;
		// A new scope starts from the nearest one already learned (or a plain guess).
		gain_t g;
		g.rad_per_count[0] = g.rad_per_count[1] = kStartGuess;
		int best = 1 << 30;
		for (const auto& [bucket, known] : g_gains)
			if (known.samples > 0 && std::abs(bucket - fov_bucket) < best)
			{
				best = std::abs(bucket - fov_bucket);
				g.rad_per_count[0] = known.rad_per_count[0];
				g.rad_per_count[1] = known.rad_per_count[1];
			}
		return g_gains.emplace(fov_bucket, g).first->second;
	}

	// The vertical focal length in pixels: the projection's row 1 is f times the camera's up
	// vector and row 3 is the (unit) forward vector.
	float focal_px(const math::matrix4& vp, const math::vector2& dims)
	{
		const math::vector3 r1{ vp.m[1][0], vp.m[1][1], vp.m[1][2] };
		const math::vector3 r3{ vp.m[3][0], vp.m[3][1], vp.m[3][2] };
		const float w = r3.length();
		return w > 1e-6f ? r1.length() / w * dims.y * 0.5f : 0.0f;
	}

	int fov_bucket(float focal, const math::vector2& dims)
	{
		const float fov_deg = 2.0f * std::atan(dims.y * 0.5f / focal) * 57.29578f;
		return static_cast<int>(std::lround(fov_deg / 4.0f));
	}

	// How far the view turned between two matrices: the angles (right and down positive) from
	// the old centre of view to the new one, measured where the old centre lands in the new view.
	bool view_turn(const math::matrix4& before, const math::matrix4& after, const math::vector2& dims, float& turn_x, float& turn_y)
	{
		math::vector3 origin{}, dir{};
		if (!math::ray_from_viewproj(before, 0.0f, 0.0f, origin, dir))
			return false;
		float nx = 0.0f, ny = 0.0f;
		if (!math::ndc_of(after, origin + dir * 500.0f, nx, ny))
			return false;
		const float f = focal_px(after, dims);
		if (f <= 0.0f)
			return false;
		// The old centre now sits at (px, py) from the middle: the view turned the other way.
		turn_x = -std::atan(nx * dims.x * 0.5f / f);
		turn_y = std::atan(ny * dims.y * 0.5f / f);
		return std::isfinite(turn_x) && std::isfinite(turn_y);
	}

	// How much of the remaining angle to close per frame: careful on the first moves of a new
	// sensitivity (the start is a guess), then most of it.
	float step_for(const gain_t& g)
	{
		return g.samples == 0 ? 0.3f : g.samples < 3 ? 0.6f : 0.85f;
	}

	// The counts for this frame: `want` radians to the target per axis, closed by `step`.
	void plan(const gain_t& g, const float want[2], const float step[2], float rem[2], int move[2])
	{
		for (int a = 0; a < 2; ++a)
		{
			// Never more than 10 degrees in one move, whatever the gain says: a big gap closes
			// over a few frames instead of in one jerk.
			const float angle = std::clamp(want[a] * step[a], -0.1745f, 0.1745f);
			rem[a] += angle / g.rad_per_count[a];
			move[a] = std::clamp(static_cast<int>(std::lround(rem[a])), -2000, 2000);
			rem[a] = std::clamp(rem[a] - static_cast<float>(move[a]), -1.0f, 1.0f);
		}
	}

	// ---- steering ---------------------------------------------------------------------------
	// A correction goes out every new frame, minus the moves the view hasn't shown yet. The view
	// natware reads trails the camera by a frame or more: sending the full correction again
	// before the last one showed doubled it and threw the aim sideways, and waiting for each move
	// to show halved how often it corrects. So each move is kept until a frame turns the view
	// its way (that frame is when it showed, and how far it turned teaches the sensitivity), or
	// until it's older than moves take to show. Guessing the delay instead was worse either way:
	// too short and the aim rings around the target, too long and it swings off it.
	struct pending_t
	{
		int counts[2] = { 0, 0 };
		float age = 0.0f;   // seconds since it was sent
	};
	constexpr int kPending = 8;

	struct controller_t
	{
		math::matrix4 last_view{};
		bool has_view = false;
		pending_t pending[kPending];   // oldest first
		int count = 0;
		bool sent_now = false;         // the newest entry was planned this frame
		float delay = 0.05f;           // seconds a move takes to show, learned (starts long: see expire)
		float since_frame = 0.0f;      // seconds since the last new frame
		float since_fire = 1.0f;       // seconds since you last fired
		float drift[2] = { 0.0f, 0.0f };   // rad/s the view turns on its own while you fire (recoil)
		float rem[2] = { 0.0f, 0.0f };
		int bucket = -1;
	};

	void pop_front(controller_t& c)
	{
		for (int i = 1; i < c.count; ++i)
			c.pending[i - 1] = c.pending[i];
		--c.count;
	}

	// Moves older than moves take to show are taken as shown. The wait is generous: a move given
	// up on too early is sent again, and the delay is only learned from moves seen landing.
	// While you fire, a shot's kick can hide a move landing: then it's taken as shown on time.
	void expire(controller_t& c, bool recoil)
	{
		const float limit = recoil ? c.delay - 0.004f : (std::min)((std::max)(c.delay + 0.02f, c.delay * 2.0f), 0.15f);
		while (c.count > 0 && c.pending[0].age >= limit)
			pop_front(c);
	}

	// The turn between two frames: which moves it shows (their turn, in radians, goes to
	// `shown_rad`), and what that says about sensitivity.
	void take_turn(controller_t& c, const float turn[2], bool learn, float shown_rad[2])
	{
		gain_t& g = gain_for(c.bucket);
		float left[2] = { turn[0], turn[1] };
		float shown[2] = { 0.0f, 0.0f };
		shown_rad[0] = shown_rad[1] = 0.0f;
		bool first = true;
		while (c.count > 0)
		{
			const pending_t& p = c.pending[0];
			const float pred[2] = { p.counts[0] * g.rad_per_count[0], p.counts[1] * g.rad_per_count[1] };
			const float pp = pred[0] * pred[0] + pred[1] * pred[1];
			if (pp <= 0.0f)
			{
				pop_front(c);
				continue;
			}
			// It showed if the view turned at least a little its way (the sensitivity may still be
			// far off). A second move only counts in the same frame if it's overdue (a skipped frame).
			const float along = (left[0] * pred[0] + left[1] * pred[1]) / pp;
			if (along < 0.1f || (!first && p.age < c.delay + 0.008f))
				break;
			// Quick to learn a shorter delay, slow to believe a longer one: a move seen late (out
			// of order, or under your own hand) must not stretch how long moves are waited on.
			if (learn)
				c.delay = std::clamp(c.delay + (p.age - c.delay) * (p.age < c.delay ? 0.3f : 0.05f), 0.0f, 0.1f);
			shown[0] += static_cast<float>(p.counts[0]);
			shown[1] += static_cast<float>(p.counts[1]);
			shown_rad[0] += pred[0];
			shown_rad[1] += pred[1];
			left[0] -= pred[0];
			left[1] -= pred[1];
			pop_front(c);
			first = false;
		}
		if (!learn || first)
			return;
		g.sxy = g.sxy * 0.85f + shown[0] * turn[0] + shown[1] * turn[1];
		g.sxx = g.sxx * 0.85f + shown[0] * shown[0] + shown[1] * shown[1];
		if (g.sxx < 64.0f || g.sxy <= 0.0f)
			return;   // too little sent yet to measure
		const float r = g.sxy / g.sxx;
		if (!std::isfinite(r) || r < 1e-5f || r > 0.05f)
			return;
		g.rad_per_count[0] = g.rad_per_count[1] = r;
		++g.samples;
	}

	// Takes in the view (call every tick, `dt` seconds apart). True on a new frame: time to steer.
	// `firing`: recoil is turning the view. `hand`: you moved the mouse yourself since last time.
	bool observe(controller_t& c, const math::matrix4& view, const math::vector2& dims, float dt, bool firing, bool hand)
	{
		c.since_frame += dt;
		c.sent_now = false;
		for (int i = 0; i < c.count; ++i)
			c.pending[i].age += dt;
		// Recoil keeps showing for as long as the view trails the camera after you let go.
		c.since_fire = firing ? 0.0f : c.since_fire + dt;
		const bool recoil = c.since_fire < c.delay + 0.02f;
		if (!recoil)
			c.drift[0] = c.drift[1] = 0.0f;
		const bool changed = c.has_view && std::memcmp(&view, &c.last_view, sizeof(view)) != 0;
		if (changed && c.bucket >= 0)
		{
			float turn[2] = { 0.0f, 0.0f };
			if (view_turn(c.last_view, view, dims, turn[0], turn[1]))
			{
				float shown[2];
				if (recoil && c.since_frame > 0.0f)
				{
					// Recoil turns the view on its own, against the moves: take out what it's been
					// doing to see which moves landed, and what's left over is the recoil now. Its
					// rate is kept so the aim leads it (otherwise it trails the kick by the frames
					// a move takes to show). Nothing is learned from it.
					const float own[2] = { turn[0] - c.drift[0] * c.since_frame, turn[1] - c.drift[1] * c.since_frame };
					take_turn(c, own, false, shown);
					for (int a = 0; a < 2; ++a)
					{
						const float rate = std::clamp((turn[a] - shown[a]) / c.since_frame, -3.0f, 3.0f);
						c.drift[a] += (rate - c.drift[a]) * 0.15f;   // over a few shots: kicks come and go
					}
				}
				else
					take_turn(c, turn, !hand && !recoil, shown);
			}
		}
		expire(c, recoil);
		if (c.has_view && !changed)
		{
			// A still view is either the same frame or a frame where nothing turned. With nothing
			// on its way there's nothing to wait for, so after a frame's time it counts as one:
			// the target can still move while you stand still.
			if (c.count > 0 || c.since_frame < 0.016f)
				return false;
		}
		c.last_view = view;
		c.has_view = true;
		c.since_frame = 0.0f;
		return true;
	}

	// The counts to send now toward `want` radians, with `step` of the remainder closed.
	void steer(controller_t& c, gain_t& g, int bucket, const float want[2], const float step[2], int move[2])
	{
		c.bucket = bucket;
		// Where the target will be when this move shows: recoil keeps lifting the view until then.
		float rest[2] = { want[0] - c.drift[0] * c.delay, want[1] - c.drift[1] * c.delay };
		for (int i = 0; i < c.count; ++i)
			for (int a = 0; a < 2; ++a)
				rest[a] -= c.pending[i].counts[a] * g.rad_per_count[a];
		plan(g, rest, step, c.rem, move);
		if (move[0] == 0 && move[1] == 0)
			return;
		if (c.count == kPending)
			pop_front(c);
		pending_t& p = c.pending[c.count++];
		p.counts[0] = move[0];
		p.counts[1] = move[1];
		p.age = 0.0f;
		c.sent_now = true;
	}

	// The move planned this frame didn't go out after all.
	void unsend(controller_t& c)
	{
		if (c.sent_now && c.count > 0)
			--c.count;
		c.sent_now = false;
	}

	controller_t g_ctrl;
	clock::time_point g_last_tick{};
	clock::time_point g_last_steer{};
	std::uint32_t g_hand_seen = 0;

	std::string g_status;
}

void pf_mouse::reset()
{
	g_ctrl = controller_t{};
}

void pf_mouse::steer_to(const math::vector2& target, const math::vector2& origin, const math::matrix4& view, const math::vector2& dims)
{
	physical_mouse::start();
	const clock::time_point now = clock::now();
	const float dt = g_last_tick.time_since_epoch().count() ? std::clamp(std::chrono::duration<float>(now - g_last_tick).count(), 0.0f, 0.05f) : 0.0f;
	g_last_tick = now;

	const float f = focal_px(view, dims);
	if (f <= 0.0f)
		return;

	// Your own hand or recoil in this stretch: don't learn from it.
	const std::uint32_t hand = physical_mouse::moves();
	const bool firing = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
	const bool hand_moved = hand != g_hand_seen;
	g_hand_seen = hand;

	if (!observe(g_ctrl, view, dims, dt, firing, hand_moved))
		return;   // same frame as last time: the last move is still on its way

	const float dx = target.x - origin.x;
	const float dy = target.y - origin.y;
	if (dx * dx + dy * dy < 1.5f * 1.5f)
	{
		g_ctrl.rem[0] = g_ctrl.rem[1] = 0.0f;
		return;   // on target
	}

	const int bucket = fov_bucket(f, dims);
	gain_t& g = gain_for(bucket);
	float step[2] = { step_for(g), step_for(g) };
	if (settings::aimbot::smoothing)
	{
		const float since = std::clamp(std::chrono::duration<float>(now - g_last_steer).count(), 0.001f, 0.1f);
		const float tx = std::clamp(1.0f / settings::aimbot::smoothingx, 0.01f, 1.0f);
		const float ty = std::clamp(1.0f / settings::aimbot::smoothingy, 0.01f, 1.0f);
		step[0] = (std::min)(step[0], 1.0f - std::pow(1.0f - tx, since * 60.0f));   // per 60 Hz frame
		step[1] = (std::min)(step[1], 1.0f - std::pow(1.0f - ty, since * 60.0f));
	}
	g_last_steer = now;

	const float want[2] = { std::atan(dx / f), std::atan(dy / f) };
	int move[2] = { 0, 0 };
	steer(g_ctrl, g, bucket, want, step, move);
	if (move[0] == 0 && move[1] == 0)
		return;

	// Only into Roblox, and only while it's in front.
	if (GetForegroundWindow() != game::get_roblox_window())
	{
		unsend(g_ctrl);   // nothing was sent after all
		return;
	}
	INPUT input{};
	input.type = INPUT_MOUSE;
	input.mi.dx = move[0];
	input.mi.dy = move[1];
	input.mi.dwFlags = MOUSEEVENTF_MOVE;
	SendInput(1, &input, sizeof(INPUT));
	aim_log::note_move(move[0], move[1]);

	char line[128];
	snprintf(line, sizeof(line), "Locked on, sensitivity %s", g.samples >= 3 ? "learned" : "still learning");
	g_status = line;
}

std::string pf_mouse::status()
{
	if (settings::aimbot::mode != 0)
		return "";
	if (g_gains.empty())
		return "Learning your sensitivity from the first few moves";
	return g_status;
}
