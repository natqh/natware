#include "wallcheck.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <cache/cache.h>
#include <game/game.h>
#include <imgui/imgui.h>
#include <memory/memory.h>
#include <offsets/offsets.hpp>
#include <settings.h>

namespace
{
	static int clamp_channel(float v)
	{
		if (v < 0.f) return 0;
		if (v > 255.f) return 255;
		return static_cast<int>(v + 0.5f);
	}

	static std::uint32_t material_to_color(std::int16_t mat)
	{
		switch (mat)
		{
		case 256:  return IM_COL32(200, 200, 200, 255);
		case 288:  return IM_COL32(255, 100, 255, 255);
		case 512:  return IM_COL32(139, 90, 43, 255);
		case 800:  return IM_COL32(100, 100, 110, 255);
		case 816:  return IM_COL32(140, 140, 145, 255);
		case 1040: return IM_COL32(80, 85, 90, 255);
		case 1056: return IM_COL32(140, 145, 150, 255);
		case 1072: return IM_COL32(180, 180, 190, 255);
		case 1088: return IM_COL32(90, 120, 60, 255);
		case 1104: return IM_COL32(180, 220, 255, 255);
		case 1120: return IM_COL32(160, 165, 170, 255);
		case 1136: return IM_COL32(130, 125, 120, 255);
		case 1152: return IM_COL32(160, 80, 60, 255);
		case 1168: return IM_COL32(130, 125, 115, 255);
		case 1184: return IM_COL32(195, 175, 130, 255);
		case 1200: return IM_COL32(200, 180, 160, 255);
		case 1280: return IM_COL32(200, 220, 255, 255);
		case 1536: return IM_COL32(100, 150, 255, 180);
		case 880:  return IM_COL32(70, 70, 75, 255);
		case 848:  return IM_COL32(55, 55, 60, 255);
		case 864:  return IM_COL32(95, 95, 100, 255);
		case 832:  return IM_COL32(120, 115, 110, 255);
		case 896:  return IM_COL32(200, 60, 40, 255);
		case 928:  return IM_COL32(220, 220, 235, 255);
		case 912:  return IM_COL32(240, 240, 250, 255);
		case 944:  return IM_COL32(200, 210, 220, 255);
		case 960:  return IM_COL32(235, 235, 240, 255);
		case 976:  return IM_COL32(245, 245, 250, 255);
		case 992:  return IM_COL32(250, 250, 255, 255);
		case 1008: return IM_COL32(255, 255, 255, 255);
		case 1024: return IM_COL32(100, 140, 180, 255);
		case 1048: return IM_COL32(85, 95, 100, 255);
		case 1064: return IM_COL32(150, 140, 120, 255);
		case 1096: return IM_COL32(190, 185, 175, 255);
		case 1112: return IM_COL32(175, 170, 160, 255);
		case 1144: return IM_COL32(140, 135, 125, 255);
		case 1176: return IM_COL32(210, 200, 175, 255);
		case 1192: return IM_COL32(225, 215, 190, 255);
		case 1216: return IM_COL32(240, 235, 220, 255);
		case 1232: return IM_COL32(255, 250, 240, 255);
		case 1248: return IM_COL32(200, 195, 185, 255);
		case 1264: return IM_COL32(180, 175, 165, 255);
		case 1288: return IM_COL32(220, 230, 245, 255);
		case 1312: return IM_COL32(100, 180, 255, 255);
		case 1328: return IM_COL32(255, 200, 120, 255);
		case 1344: return IM_COL32(255, 140, 80, 255);
		case 1360: return IM_COL32(255, 100, 60, 255);
		case 1376: return IM_COL32(200, 100, 255, 255);
		case 1392: return IM_COL32(255, 255, 200, 255);
		case 1408: return IM_COL32(180, 220, 255, 255);
		case 1424: return IM_COL32(160, 200, 255, 255);
		case 1440: return IM_COL32(140, 180, 240, 255);
		case 1456: return IM_COL32(120, 160, 220, 255);
		case 1472: return IM_COL32(100, 140, 200, 255);
		case 1488: return IM_COL32(80, 120, 180, 255);
		case 1504: return IM_COL32(60, 100, 160, 255);
		case 1520: return IM_COL32(40, 80, 140, 255);
		default:
		{
			std::uint32_t u = static_cast<std::uint32_t>(static_cast<std::int16_t>(mat)) * 0x9e3779b1u;
			int r = 85 + static_cast<int>(u & 0x4fu);
			int g = 85 + static_cast<int>((u >> 9) & 0x4fu);
			int b = 85 + static_cast<int>((u >> 18) & 0x4fu);
			return IM_COL32(r, g, b, 255);
		}
		}
	}

