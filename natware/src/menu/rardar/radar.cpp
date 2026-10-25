#include "radar.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <vector>

#include <imgui/imgui.h>

#include <cache/cache.h>
#include <game/game.h>
#include <memory/memory.h>
#include <settings.h>
#include <offsets/offsets.hpp>
#include <wallcheck/wallcheck.h>

namespace
{
	static void xz_basis_from_camera(const math::matrix3& rot, math::vector3& out_fwd, math::vector3& out_rgt)
	{
		math::vector3 f = rot.forward();
		f.y = 0.f;
		float fl = std::sqrtf(f.x * f.x + f.z * f.z);
		if (fl < 1e-4f)
			out_fwd = { 0.f, 0.f, -1.f };
		else
			out_fwd = { f.x / fl, 0.f, f.z / fl };

		math::vector3 r = rot.right();
		r.y = 0.f;
		float rl = std::sqrtf(r.x * r.x + r.z * r.z);
		if (rl < 1e-4f)
			out_rgt = { 1.f, 0.f, 0.f };
		else
			out_rgt = { r.x / rl, 0.f, r.z / rl };
	}

	static ImVec2 world_to_radar_pt(const ImVec2& center, float rad, float range,
		const math::vector3& local,
		const math::vector3& fwd_xz, const math::vector3& rgt_xz,
		const math::vector3& world, bool use_3d)
	{
		math::vector3 d = world - local;
		float side = d.dot(rgt_xz);
		float ahead = d.dot(fwd_xz);
		float rx = side / range * rad;
		float ry = -ahead / range * rad;
		if (use_3d)
			ry -= (d.y / range) * rad * 0.4f;
		return ImVec2(center.x + rx, center.y + ry);
	}

	static bool in_square(ImVec2 rmin, ImVec2 rmax, const ImVec2& p)
	{
		return p.x >= rmin.x && p.x <= rmax.x && p.y >= rmin.y && p.y <= rmax.y;
	}

	static bool clip_segment_to_rect(ImVec2 rmin, ImVec2 rmax, ImVec2 a, ImVec2 b, ImVec2& out_a, ImVec2& out_b)
	{
		float t0 = 0.f;
		float t1 = 1.f;
		const float dx = b.x - a.x;
		const float dy = b.y - a.y;

		auto clip_dim = [&](float p, float q) -> bool {
			if (std::fabs(p) < 1e-8f)
				return q >= 0.f;
			const float t = q / p;
			if (p < 0.f)
			{
				if (t > t1) return false;
				if (t > t0) t0 = t;
			}
			else
			{
				if (t < t0) return false;
				if (t < t1) t1 = t;
			}
			return true;
			};

		if (!clip_dim(-dx, a.x - rmin.x)) return false;
		if (!clip_dim(dx, rmax.x - a.x)) return false;
		if (!clip_dim(-dy, a.y - rmin.y)) return false;
		if (!clip_dim(dy, rmax.y - a.y)) return false;

		out_a = ImVec2(a.x + t0 * dx, a.y + t0 * dy);
		out_b = ImVec2(a.x + t1 * dx, a.y + t1 * dy);
		return true;
	}

	static ImVec2 intersect_lines(ImVec2 p1, ImVec2 p2, ImVec2 q1, ImVec2 q2)
	{
		const float dx1 = p2.x - p1.x;
		const float dy1 = p2.y - p1.y;
		const float dx2 = q2.x - q1.x;
		const float dy2 = q2.y - q1.y;
		float den = dx1 * dy2 - dy1 * dx2;
		if (std::fabs(den) < 1e-8f)
			return p2;
		const float t = ((q1.x - p1.x) * dy2 - (q1.y - p1.y) * dx2) / den;
		return ImVec2(p1.x + t * dx1, p1.y + t * dy1);
	}

	static void clip_polygon_halfplane_ccw(std::vector<ImVec2>& poly, ImVec2 A, ImVec2 B)
	{
		const ImVec2 de(B.x - A.x, B.y - A.y);
		auto inside = [&](ImVec2 p) {
			const ImVec2 d(p.x - A.x, p.y - A.y);
			return de.x * d.y - de.y * d.x >= -1e-3f;
			};

		std::vector<ImVec2> out;
		const size_t n = poly.size();
		if (n == 0)
			return;
		for (size_t i = 0; i < n; ++i)
		{
			const ImVec2 cur = poly[i];
			const ImVec2 prev = poly[(i + n - 1) % n];
			const bool inc = inside(cur);
			const bool inp = inside(prev);
			if (inc)
			{
				if (!inp)
					out.push_back(intersect_lines(prev, cur, A, B));
				out.push_back(cur);
			}
			else if (inp)
				out.push_back(intersect_lines(prev, cur, A, B));
		}
		poly.swap(out);
	}