	static std::uint32_t read_part_color_rgba(std::uint64_t part_address, std::uint64_t prim_address)
	{
		if (!part_address)
			return material_to_color(256);

		const std::uintptr_t color_offset = part_address + Offsets::BasePart::Color3;

		std::uint64_t ptr = memory->read<std::uint64_t>(color_offset);
		if (ptr && ptr < 0x7FFFFFFFFFFFull)
		{
			math::vector3 rgb = memory->read<math::vector3>(ptr);
			if (std::isfinite(rgb.x) && std::isfinite(rgb.y) && std::isfinite(rgb.z))
			{
				float mx = (std::max)((std::max)(rgb.x, rgb.y), rgb.z);
				float mn = (std::min)((std::min)(rgb.x, rgb.y), rgb.z);
				if (mx <= 1.2f && mn >= -0.02f)
					return IM_COL32(clamp_channel(rgb.x * 255.f), clamp_channel(rgb.y * 255.f), clamp_channel(rgb.z * 255.f), 255);
			}
		}

		math::vector3 rgb = memory->read<math::vector3>(color_offset);
		if (std::isfinite(rgb.x) && std::isfinite(rgb.y) && std::isfinite(rgb.z))
		{
			float mx = (std::max)((std::max)(rgb.x, rgb.y), rgb.z);
			float mn = (std::min)((std::min)(rgb.x, rgb.y), rgb.z);
			if (mx <= 1.2f && mn >= -0.02f)
				return IM_COL32(clamp_channel(rgb.x * 255.f), clamp_channel(rgb.y * 255.f), clamp_channel(rgb.z * 255.f), 255);
			if (mx <= 260.f && mn >= 0.f && mx > 1.5f)
				return IM_COL32(clamp_channel(rgb.x), clamp_channel(rgb.y), clamp_channel(rgb.z), 255);
		}

		std::uint32_t p = memory->read<std::uint32_t>(color_offset);
		int r = static_cast<int>(p & 0xFFu);
		int g = static_cast<int>((p >> 8) & 0xFFu);
		int b = static_cast<int>((p >> 16) & 0xFFu);
		if (r != 0 || g != 0 || b != 0)
			return IM_COL32(r, g, b, 255);

		r = static_cast<int>((p >> 16) & 0xFFu);
		g = static_cast<int>((p >> 8) & 0xFFu);
		b = static_cast<int>(p & 0xFFu);
		if (r != 0 || g != 0 || b != 0)
			return IM_COL32(r, g, b, 255);

		if (prim_address)
		{
			std::int16_t mat0 = memory->read<std::int16_t>(prim_address + Offsets::Primitive::Material);
			if (mat0 != 0 && mat0 >= 128 && mat0 <= 4096)
				return material_to_color(mat0);
			std::int16_t mat1 = memory->read<std::int16_t>(prim_address + 0x246);
			if (mat1 != 0 && mat1 >= 128 && mat1 <= 4096)
				return material_to_color(mat1);
		}
		return material_to_color(256);
	}

	static bool is_basepart_class(const std::string& className)
	{
		return className == "BasePart" ||
			className == "Part" ||
			className == "MeshPart" ||
			className == "WedgePart" ||
			className == "CornerWedgePart" ||
			className == "Ball" ||
			className == "Cylinder" ||
			className == "UnionOperation" ||
			className == "TrussPart" ||
			className == "TriangleMeshPart" ||
			className == "SmoothVoxelPart" ||
			className == "PartOperation";
	}

	static bool finite3(const math::vector3& v)
	{
		return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
	}

	// Whether a part hides what's behind it, by the filters the map read decided to trust.
	static bool blocks_sight(wallcheck_snapshot_t& snap, float transparency, std::uint8_t flags)
	{
		// See-through: invisible parts, and glass or anything half see-through.
		if (snap.trust_transparency && transparency >= 0.5f)
		{
			++snap.stats.invisible;
			return false;
		}
		// Bullets and raycasts pass through (CanQuery off), or it's decoration you can walk
		// through (CanCollide off: leaves, grass, effects, trims, a door that's been opened).
		const std::uint8_t can_query = static_cast<std::uint8_t>(Offsets::PrimitiveFlags::CanQuery);
		const std::uint8_t can_collide = static_cast<std::uint8_t>(Offsets::PrimitiveFlags::CanCollide);
		if ((snap.trust_query && !(flags & can_query)) || (snap.trust_collide && !(flags & can_collide)))
		{
			++snap.stats.no_query;
			return false;
		}
		return true;
	}

	// Walks the workspace once. Class names are looked up once per class (by descriptor), and an
	// instance budget stops a huge or half-torn-down place from stalling the read.
	struct scanner_t
	{
		wallcheck_snapshot_t& out;
		bool want_radar;
		std::unordered_set<std::uint64_t> character_parts;
		std::unordered_map<std::uint64_t, std::string> classes;
		int budget = 250000;

		struct candidate_t
		{
			rbx::obb box;
			std::uint8_t flags;
			float transparency;
			std::uint32_t raw_shape;   // Part.Shape as read, for plain Parts (see finish)
			bool plain_part;
		};
		std::vector<candidate_t> candidates;

		const std::string& class_of(const rbx::instance_t& inst)
		{
			const std::uint64_t desc = memory->read<std::uint64_t>(inst.address + Offsets::Instance::ClassDescriptor);
			auto it = classes.find(desc);
			if (it != classes.end())
				return it->second;

			std::string name = "unknown";
			if (desc)
				if (const std::uint64_t p = memory->read<std::uint64_t>(desc + Offsets::Instance::ClassName))
					name = memory->read_string(p);
			return classes.emplace(desc, std::move(name)).first->second;
		}

		// A character (anyone's, or an NPC's) is not a wall: it has a Humanoid, or it holds a
		// part the player cache knows as someone's body (games like Phantom Forces have no Humanoid).
		bool is_character(const std::vector<rbx::instance_t>& children)
		{
			for (const rbx::instance_t& c : children)
				if (character_parts.count(c.address) || class_of(c) == "Humanoid")
					return true;
			return false;
		}

		void scan(const std::vector<rbx::instance_t>& children, int depth)
		{
			if (depth > 96)
				return;

			for (const rbx::instance_t& child : children)
			{
				if (--budget < 0)
				{
					out.stats.budget_hit = true;
					return;
				}

				const std::string& cls = class_of(child);
				if (is_basepart_class(cls))
				{
					add_part(child, cls);
					scan(child.get_children<rbx::instance_t>(), depth + 1);   // parts parented to parts
				}
				else if (cls == "Model")
				{
					const std::vector<rbx::instance_t> inner = child.get_children<rbx::instance_t>();
					if (!is_character(inner))
						scan(inner, depth + 1);
				}
				else if (cls == "Folder" || cls == "Actor" || cls == "WorldRoot" || cls == "WorldModel")
				{
					scan(child.get_children<rbx::instance_t>(), depth + 1);
				}
				// Camera (view models), Terrain, tools and accessories are skipped.
			}
		}