	static void clip_polygon_to_rect(std::vector<ImVec2>& poly, ImVec2 rmin, ImVec2 rmax)
	{
		if (poly.size() < 3)
		{
			poly.clear();
			return;
		}
		clip_polygon_halfplane_ccw(poly, ImVec2(rmin.x, rmax.y), ImVec2(rmin.x, rmin.y));
		if (poly.size() < 3) { poly.clear(); return; }
		clip_polygon_halfplane_ccw(poly, ImVec2(rmax.x, rmin.y), ImVec2(rmax.x, rmax.y));
		if (poly.size() < 3) { poly.clear(); return; }
		clip_polygon_halfplane_ccw(poly, ImVec2(rmin.x, rmin.y), ImVec2(rmax.x, rmin.y));
		if (poly.size() < 3) { poly.clear(); return; }
		clip_polygon_halfplane_ccw(poly, ImVec2(rmax.x, rmax.y), ImVec2(rmin.x, rmax.y));
		if (poly.size() < 3) poly.clear();
	}

	static float polygon_area_abs(const std::vector<ImVec2>& p)
	{
		const size_t n = p.size();
		if (n < 3)
			return 0.f;
		double s = 0.0;
		for (size_t i = 0; i < n; ++i)
		{
			const ImVec2& a = p[i];
			const ImVec2& b = p[(i + 1) % n];
			s += (double)a.x * (double)b.y - (double)a.y * (double)b.x;
		}
		return static_cast<float>(std::fabs(s * 0.5));
	}

	static bool imvec2_less(const ImVec2& a, const ImVec2& b)
	{
		if (a.x < b.x) return true;
		if (a.x > b.x) return false;
		return a.y < b.y;
	}

	static float cross_oab(const ImVec2& o, const ImVec2& a, const ImVec2& b)
	{
		return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
	}

	static std::vector<ImVec2> convex_hull(std::vector<ImVec2> pts)
	{
		const float eps = 1e-3f;
		std::sort(pts.begin(), pts.end(), imvec2_less);
		(void)pts.erase(std::unique(pts.begin(), pts.end(),
			[eps](const ImVec2& a, const ImVec2& b) {
				return std::fabsf(a.x - b.x) < eps && std::fabsf(a.y - b.y) < eps;
			}), pts.end());

		if (pts.size() <= 1)
			return pts;

		std::vector<ImVec2> lower;
		for (const ImVec2& p : pts)
		{
			while (lower.size() >= 2 &&
				cross_oab(lower[lower.size() - 2], lower[lower.size() - 1], p) <= 1e-5f)
				lower.pop_back();
			lower.push_back(p);
		}
		std::vector<ImVec2> upper;
		for (int i = (int)pts.size() - 1; i >= 0; --i)
		{
			const ImVec2& p = pts[(size_t)i];
			while (upper.size() >= 2 &&
				cross_oab(upper[upper.size() - 2], upper[upper.size() - 1], p) <= 1e-5f)
				upper.pop_back();
			upper.push_back(p);
		}
		lower.pop_back();
		upper.pop_back();
		lower.insert(lower.end(), upper.begin(), upper.end());
		return lower;
	}

	static void ensure_min_visibility(int& r, int& g, int& b)
	{
		const int lum = (r * 30 + g * 59 + b * 11) / 100;
		if (lum >= 25)
			return;
		float boost = 28.f / (float)((std::max)(lum, 1));
		if (boost > 3.f) boost = 3.f;
		r = (std::min)(255, static_cast<int>(r * boost));
		g = (std::min)(255, static_cast<int>(g * boost));
		b = (std::min)(255, static_cast<int>(b * boost));
	}

	static bool obb_is_floorish(const rbx::obb& box)
	{
		const float hx = box.half_size.x;
		const float hy = box.half_size.y;
		const float hz = box.half_size.z;
		const float hmn = (std::min)((std::min)(hx, hy), hz);
		const float hmx = (std::max)((std::max)(hx, hy), hz);
		if (hmx < 1e-4f)
			return false;
		return (hmn / hmx) < 0.22f;
	}

	struct map_piece_t
	{
		float dist_sq{};
		bool floorish{};
		std::vector<ImVec2> clipped_fill{};
		ImU32 fill_col{};
		ImU32 stroke_col{};
		float stroke_thick{ 1.25f };
		ImVec2 corners[8]{};
	};
}