		void add_part(const rbx::instance_t& inst, const std::string& cls)
		{
			if (character_parts.count(inst.address))
				return;

			rbx::primitive_t prim = rbx::part_t(inst.address).get_primitive();
			if (!prim.address)
				return;

			const math::vector3 size = prim.get_size();
			const float mn = (std::min)((std::min)(size.x, size.y), size.z);
			const float mx = (std::max)((std::max)(size.x, size.y), size.z);
			if (!std::isfinite(mn) || !std::isfinite(mx) || mn <= 0.f || mx > 8000.f)
				return;

			const math::vector3 center = prim.get_position();
			if (!finite3(center))
				return;

			const rbx::obb box(center, size, math::cframe(center, prim.get_rotation()));
			for (const math::vector3& axis : box.axes)
				if (!finite3(axis) || std::fabs(axis.length() - 1.0f) > 0.05f)
					return;   // not a real rotation: a part being torn down

			if (want_radar && mn >= 0.08f)
			{
				out.radar_geometry.push_back(box);
				out.radar_geometry_colors.push_back(read_part_color_rgba(inst.address, prim.address));
			}

			const bool plain = cls == "Part";
			candidates.push_back({ box,
				memory->read<std::uint8_t>(prim.address + Offsets::Primitive::Flags),
				memory->read<float>(inst.address + Offsets::BasePart::Transparency),
				plain ? memory->read<std::uint32_t>(inst.address + Offsets::BasePart::Shape) : 0u,
				plain });
			wallcheck_snapshot_t::source_t src;
			src.part = inst.address;
			src.prim = prim.address;
			src.size = size;
			src.shape = cls == "WedgePart" ? wallcheck_snapshot_t::wedge_shape : wallcheck_snapshot_t::box_shape;
			out.sources.push_back(src);
		}

		// Two filters, each checked against the whole map before it's trusted: if an offset has
		// gone stale after a Roblox update its reads look like noise, and a filter built on noise
		// would quietly throw away every wall.
		void finish()
		{
			const std::size_t n = candidates.size();
			out.stats.parts = static_cast<int>(n);

			// Invisible parts (zones, barriers, hitboxes) don't hide anyone. Every real
			// transparency is within 0..1, and most of a map is visible.
			std::size_t out_of_range = 0, invisible = 0;
			for (const candidate_t& c : candidates)
			{
				if (!std::isfinite(c.transparency) || c.transparency < -0.001f || c.transparency > 1.001f)
					++out_of_range;
				else if (c.transparency >= 0.95f)
					++invisible;
			}
			const bool trust_transparency = n > 0 && out_of_range * 20 <= n && invisible * 10 <= n * 7;

			// With CanQuery off, raycasts (so bullets) pass through a part: leaves, grass,
			// effects. Parts have it on by default, so most of a map must read as on.
			const std::uint8_t can_query = static_cast<std::uint8_t>(Offsets::PrimitiveFlags::CanQuery);
			std::size_t queryable = 0;
			for (const candidate_t& c : candidates)
				if (c.flags & can_query)
					++queryable;
			const bool trust_flags = n > 0 && queryable * 10 >= n * 6;

			// CanCollide gets its own check: most of a map is solid, so if under 40% reads as
			// colliding the bit is likely somewhere else and it's ignored.
			const std::uint8_t can_collide = static_cast<std::uint8_t>(Offsets::PrimitiveFlags::CanCollide);
			std::size_t colliding = 0;
			for (const candidate_t& c : candidates)
				if (c.flags & can_collide)
					++colliding;
			out.trust_transparency = trust_transparency;
			out.trust_query = trust_flags;
			out.trust_collide = trust_flags && colliding * 10 >= n * 4;
			out.stats.transparency_trusted = trust_transparency;
			out.stats.flags_trusted = trust_flags;

			// Part.Shape (ball, block, cylinder, wedge, corner wedge): trusted only if plain Parts
			// read as real shapes, mostly blocks. Otherwise every Part stays a box.
			std::size_t plain = 0, valid_shape = 0, blocks = 0;
			for (const candidate_t& c : candidates)
				if (c.plain_part)
				{
					++plain;
					valid_shape += c.raw_shape <= 4u;
					blocks += c.raw_shape == 1u;
				}
			const bool trust_shape = plain > 0 && valid_shape * 20 >= plain * 19 && blocks * 10 >= plain * 3;
			for (std::size_t i = 0; i < n; ++i)
			{
				const candidate_t& c = candidates[i];
				if (!trust_shape || !c.plain_part)
					continue;
				std::uint8_t& shape = out.sources[i].shape;
				switch (c.raw_shape)
				{
				case 0: shape = wallcheck_snapshot_t::ball_shape; break;
				case 2: shape = wallcheck_snapshot_t::cylinder_shape; break;
				case 3: shape = wallcheck_snapshot_t::wedge_shape; break;
				default: shape = wallcheck_snapshot_t::box_shape; break;   // block, corner wedge
				}
			}

			out.obstacles.reserve(n);
			out.shapes.reserve(n);
			for (std::size_t i = 0; i < n; ++i)
			{
				const candidate_t& c = candidates[i];
				if (blocks_sight(out, c.transparency, c.flags))
				{
					out.obstacles.push_back(c.box);
					out.shapes.push_back(out.sources[i].shape);
				}
			}
			out.stats.walls = static_cast<int>(out.obstacles.size());
		}
	};

	// Median-split BVH over the obstacles' world-aligned bounds, leaves of up to four boxes.
	static void build_bvh(wallcheck_snapshot_t& s)
	{
		const std::int32_t n = static_cast<std::int32_t>(s.obstacles.size());
		s.nodes.clear();
		s.order.resize(n);
		std::iota(s.order.begin(), s.order.end(), 0);
		if (n == 0)
			return;

		std::vector<math::vector3> lo(n), hi(n);
		for (std::int32_t i = 0; i < n; ++i)
		{
			const math::vector3 h = s.obstacles[i].aabb_half();
			lo[i] = s.obstacles[i].center - h;
			hi[i] = s.obstacles[i].center + h;
		}

		struct job_t { std::int32_t node, begin, end; };
		std::vector<job_t> jobs{ { 0, 0, n } };
		s.nodes.reserve(static_cast<std::size_t>(n) / 2 + 8);
		s.nodes.emplace_back();

		while (!jobs.empty())
		{
			const job_t job = jobs.back();
			jobs.pop_back();

			math::vector3 mn = lo[s.order[job.begin]], mx = hi[s.order[job.begin]];
			math::vector3 cmn = s.obstacles[s.order[job.begin]].center, cmx = cmn;
			for (std::int32_t k = job.begin; k < job.end; ++k)
			{
				const std::int32_t i = s.order[k];
				const math::vector3& c = s.obstacles[i].center;
				for (int a = 0; a < 3; ++a)
				{
					mn[a] = (std::min)(mn[a], lo[i][a]);
					mx[a] = (std::max)(mx[a], hi[i][a]);
					cmn[a] = (std::min)(cmn[a], c[a]);
					cmx[a] = (std::max)(cmx[a], c[a]);
				}
			}
			s.nodes[job.node].mn = mn;
			s.nodes[job.node].mx = mx;

			int axis = 0;
			for (int a = 1; a < 3; ++a)
				if (cmx[a] - cmn[a] > cmx[axis] - cmn[axis])
					axis = a;

			if (job.end - job.begin <= 4 || cmx[axis] - cmn[axis] < 1e-4f)
			{
				s.nodes[job.node].first = job.begin;
				s.nodes[job.node].count = job.end - job.begin;
				continue;
			}

			const std::int32_t mid = (job.begin + job.end) / 2;
			std::nth_element(s.order.begin() + job.begin, s.order.begin() + mid, s.order.begin() + job.end,
				[&](std::int32_t x, std::int32_t y) { return s.obstacles[x].center[axis] < s.obstacles[y].center[axis]; });

			const std::int32_t left = static_cast<std::int32_t>(s.nodes.size());
			s.nodes.emplace_back();
			s.nodes.emplace_back();
			s.nodes[job.node].first = left;
			s.nodes[job.node].count = 0;
			jobs.push_back({ left, job.begin, mid });
			jobs.push_back({ left + 1, mid, job.end });
		}
	}