void radar::run()
{
	if (!settings::visuals::radar_enabled)
		return;

	ImDrawList* draw = ImGui::GetBackgroundDrawList();
	const float radar_half = settings::visuals::radar_size;
	ImVec2 display = ImGui::GetIO().DisplaySize;
	ImVec2 center(display.x - radar_half - 24.f, radar_half + 24.f);
	ImVec2 rmin(center.x - radar_half, center.y - radar_half);
	ImVec2 rmax(center.x + radar_half, center.y + radar_half);

	math::vector3 local_pos{};
	bool has_lp = false;
	{
		std::lock_guard<std::mutex> lock(cache::mtx);
		if (cache::cached_local_player.instance.address)
		{
			auto it = cache::cached_local_player.parts.find("HumanoidRootPart");
			if (it != cache::cached_local_player.parts.end())
			{
				rbx::primitive_t prim = it->second.get_primitive();
				if (prim.address)
				{
					local_pos = prim.get_position();
					has_lp = std::isfinite(local_pos.x) &&
						std::isfinite(local_pos.y) &&
						std::isfinite(local_pos.z);
				}
			}
		}
	}

	if (!has_lp && game::camera)
	{
		local_pos = memory->read<math::vector3>(game::camera + Offsets::Camera::Position);
		has_lp = std::isfinite(local_pos.x) &&
			std::isfinite(local_pos.y) &&
			std::isfinite(local_pos.z);
	}

	if (!has_lp || !game::camera)
		return;

	math::matrix3 cam_rot = memory->read<math::matrix3>(game::camera + Offsets::Camera::Rotation);
	math::vector3 fwd{};
	math::vector3 rgt{};
	xz_basis_from_camera(cam_rot, fwd, rgt);

	const float range = 180.f;

	draw->AddRectFilled(rmin, rmax, IM_COL32(52, 48, 44, 210));

	// Held for the whole draw: the wall check's thread may swap in a newer map meanwhile.
	static const std::vector<rbx::obb> no_geom;
	static const std::vector<std::uint32_t> no_colors;
	const std::shared_ptr<const wallcheck_snapshot_t> map_snap = wallcheck->snapshot();
	const std::vector<rbx::obb>& geom = map_snap ? map_snap->radar_geometry : no_geom;
	const std::vector<std::uint32_t>& geom_colors = map_snap ? map_snap->radar_geometry_colors : no_colors;

	std::vector<map_piece_t> pieces;
	pieces.reserve(geom.size());

	for (std::size_t oi = 0; oi < geom.size(); ++oi)
	{
		const rbx::obb& box = geom[oi];
		const float dx = box.center.x - local_pos.x;
		const float dz = box.center.z - local_pos.z;
		const float hmn = (std::min)((std::min)(box.half_size.x, box.half_size.y), box.half_size.z);
		const float hmx = (std::max)((std::max)(box.half_size.x, box.half_size.y), box.half_size.z);
		float cull_mult = 1.45f;
		if (hmn <= 9.f && hmx >= 80.f)
			cull_mult = 3.6f;
		else if (hmn <= 9.f && hmx >= 28.f)
			cull_mult = 2.4f;
		const float cull_r = range * cull_mult;
		if (dx * dx + dz * dz > cull_r * cull_r)
			continue;

		ImU32 wall_argb = IM_COL32(130, 130, 140, 255);
		if (oi < geom_colors.size())
			wall_argb = static_cast<ImU32>(geom_colors[oi]);

		int r = static_cast<int>(wall_argb & 0xFFu);
		int g = static_cast<int>((wall_argb >> 8) & 0xFFu);
		int b = static_cast<int>((wall_argb >> 16) & 0xFFu);
		ensure_min_visibility(r, g, b);

		const bool fl = obb_is_floorish(box);
		const int fill_a = fl ? 195 : 170;
		const int stroke_a = fl ? 130 : 200;
		const ImU32 fill_col = IM_COL32(r, g, b, fill_a);
		const int sd = fl ? 82 : 75;
		const ImU32 stroke_col = IM_COL32(
			(std::max)(0, (std::min)(255, r * sd / 100)),
			(std::max)(0, (std::min)(255, g * sd / 100)),
			(std::max)(0, (std::min)(255, b * sd / 100)),
			stroke_a);

		math::vector3 wcorners[8];
		for (int i = 0; i < 8; ++i)
		{
			float sx = ((i & 1) != 0) ? 1.f : -1.f;
			float sy = ((i & 2) != 0) ? 1.f : -1.f;
			float sz = ((i & 4) != 0) ? 1.f : -1.f;
			wcorners[i] = box.center
				+ box.axes[0] * (box.half_size.x * sx)
				+ box.axes[1] * (box.half_size.y * sy)
				+ box.axes[2] * (box.half_size.z * sz);
		}

		map_piece_t mp{};
		mp.dist_sq = dx * dx + dz * dz;
		mp.floorish = fl;
		mp.fill_col = fill_col;
		mp.stroke_col = stroke_col;
		mp.stroke_thick = fl ? 0.9f : 1.25f;
		std::vector<ImVec2> hull_in;
		hull_in.reserve(8);
		for (int i = 0; i < 8; ++i)
		{
			ImVec2 rp = world_to_radar_pt(center, radar_half, range, local_pos, fwd, rgt, wcorners[i], true);
			mp.corners[i] = rp;
			hull_in.push_back(rp);
		}

		std::vector<ImVec2> hull = convex_hull(std::move(hull_in));
		bool hull_valid = hull.size() >= 3 && polygon_area_abs(hull) > 3.f;
		if (hull_valid)
		{
			clip_polygon_to_rect(hull, rmin, rmax);
			if (hull.size() >= 3)
				mp.clipped_fill = std::move(hull);
		}

		pieces.push_back(std::move(mp));
	}

	std::sort(pieces.begin(), pieces.end(),
		[](const map_piece_t& a, const map_piece_t& b) {
			if (a.floorish != b.floorish)
				return a.floorish;
			return a.dist_sq > b.dist_sq;
		});

	const int edges[12][2] = {
		{ 0, 1 }, { 1, 3 }, { 3, 2 }, { 2, 0 },
		{ 4, 5 }, { 5, 7 }, { 7, 6 }, { 6, 4 },
		{ 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }
	};

	for (const map_piece_t& mp : pieces)
	{
		if (mp.clipped_fill.size() >= 3 && mp.clipped_fill.size() <= 64)
		{
			bool valid = true;
			for (const auto& p : mp.clipped_fill)
			{
				if (!std::isfinite(p.x) || !std::isfinite(p.y) ||
					p.x < -10000.f || p.x > 10000.f ||
					p.y < -10000.f || p.y > 10000.f)
				{
					valid = false;
					break;
				}
			}
			if (valid)
				draw->AddConvexPolyFilled(mp.clipped_fill.data(), (int)mp.clipped_fill.size(), mp.fill_col);
		}
	}

	for (const map_piece_t& mp : pieces)
	{
		for (const auto& e : edges)
		{
			ImVec2 ca, cb;
			if (!clip_segment_to_rect(rmin, rmax, mp.corners[e[0]], mp.corners[e[1]], ca, cb))
				continue;
			draw->AddLine(ca, cb, mp.stroke_col, mp.stroke_thick);
		}
	}

	ImVec2 n0, n1, e0, e1;
	if (clip_segment_to_rect(rmin, rmax, ImVec2(center.x, rmin.y), ImVec2(center.x, rmax.y), n0, n1))
		draw->AddLine(n0, n1, IM_COL32(255, 255, 255, 35), 1.f);
	if (clip_segment_to_rect(rmin, rmax, ImVec2(rmin.x, center.y), ImVec2(rmax.x, center.y), e0, e1))
		draw->AddLine(e0, e1, IM_COL32(255, 255, 255, 35), 1.f);

	std::vector<cache::entity_t> snapshot;
	{
		std::lock_guard<std::mutex> lock(cache::mtx);
		snapshot = cache::cached_players;
	}

	std::uint64_t local_addr = 0;
	if (game::players.address)
		local_addr = memory->read<std::uint64_t>(game::players.address + Offsets::Player::LocalPlayer);

	for (const auto& entity : snapshot)
	{
		if (!entity.instance.address)
			continue;

		auto pit = entity.parts.find("HumanoidRootPart");
		if (pit == entity.parts.end())
			pit = entity.parts.find("Head");
		if (pit == entity.parts.end())
			continue;

		rbx::primitive_t ep = pit->second.get_primitive();
		if (!ep.address)
			continue;

		math::vector3 pos = ep.get_position();
		ImVec2 p = world_to_radar_pt(center, radar_half, range, local_pos, fwd, rgt, pos, true);
		if (!in_square(rmin, rmax, p))
			continue;

		bool is_local = entity.instance.address == local_addr;
		ImU32 col = is_local ? IM_COL32(80, 200, 255, 255) : IM_COL32(255, 80, 80, 255);
		draw->AddCircleFilled(p, is_local ? 4.f : 3.f, col, 12);
	}
}