	// Whether the segment (origin, unit dir, len) passes through the solid part of an obstacle:
	// the box, then for a wedge, ball or cylinder the part of the box it fills. Worked in the
	// box's own axes.
	static bool shape_blocks(const rbx::obb& box, std::uint8_t shape, const math::vector3& origin, const math::vector3& dir, float len)
	{
		const math::vector3 rel = origin - box.center;
		float o[3], d[3];
		for (int a = 0; a < 3; ++a)
		{
			o[a] = box.axes[a].dot(rel);
			d[a] = box.axes[a].dot(dir);
		}
		const float* h = &box.half_size.x;

		float t0 = 0.0f, t1 = len;
		for (int a = 0; a < 3; ++a)
		{
			if (std::fabs(d[a]) < 1e-8f)
			{
				if (std::fabs(o[a]) > h[a])
					return false;
				continue;
			}
			float ta = (-h[a] - o[a]) / d[a], tb = (h[a] - o[a]) / d[a];
			if (ta > tb) std::swap(ta, tb);
			t0 = (std::max)(t0, ta);
			t1 = (std::min)(t1, tb);
			if (t1 < t0)
				return false;
		}

		// Where on [t0, t1] a quadratic a*t^2 + 2b*t + c is at or below zero (inside a round shape).
		auto round_overlap = [&](float qa, float qb, float qc) -> bool
		{
			if (qa < 1e-10f)
				return qc <= 0.0f;
			const float disc = qb * qb - qa * qc;
			if (disc < 0.0f)
				return false;
			const float root = std::sqrt(disc);
			const float ta = (-qb - root) / qa, tb = (-qb + root) / qa;
			return ta <= t1 && tb >= t0;
		};

		switch (shape)
		{
		case wallcheck_snapshot_t::wedge_shape:
		{
			// Solid below the slope that runs from the bottom front edge (-Z) to the top back
			// edge (+Z): y / hy <= z / hz.
			const float f0 = o[1] * h[2] - o[2] * h[1];
			const float f1 = d[1] * h[2] - d[2] * h[1];
			if (std::fabs(f1) < 1e-10f)
				return f0 <= 0.0f;
			const float tz = -f0 / f1;
			return f1 > 0.0f ? tz >= t0 : tz <= t1;
		}
		case wallcheck_snapshot_t::ball_shape:
		{
			const float r = (std::min)((std::min)(h[0], h[1]), h[2]);
			return round_overlap(d[0] * d[0] + d[1] * d[1] + d[2] * d[2], o[0] * d[0] + o[1] * d[1] + o[2] * d[2],
				o[0] * o[0] + o[1] * o[1] + o[2] * o[2] - r * r);
		}
		case wallcheck_snapshot_t::cylinder_shape:
		{
			const float r = (std::min)(h[1], h[2]);
			return round_overlap(d[1] * d[1] + d[2] * d[2], o[1] * d[1] + o[2] * d[2], o[1] * o[1] + o[2] * o[2] - r * r);
		}
		default:
			return true;
		}
	}

	static bool segment_hits_aabb(const math::vector3& o, const math::vector3& inv, float len, const math::vector3& mn, const math::vector3& mx)
	{
		float t0 = 0.0f, t1 = len;
		for (int a = 0; a < 3; ++a)
		{
			float ta = (mn[a] - o[a]) * inv[a];
			float tb = (mx[a] - o[a]) * inv[a];
			if (ta > tb) std::swap(ta, tb);
			if (ta > t0) t0 = ta;
			if (tb < t1) t1 = tb;
			if (t1 < t0)
				return false;
		}
		return true;
	}
}

bool wallcheck_snapshot_t::is_visible(const math::vector3& origin, const math::vector3& target) const
{
	if (nodes.empty())
		return true;

	const math::vector3 delta = target - origin;
	const float dist = delta.length();
	if (!std::isfinite(dist) || dist < 0.1f)
		return true;

	// Stop just short of the target, so a wall it stands flush against doesn't count.
	const math::vector3 dir = delta * (1.0f / dist);
	const float len = (std::max)(0.0f, dist - 0.05f);
	math::vector3 inv{};
	for (int a = 0; a < 3; ++a)
		inv[a] = std::fabs(dir[a]) > 1e-8f ? 1.0f / dir[a] : (dir[a] < 0.0f ? -1e30f : 1e30f);

	std::int32_t stack[96];
	int sp = 0;
	stack[sp++] = 0;
	while (sp > 0)
	{
		const node_t& node = nodes[stack[--sp]];
		if (!segment_hits_aabb(origin, inv, len, node.mn, node.mx))
			continue;

		if (node.count == 0)
		{
			if (sp + 2 > 96)
				return true;
			stack[sp++] = node.first;
			stack[sp++] = node.first + 1;
			continue;
		}

		for (std::int32_t k = node.first; k < node.first + node.count; ++k)
		{
			const std::int32_t i = order[k];
			const rbx::obb& box = obstacles[i];
			if (box.contains(origin) || box.contains(target))
				continue;
			const std::uint8_t shape = static_cast<std::size_t>(i) < shapes.size() ? shapes[i] : static_cast<std::uint8_t>(box_shape);
			if (shape_blocks(box, shape, origin, dir, len))
				return false;
		}
	}
	return true;
}

bool c_wallcheck::needed()
{
	return settings::visuals::radar_enabled
		|| settings::visuals::debug_wallcheck
		|| (settings::aimbot::enabled && settings::aimbot::wallcheck)
		|| (settings::silentaim::enabled && settings::silentaim::wallcheck)
		|| (settings::aimbot::triggerbot::enabled && settings::aimbot::triggerbot::wallcheck)
		|| (settings::silentaim::enabled && settings::silentaim::triggerbot::enabled && settings::silentaim::triggerbot::wallcheck);
}

std::shared_ptr<const wallcheck_snapshot_t> c_wallcheck::build(bool want_radar) const
{
	if (game::datamodel.address == 0)
		return nullptr;

	rbx::instance_t workspace = game::datamodel.find_first_child_by_class("Workspace");
	if (workspace.address == 0)
		return nullptr;

	auto snap = std::make_shared<wallcheck_snapshot_t>();
	snap->has_radar = want_radar;

	scanner_t scanner{ *snap, want_radar };
	{
		std::lock_guard<std::mutex> lock(cache::mtx);
		auto add = [&](const cache::entity_t& e)
		{
			for (const auto& [name, part] : e.parts)
				if (part.address)
					scanner.character_parts.insert(part.address);
		};
		add(cache::cached_local_player);
		for (const cache::entity_t& e : cache::cached_players)
			add(e);
	}

	const auto started_at = std::chrono::steady_clock::now();
	scanner.scan(workspace.get_children<rbx::instance_t>(), 0);
	scanner.finish();
	build_bvh(*snap);
	snap->stats.read_ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started_at).count();
	snap->built_at = std::chrono::steady_clock::now();
	snap->posed_at = snap->built_at;
	return snap;
}

// Where every part is now, and whether it's solid, from the last full read's part list: doors
// that open, barriers that vanish or move, platforms. New and removed parts wait for the next
// full read.
std::shared_ptr<const wallcheck_snapshot_t> c_wallcheck::repose(const wallcheck_snapshot_t& last)
{
	struct pose_t
	{
		math::matrix3 rotation;   // Primitive::Rotation, followed directly by Position
		math::vector3 position;
	};
	static_assert(sizeof(math::matrix3) == 36 && sizeof(pose_t) == 48, "pose read layout");
	const bool contiguous = Offsets::Primitive::Position == Offsets::Primitive::Rotation + sizeof(math::matrix3);

	auto snap = std::make_shared<wallcheck_snapshot_t>();
	snap->sources = last.sources;
	snap->trust_transparency = last.trust_transparency;
	snap->trust_query = last.trust_query;
	snap->trust_collide = last.trust_collide;
	snap->has_radar = last.has_radar;
	snap->radar_geometry = last.radar_geometry;
	snap->radar_geometry_colors = last.radar_geometry_colors;
	snap->built_at = last.built_at;
	snap->stats = last.stats;
	snap->stats.invisible = snap->stats.no_query = 0;

	snap->obstacles.reserve(last.obstacles.size() + 16);
	snap->shapes.reserve(last.obstacles.size() + 16);
	for (const wallcheck_snapshot_t::source_t& src : snap->sources)
	{
		pose_t pose{};
		if (contiguous)
			pose = memory->read<pose_t>(src.prim + Offsets::Primitive::Rotation);
		else
		{
			pose.rotation = memory->read<math::matrix3>(src.prim + Offsets::Primitive::Rotation);
			pose.position = memory->read<math::vector3>(src.prim + Offsets::Primitive::Position);
		}
		if (!finite3(pose.position))
			continue;
		const rbx::obb box(pose.position, src.size, math::cframe(pose.position, pose.rotation));
		bool real = true;
		for (const math::vector3& axis : box.axes)
			real = real && finite3(axis) && std::fabs(axis.length() - 1.0f) <= 0.05f;
		if (!real)
			continue;   // gone, or being torn down
		const float transparency = memory->read<float>(src.part + Offsets::BasePart::Transparency);
		const std::uint8_t flags = memory->read<std::uint8_t>(src.prim + Offsets::Primitive::Flags);
		if (!blocks_sight(*snap, transparency, flags))
			continue;
		snap->obstacles.push_back(box);
		snap->shapes.push_back(src.shape);
	}
	snap->stats.walls = static_cast<int>(snap->obstacles.size());
	build_bvh(*snap);
	snap->stats.read_ms = last.stats.read_ms;
	snap->posed_at = std::chrono::steady_clock::now();
	return snap;
}

void c_wallcheck::worker()
{
	using clock = std::chrono::steady_clock;
	constexpr auto kRefresh = std::chrono::seconds(3);

	std::uint64_t last_datamodel = 0;
	clock::time_point last_read{};
	clock::time_point last_pose{};
	auto pose_every = std::chrono::milliseconds(250);
	bool have_read = false;

	for (;;)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(50));

		const std::uint64_t datamodel = game::datamodel.address;
		if (datamodel != last_datamodel)
		{
			// A new place or a rejoin: the old walls are wrong now.
			std::lock_guard<std::mutex> lock(mtx);
			current.reset();
			last_datamodel = datamodel;
			have_read = false;
		}

		if (datamodel == 0 || !needed())
		{
			have_read = false;   // read straight away once something turns it on
			continue;
		}

		const bool want_radar = settings::visuals::radar_enabled;
		const std::shared_ptr<const wallcheck_snapshot_t> now_snap = snapshot();
		const bool radar_missing = want_radar && now_snap && !now_snap->has_radar;
		if (have_read && !radar_missing && clock::now() - last_read < kRefresh)
		{
			// Between full reads: where the parts are now, a few times a second. A big map
			// re-reads less often, so this never takes more than about a fifth of the time.
			if (!now_snap || now_snap->sources.empty() || clock::now() - last_pose < pose_every)
				continue;
			const clock::time_point began = clock::now();
			std::shared_ptr<const wallcheck_snapshot_t> posed = repose(*now_snap);
			last_pose = clock::now();
			const auto took = std::chrono::duration_cast<std::chrono::milliseconds>(last_pose - began);
			pose_every = (std::max)(std::chrono::milliseconds(250), took * 5);
			if (posed && game::datamodel.address == datamodel)
			{
				std::lock_guard<std::mutex> lock(mtx);
				if (current == now_snap)   // not replaced by a full read meanwhile
					current = std::move(posed);
			}
			continue;
		}

		std::shared_ptr<const wallcheck_snapshot_t> snap = build(want_radar);
		last_read = clock::now();
		last_pose = last_read;
		have_read = true;
		if (snap && game::datamodel.address == datamodel)
		{
			std::lock_guard<std::mutex> lock(mtx);
			current = std::move(snap);
		}
	}
}

void c_wallcheck::tick()
{
	bool expected = false;
	if (started.compare_exchange_strong(expected, true))
		std::thread([this] { worker(); }).detach();
}

bool c_wallcheck::ready() const
{
	return snapshot() != nullptr;
}

std::shared_ptr<const wallcheck_snapshot_t> c_wallcheck::snapshot() const
{
	std::lock_guard<std::mutex> lock(mtx);
	return current;
}

bool c_wallcheck::is_visible(const math::vector3& origin, const math::vector3& target) const
{
	const std::shared_ptr<const wallcheck_snapshot_t> snap = snapshot();
	return !snap || snap->is_visible(origin, target);
}
