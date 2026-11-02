#include "esp.h"
#include "rig.h"
#include "esp_draw.h"

#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <settings.h>
#include <game/game.h>
#include <cache/cache.h>
#include <cache/custom_entities/custom_entities.h>
#include <cache/bodyparts/bodyparts.h>
#include <render/render.h>
#include <memory/memory.h>
#include <offsets/offsets.hpp>
#include <wallcheck/wallcheck.h>
#include "../../font/config/font_config.h"
#include <features/avatarmanager/avatarmanager.h>
#include <features/chams/engine_chams.h>
#include <features/chams/body_chams.h>
#include <menu/keybind/keybind.h>

namespace helper
{
	__forceinline float clamp(float value, float min_val, float max_val)
	{
		return value < min_val ? min_val : (value > max_val ? max_val : value);
	}

	__forceinline ImU32 apply_opacity(ImU32 color, float opacity)
	{
		ImU32 r = (color >> 0) & 0xFF;
		ImU32 g = (color >> 8) & 0xFF;
		ImU32 b = (color >> 16) & 0xFF;
		ImU32 a = (color >> 24) & 0xFF;
		a = static_cast<ImU32>(a * opacity);
		return IM_COL32(r, g, b, a);
	}

	__forceinline ImU32 color_with_opacity(float r, float g, float b, float a, float opacity)
	{
		return IM_COL32(
			static_cast<int>(r * 255.f),
			static_cast<int>(g * 255.f),
			static_cast<int>(b * 255.f),
			static_cast<int>(a * opacity * 255.f)
		);
	}

	__forceinline void draw_text_outlined(ImDrawList* draw, ImFont* font, float font_size, ImVec2 pos, ImU32 col, const char* text_begin, const char* text_end = nullptr)
	{
		if (!font)
			font = ImGui::GetFont();
		if (!text_begin || (text_end ? text_end == text_begin : !*text_begin))
			return;

		pos.x = roundf(pos.x);
		pos.y = roundf(pos.y);
		const ImU32 outline_col = IM_COL32(0, 0, 0, (col >> IM_COL32_A_SHIFT) & 0xFF);

		ImDrawListFlags old_flags = draw->Flags;
		draw->Flags &= ~ImDrawListFlags_AntiAliasedLines;
		for (int x = -1; x <= 1; x++)
			for (int y = -1; y <= 1; y++)
				if (x != 0 || y != 0)
					draw->AddText(font, font_size, ImVec2(pos.x + x, pos.y + y), outline_col, text_begin, text_end);
		draw->AddText(font, font_size, pos, col, text_begin, text_end);
		draw->Flags = old_flags;
	}

	__forceinline void draw_text_blended(ImDrawList* draw, ImFont* font, float font_size, ImVec2 pos, ImU32 col_start, ImU32 col_end, const char* text_begin, const char* text_end = nullptr)
	{
		pos.x = roundf(pos.x);
		pos.y = roundf(pos.y);

		ImU32 outline_col = IM_COL32(0, 0, 0, 255);

		ImDrawListFlags old_flags = draw->Flags;
		draw->Flags &= ~ImDrawListFlags_AntiAliasedLines;

		const char* text_end_ptr = text_end ? text_end : text_begin + std::strlen(text_begin);
		int text_len = static_cast<int>(text_end_ptr - text_begin);

		if (text_len == 0)
			return;

		float start_r = static_cast<float>((col_start >> 0) & 0xFF);
		float start_g = static_cast<float>((col_start >> 8) & 0xFF);
		float start_b = static_cast<float>((col_start >> 16) & 0xFF);
		float start_a = static_cast<float>((col_start >> 24) & 0xFF);

		float end_r = static_cast<float>((col_end >> 0) & 0xFF);
		float end_g = static_cast<float>((col_end >> 8) & 0xFF);
		float end_b = static_cast<float>((col_end >> 16) & 0xFF);
		float end_a = static_cast<float>((col_end >> 24) & 0xFF);

		float current_x = pos.x;
		for (int i = 0; i < text_len; i++)
		{
			float t = text_len > 1 ? static_cast<float>(i) / static_cast<float>(text_len - 1) : 0.0f;

			float r = start_r + (end_r - start_r) * t;
			float g = start_g + (end_g - start_g) * t;
			float b = start_b + (end_b - start_b) * t;
			float a = start_a + (end_a - start_a) * t;

			ImU32 char_col = IM_COL32(static_cast<int>(r), static_cast<int>(g), static_cast<int>(b), static_cast<int>(a));

			const char* char_begin = text_begin + i;
			const char* char_end = text_begin + i + 1;

			for (int x = -1; x <= 1; x++)
			{
				for (int y = -1; y <= 1; y++)
				{
					if (x == 0 && y == 0)
					{
						continue;
					}
					draw->AddText(font, font_size, ImVec2(current_x + x, pos.y + y), outline_col, char_begin, char_end);
				}
			}

			float char_width = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, char_begin, char_end).x;
			draw->AddText(font, font_size, ImVec2(current_x, pos.y), char_col, char_begin, char_end);
			current_x += char_width + 1.0f;
		}

		draw->Flags = old_flags;
	}

	__forceinline void box(ImVec2& c1, ImVec2& c2, ImU32 color)
	{
		c1.x = roundf(c1.x);
		c1.y = roundf(c1.y);
		c2.x = roundf(c2.x);
		c2.y = roundf(c2.y);

		ImDrawList* draw = ImGui::GetBackgroundDrawList();
		const ImDrawListFlags old_flags = draw->Flags;
		draw->Flags &= ~ImDrawListFlags_AntiAliasedLines;   // crisp 1px lines

		ImRect rect(c1.x, c1.y, c1.x + c2.x, c1.y + c2.y);
		draw->AddRect(rect.Min, rect.Max, IM_COL32(0, 0, 0, color >> 24));
		draw->AddRect({ rect.Min.x - 1.f, rect.Min.y - 1.f }, { rect.Max.x + 1.f, rect.Max.y + 1.f }, color);
		draw->AddRect({ rect.Min.x - 2.f, rect.Min.y - 2.f }, { rect.Max.x + 2.f, rect.Max.y + 2.f }, IM_COL32(0, 0, 0, color >> 24));
		draw->Flags = old_flags;
	}

	__forceinline void corner_box(ImDrawList* draw, ImVec2 min, ImVec2 max, ImU32 col, float thickness = 1.f)
	{
		float x1 = roundf(min.x) - 1;
		float y1 = roundf(min.y) - 1;
		float x2 = roundf(max.x) + 1;
		float y2 = roundf(max.y) + 1;

		ImU32 outline_col = IM_COL32(0, 0, 0, 255);

		float box_width = x2 - x1;
		float box_height = y2 - y1;
		float length = (std::min)(box_width * 0.3f, box_height * 0.3f);
		length = (std::max)(length, 5.f);
		length = (std::min)(length, 15.f);

		float x1_len = roundf(x1 + length);
		float y1_len = roundf(y1 + length);
		float x2_len = roundf(x2 - length);
		float y2_len = roundf(y2 - length);

		draw->AddRectFilled(ImVec2(x1 - 1.f, y1 - 1.f), ImVec2(x1_len + 1.f, y1 + thickness + 1.f), outline_col);
		draw->AddRectFilled(ImVec2(x1 - 1.f, y1 - 1.f), ImVec2(x1 + thickness + 1.f, y1_len + 1.f), outline_col);

		draw->AddRectFilled(ImVec2(x2_len - 1.f, y1 - 1.f), ImVec2(x2 + 1.f, y1 + thickness + 1.f), outline_col);
		draw->AddRectFilled(ImVec2(x2 - thickness - 1.f, y1 - 1.f), ImVec2(x2 + 1.f, y1_len + 1.f), outline_col);

		draw->AddRectFilled(ImVec2(x1 - 1.f, y2 - thickness - 1.f), ImVec2(x1_len + 1.f, y2 + 1.f), outline_col);
		draw->AddRectFilled(ImVec2(x1 - 1.f, y2_len - 1.f), ImVec2(x1 + thickness + 1.f, y2 + 1.f), outline_col);

		draw->AddRectFilled(ImVec2(x2_len - 1.f, y2 - thickness - 1.f), ImVec2(x2 + 1.f, y2 + 1.f), outline_col);
		draw->AddRectFilled(ImVec2(x2 - thickness - 1.f, y2_len - 1.f), ImVec2(x2 + 1.f, y2 + 1.f), outline_col);

		draw->AddRectFilled(ImVec2(x1, y1), ImVec2(x1_len, y1 + thickness), col);
		draw->AddRectFilled(ImVec2(x1, y1), ImVec2(x1 + thickness, y1_len), col);

		draw->AddRectFilled(ImVec2(x2_len, y1), ImVec2(x2, y1 + thickness), col);
		draw->AddRectFilled(ImVec2(x2 - thickness, y1), ImVec2(x2, y1_len), col);

		draw->AddRectFilled(ImVec2(x1, y2 - thickness), ImVec2(x1_len, y2), col);
		draw->AddRectFilled(ImVec2(x1, y2_len), ImVec2(x1 + thickness, y2), col);

		draw->AddRectFilled(ImVec2(x2_len, y2 - thickness), ImVec2(x2, y2), col);
		draw->AddRectFilled(ImVec2(x2 - thickness, y2_len), ImVec2(x2, y2), col);
	}

	__forceinline void rounded_box(ImDrawList* draw, ImVec2 min, ImVec2 max, ImU32 col, float rounding = 4.f)
	{
		ImU32 outline_col = IM_COL32(0, 0, 0, (col >> 24) & 0xFF);
		draw->AddRect(ImVec2(min.x - 1.f, min.y - 1.f), ImVec2(max.x + 1.f, max.y + 1.f), outline_col, rounding + 1.f);
		draw->AddRect(min, max, col, rounding);
		draw->AddRect(ImVec2(min.x + 1.f, min.y + 1.f), ImVec2(max.x - 1.f, max.y - 1.f), outline_col, (std::max)(rounding - 1.f, 0.f));
	}

	__forceinline void cross_box(ImDrawList* draw, ImVec2 min, ImVec2 max, ImU32 col, float mark_size = 6.f)
	{
		ImU32 outline_col = IM_COL32(0, 0, 0, 255);
		float cx[] = { min.x, max.x, min.x, max.x };
		float cy[] = { min.y, min.y, max.y, max.y };

		for (int i = 0; i < 4; i++)
		{
			float x = roundf(cx[i]);
			float y = roundf(cy[i]);
			draw->AddLine(ImVec2(x - mark_size, y), ImVec2(x + mark_size, y), outline_col, 3.f);
			draw->AddLine(ImVec2(x, y - mark_size), ImVec2(x, y + mark_size), outline_col, 3.f);
			draw->AddLine(ImVec2(x - mark_size, y), ImVec2(x + mark_size, y), col, 1.f);
			draw->AddLine(ImVec2(x, y - mark_size), ImVec2(x, y + mark_size), col, 1.f);
		}

		draw->AddLine(ImVec2(min.x, min.y), ImVec2(max.x, min.y), IM_COL32(col & 0xFF, (col >> 8) & 0xFF, (col >> 16) & 0xFF, 60), 1.f);
		draw->AddLine(ImVec2(min.x, max.y), ImVec2(max.x, max.y), IM_COL32(col & 0xFF, (col >> 8) & 0xFF, (col >> 16) & 0xFF, 60), 1.f);
		draw->AddLine(ImVec2(min.x, min.y), ImVec2(min.x, max.y), IM_COL32(col & 0xFF, (col >> 8) & 0xFF, (col >> 16) & 0xFF, 60), 1.f);
		draw->AddLine(ImVec2(max.x, min.y), ImVec2(max.x, max.y), IM_COL32(col & 0xFF, (col >> 8) & 0xFF, (col >> 16) & 0xFF, 60), 1.f);
	}

	__forceinline void glow_box(ImDrawList* draw, ImVec2 min, ImVec2 max, ImU32 col, float glow_strength = 0.5f)
	{
		int r = col & 0xFF;
		int g = (col >> 8) & 0xFF;
		int b = (col >> 16) & 0xFF;
		int a = (col >> 24) & 0xFF;

		int layers = static_cast<int>(3 + glow_strength * 5);
		for (int i = layers; i >= 1; i--)
		{
			float expand = static_cast<float>(i) * 1.5f;
			int layer_alpha = static_cast<int>(a * glow_strength * (1.0f - static_cast<float>(i) / (layers + 1)));
			if (layer_alpha < 1) continue;
			ImU32 glow_col = IM_COL32(r, g, b, layer_alpha);
			draw->AddRect(ImVec2(min.x - expand, min.y - expand), ImVec2(max.x + expand, max.y + expand), glow_col, 0.f, 0, 1.f);
		}

		draw->AddRect(min, max, col, 0.f, 0, 1.f);
	}

	__forceinline void diamond_box(ImDrawList* draw, ImVec2 min, ImVec2 max, ImU32 col, float diamond_size = 5.f)
	{
		ImU32 outline_col = IM_COL32(0, 0, 0, 255);
		float cx[] = { min.x, max.x, min.x, max.x };
		float cy[] = { min.y, min.y, max.y, max.y };

		for (int i = 0; i < 4; i++)
		{
			float x = roundf(cx[i]);
			float y = roundf(cy[i]);
			ImVec2 pts[4] = {
				ImVec2(x, y - diamond_size),
				ImVec2(x + diamond_size, y),
				ImVec2(x, y + diamond_size),
				ImVec2(x - diamond_size, y)
			};
			draw->AddConvexPolyFilled(pts, 4, IM_COL32((col & 0xFF), (col >> 8) & 0xFF, (col >> 16) & 0xFF, 120));
			draw->AddPolyline(pts, 4, outline_col, ImDrawFlags_Closed, 1.5f);
			draw->AddPolyline(pts, 4, col, ImDrawFlags_Closed, 0.8f);
		}

		draw->AddLine(ImVec2(min.x, min.y), ImVec2(max.x, min.y), IM_COL32(col & 0xFF, (col >> 8) & 0xFF, (col >> 16) & 0xFF, 80), 1.f);
		draw->AddLine(ImVec2(min.x, max.y), ImVec2(max.x, max.y), IM_COL32(col & 0xFF, (col >> 8) & 0xFF, (col >> 16) & 0xFF, 80), 1.f);
		draw->AddLine(ImVec2(min.x, min.y), ImVec2(min.x, max.y), IM_COL32(col & 0xFF, (col >> 8) & 0xFF, (col >> 16) & 0xFF, 80), 1.f);
		draw->AddLine(ImVec2(max.x, min.y), ImVec2(max.x, max.y), IM_COL32(col & 0xFF, (col >> 8) & 0xFF, (col >> 16) & 0xFF, 80), 1.f);
	}

	inline float cross_product_2d(const ImVec2& O, const ImVec2& A, const ImVec2& B) {
		return (A.x - O.x) * (B.y - O.y) - (A.y - O.y) * (B.x - O.x);
	}

	inline float distance_sq(const ImVec2& A, const ImVec2& B) {
		return (A.x - B.x) * (A.x - B.x) + (A.y - B.y) * (A.y - B.y);
	}

	inline std::vector<ImVec2> convex_hull(const std::vector<ImVec2>& points) {
		if (points.size() <= 3) return points;

		std::vector<ImVec2> working_points = points;

		auto it = std::min_element(working_points.begin(), working_points.end(), [](const ImVec2& a, const ImVec2& b) {
			return (a.y < b.y) || (a.y == b.y && a.x < b.x);
			});

		std::swap(working_points[0], *it);
		ImVec2 p0 = working_points[0];

		std::sort(working_points.begin() + 1, working_points.end(), [&p0](const ImVec2& a, const ImVec2& b) {
			float cross = cross_product_2d(p0, a, b);
			return (cross > 0) || (cross == 0 && distance_sq(p0, a) < distance_sq(p0, b));
			});

		std::vector<ImVec2> hull;
		hull.push_back(working_points[0]);
		hull.push_back(working_points[1]);

		for (size_t i = 2; i < working_points.size(); i++) {
			while (hull.size() > 1 && cross_product_2d(hull[hull.size() - 2], hull.back(), working_points[i]) <= 0) {
				hull.pop_back();
			}
			hull.push_back(working_points[i]);
		}

		return hull;
	}
}

struct health_smooth_state_t
{
	float smooth_health = 0.0f;
	float smooth_armor = 0.0f;
};

static std::unordered_map<uint64_t, health_smooth_state_t> g_health_smooth_states;

struct esp_fade_state_t
{
	float box_opacity = 0.0f;
	float box_fill_opacity = 0.0f;
	float name_opacity = 0.0f;
	float healthbar_opacity = 0.0f;
	float health_percent_opacity = 0.0f;
	float armorbar_opacity = 0.0f;
	float distance_opacity = 0.0f;
	float tool_opacity = 0.0f;
	float flags_opacity = 0.0f;
	float chams_opacity = 0.0f;
	float skeleton_opacity = 0.0f;
	float client_box_opacity = 0.0f;
	float client_box_fill_opacity = 0.0f;
	float client_name_opacity = 0.0f;
	float client_healthbar_opacity = 0.0f;
	float client_health_percent_opacity = 0.0f;
	float client_armorbar_opacity = 0.0f;
	float client_distance_opacity = 0.0f;
	float client_tool_opacity = 0.0f;
	float client_flags_opacity = 0.0f;
	float client_chams_opacity = 0.0f;
	float client_skeleton_opacity = 0.0f;
};

static esp_fade_state_t g_esp_fade_state;

static float update_fade_opacity(float current_opacity, bool enabled, float fade_in_speed, float fade_out_speed)
{
	float target_opacity = enabled ? 1.0f : 0.0f;
	float speed = enabled ? fade_in_speed : fade_out_speed;
	float delta_time = ImGui::GetIO().DeltaTime;

	if (current_opacity < target_opacity)
	{
		current_opacity += speed * delta_time;
		if (current_opacity > target_opacity)
			current_opacity = target_opacity;
	}
	else if (current_opacity > target_opacity)
	{
		current_opacity -= speed * delta_time;
		if (current_opacity < target_opacity)
			current_opacity = target_opacity;
	}

	return current_opacity;
}

static bool is_player_knocked(const cache::entity_t& player)
{
	return player.knocked;
}

static void draw_hitbox_3d(const cache::entity_t& entity, const math::matrix4& view, const math::vector2& dims, ImDrawList* draw, std::uint64_t local_player_address)
{
	if (!settings::visuals::view_hitbox)
		return;

	if (entity.instance.address == local_player_address)
		return;

	if (settings::rage::hitbox_expander::knock_check && is_player_knocked(entity))
		return;

	rbx::part_t hitbox_part{};
	math::vector3 hitbox_pos{};
	math::vector3 hitbox_size{};
	math::matrix3 hitbox_rot{};
	bool has_hitbox = false;

	static const char* const kTargets[3][2] = { { "Head", nullptr }, { "HumanoidRootPart", nullptr }, { "UpperTorso", "Torso" } };
	const int which = (std::clamp)(settings::rage::hitbox_expander::target_part, 0, 2);
	auto part_it = entity.parts.end();
	for (const char* name : kTargets[which])
	{
		if (!name)
			break;
		part_it = entity.parts.find(name);
		if (part_it != entity.parts.end() && part_it->second.address != 0)
			break;
	}
	if (part_it != entity.parts.end() && part_it->second.address != 0)
	{
		hitbox_part = part_it->second;
		rbx::primitive_t prim = hitbox_part.get_primitive();
		if (prim.address != 0)
		{
			hitbox_pos = prim.get_position();
			hitbox_size = prim.get_size();
			hitbox_rot = prim.get_rotation();
			has_hitbox = (hitbox_size.x > 0 && hitbox_size.y > 0 && hitbox_size.z > 0);
		}
	}

	if (!has_hitbox)
		return;

	math::vector3 expanded_size
	{
		settings::rage::hitbox_expander::size_x,
		settings::rage::hitbox_expander::size_y,
		settings::rage::hitbox_expander::size_z
	};

	static const math::vector3 corners[8] =
	{
		{-1, -1, -1}, {1, -1, -1}, {-1, 1, -1}, {1, 1, -1},
		{-1, -1, 1}, {1, -1, 1}, {-1, 1, 1}, {1, 1, 1}
	};

	std::vector<ImVec2> projected_corners;
	projected_corners.reserve(8);

	for (const auto& corner : corners)
	{
		math::vector3 world = hitbox_pos + hitbox_rot * math::vector3
		{
			corner.x * expanded_size.x * 0.5f,
			corner.y * expanded_size.y * 0.5f,
			corner.z * expanded_size.z * 0.5f
		};

		math::vector2 screen{};
		if (game::visengine.world_to_screen(world, screen, dims, view))
		{
			projected_corners.emplace_back(screen.x, screen.y);
		}
		else
		{
			projected_corners.emplace_back(-1.f, -1.f);
		}
	}

	if (projected_corners.size() != 8)
		return;

	ImU32 box_color = IM_COL32(
		static_cast<int>(settings::visuals::view_hitbox_color[0] * 255.f),
		static_cast<int>(settings::visuals::view_hitbox_color[1] * 255.f),
		static_cast<int>(settings::visuals::view_hitbox_color[2] * 255.f),
		static_cast<int>(settings::visuals::view_hitbox_color[3] * 255.f)
	);

	ImDrawListFlags old_flags = draw->Flags;
	draw->Flags |= ImDrawListFlags_AntiAliasedLines;

	const int edges[12][2] =
	{
		{0, 1}, {1, 3}, {3, 2}, {2, 0},
		{4, 5}, {5, 7}, {7, 6}, {6, 4},
		{0, 4}, {1, 5}, {2, 6}, {3, 7}
	};

	for (int i = 0; i < 12; i++)
	{
		int idx1 = edges[i][0];
		int idx2 = edges[i][1];

		if (projected_corners[idx1].x >= 0 && projected_corners[idx1].y >= 0 &&
			projected_corners[idx2].x >= 0 && projected_corners[idx2].y >= 0)
		{
			draw->AddLine(projected_corners[idx1], projected_corners[idx2], box_color, 1.0f);
		}
	}

	draw->Flags = old_flags;
}

void esp::draw_skeleton(ImDrawList* draw, const rig::body_t& body, const math::matrix4& view, const math::vector2& dims, ImU32 color, float thickness)
{
	thread_local std::vector<std::pair<math::vector3, math::vector3>> bones;
	thread_local std::vector<std::pair<ImVec2, ImVec2>> lines;
	rig::skeleton(body, bones);
	lines.clear();
	for (const auto& [a, b] : bones)
	{
		math::vector2 sa{}, sb{};
		if (game::visengine.world_to_screen(a, sa, dims, view) && game::visengine.world_to_screen(b, sb, dims, view))
			lines.emplace_back(ImVec2(sa.x, sa.y), ImVec2(sb.x, sb.y));
	}
	esp_draw::skeleton(draw, lines, color, thickness);
}

void esp::run()
{
	// ESP and chams can each have a key (Toggle, Hold or Always) on top of their switch.
	static keybind::keybind_t esp_key_state{}, chams_key_state{};
	const bool esp_on = keybind::feature_on(settings::visuals::enable_enemies, settings::visuals::esp_key, settings::visuals::esp_key_mode, esp_key_state);
	const bool chams_on = keybind::feature_on(settings::visuals::chams, settings::visuals::chams_key, settings::visuals::chams_key_mode, chams_key_state);

	{
		engine_chams::Settings ec_settings;
		ec_settings.enabled       = chams_on && esp_on && settings::visuals::chams_style == 2;
		ec_settings.through_walls = true;
		ec_settings.fill          = { settings::visuals::chams_fill_color[0], settings::visuals::chams_fill_color[1],
		                              settings::visuals::chams_fill_color[2], settings::visuals::chams_fill_color[3] };
		ec_settings.outline       = { settings::visuals::chams_outline_color[0], settings::visuals::chams_outline_color[1],
		                              settings::visuals::chams_outline_color[2], settings::visuals::chams_outline_color[3] };
		static engine_chams::Settings sent;
		static bool has_sent = false;
		if (!has_sent || sent.enabled != ec_settings.enabled || sent.fill != ec_settings.fill || sent.outline != ec_settings.outline)
		{
			engine_chams::update(ec_settings);
			sent = ec_settings;
			has_sent = true;
		}
	}

	// Early exit if no visuals are enabled
	if (!esp_on && !settings::visuals::enable_client && !settings::visuals::view_hitbox)
	{
		return;
	}

	// Update fade states only when needed (reduce unnecessary calculations)
	static constexpr float FADE_DELTA = 0.01f;
	auto update_fade_if_needed = [](float current, bool enabled, float fade_in, float fade_out) -> float {
		float target = enabled ? 1.0f : 0.0f;
		if (fabsf(current - target) < FADE_DELTA) return target;
		return update_fade_opacity(current, enabled, fade_in, fade_out);
	};

	g_esp_fade_state.box_opacity = update_fade_if_needed(g_esp_fade_state.box_opacity, settings::visuals::box, settings::visuals::fade_in_speed, settings::visuals::fade_out_speed);
	g_esp_fade_state.box_fill_opacity = update_fade_if_needed(g_esp_fade_state.box_fill_opacity, settings::visuals::box_fill, settings::visuals::fade_in_speed, settings::visuals::fade_out_speed);
	g_esp_fade_state.name_opacity = update_fade_if_needed(g_esp_fade_state.name_opacity, settings::visuals::name, settings::visuals::fade_in_speed, settings::visuals::fade_out_speed);
	g_esp_fade_state.healthbar_opacity = update_fade_if_needed(g_esp_fade_state.healthbar_opacity, settings::visuals::healthbar, settings::visuals::fade_in_speed, settings::visuals::fade_out_speed);
	g_esp_fade_state.health_percent_opacity = update_fade_if_needed(g_esp_fade_state.health_percent_opacity, settings::visuals::health_percent, settings::visuals::fade_in_speed, settings::visuals::fade_out_speed);
	g_esp_fade_state.armorbar_opacity = update_fade_if_needed(g_esp_fade_state.armorbar_opacity, settings::visuals::armorbar, settings::visuals::fade_in_speed, settings::visuals::fade_out_speed);
	g_esp_fade_state.distance_opacity = update_fade_if_needed(g_esp_fade_state.distance_opacity, settings::visuals::distance, settings::visuals::fade_in_speed, settings::visuals::fade_out_speed);
	g_esp_fade_state.tool_opacity = update_fade_if_needed(g_esp_fade_state.tool_opacity, settings::visuals::tool, settings::visuals::fade_in_speed, settings::visuals::fade_out_speed);
	g_esp_fade_state.flags_opacity = update_fade_if_needed(g_esp_fade_state.flags_opacity, settings::visuals::flags, settings::visuals::fade_in_speed, settings::visuals::fade_out_speed);
	g_esp_fade_state.chams_opacity = update_fade_if_needed(g_esp_fade_state.chams_opacity, chams_on, settings::visuals::fade_in_speed, settings::visuals::fade_out_speed);
	g_esp_fade_state.skeleton_opacity = update_fade_if_needed(g_esp_fade_state.skeleton_opacity, settings::visuals::skeleton, settings::visuals::fade_in_speed, settings::visuals::fade_out_speed);
	g_esp_fade_state.client_box_opacity = update_fade_if_needed(g_esp_fade_state.client_box_opacity, settings::visuals::client_box, settings::visuals::fade_in_speed, settings::visuals::fade_out_speed);
	g_esp_fade_state.client_box_fill_opacity = update_fade_if_needed(g_esp_fade_state.client_box_fill_opacity, settings::visuals::client_box_fill, settings::visuals::fade_in_speed, settings::visuals::fade_out_speed);
	g_esp_fade_state.client_name_opacity = update_fade_if_needed(g_esp_fade_state.client_name_opacity, settings::visuals::client_name, settings::visuals::fade_in_speed, settings::visuals::fade_out_speed);
	g_esp_fade_state.client_healthbar_opacity = update_fade_if_needed(g_esp_fade_state.client_healthbar_opacity, settings::visuals::client_healthbar, settings::visuals::fade_in_speed, settings::visuals::fade_out_speed);
	g_esp_fade_state.client_health_percent_opacity = update_fade_if_needed(g_esp_fade_state.client_health_percent_opacity, settings::visuals::client_health_percent, settings::visuals::fade_in_speed, settings::visuals::fade_out_speed);
	g_esp_fade_state.client_armorbar_opacity = update_fade_if_needed(g_esp_fade_state.client_armorbar_opacity, settings::visuals::client_armorbar, settings::visuals::fade_in_speed, settings::visuals::fade_out_speed);
	g_esp_fade_state.client_distance_opacity = update_fade_if_needed(g_esp_fade_state.client_distance_opacity, settings::visuals::client_distance, settings::visuals::fade_in_speed, settings::visuals::fade_out_speed);
	g_esp_fade_state.client_tool_opacity = update_fade_if_needed(g_esp_fade_state.client_tool_opacity, settings::visuals::client_tool, settings::visuals::fade_in_speed, settings::visuals::fade_out_speed);
	g_esp_fade_state.client_flags_opacity = update_fade_if_needed(g_esp_fade_state.client_flags_opacity, settings::visuals::client_flags, settings::visuals::fade_in_speed, settings::visuals::fade_out_speed);
	g_esp_fade_state.client_chams_opacity = update_fade_if_needed(g_esp_fade_state.client_chams_opacity, settings::visuals::client_chams, settings::visuals::fade_in_speed, settings::visuals::fade_out_speed);
	g_esp_fade_state.client_skeleton_opacity = update_fade_if_needed(g_esp_fade_state.client_skeleton_opacity, settings::visuals::client_skeleton, settings::visuals::fade_in_speed, settings::visuals::fade_out_speed);

	if (!esp_on && !settings::visuals::enable_client && !settings::visuals::view_hitbox)
	{
		return;
	}

	// Pre-calculate constants once per frame
	static constexpr math::vector3 corners[8] =
	{
		{-1, -1, -1}, {1, -1, -1}, {-1, 1, -1},{1, 1, -1},
		{-1, -1, 1}, {1, -1, 1}, {-1, 1, 1}, {1, 1, 1}
	};

	game::update_window_offset();

	const math::vector2 dims = game::visengine.get_dimensions();
	const math::matrix4 view = game::visengine.get_viewmatrix();

	// Snapshot with minimal lock time
	std::vector<cache::entity_t> snapshot;
	{
		std::lock_guard<std::mutex> lock(cache::mtx);
		snapshot.reserve(cache::cached_players.size());
		snapshot = cache::cached_players;
	}

	std::uint64_t local_player_address = 0;
	if (game::players.address != 0)
	{
		local_player_address = memory->read<std::uint64_t>(game::players.address + Offsets::Player::LocalPlayer);
	}

	// One map snapshot for the whole frame (read on the wall check's own thread).
	const std::shared_ptr<const wallcheck_snapshot_t> wall_snap = settings::visuals::debug_wallcheck ? wallcheck->snapshot() : nullptr;
	const bool wallcheck_cached = wall_snap != nullptr;

	math::vector3 camera_pos{};
	bool has_camera = false;
	if (settings::visuals::debug_wallcheck && game::camera != 0)
	{
		camera_pos = memory->read<math::vector3>(game::camera + Offsets::Camera::Position);
		has_camera = true;
	}

	// Pre-compute local player position for ESP max distance filter
	math::vector3 esp_local_pos{};
	bool esp_has_local_pos = false;
	if (settings::visuals::max_distance_enabled)
	{
		auto lp_it = cache::cached_local_player.parts.find("HumanoidRootPart");
		if (lp_it == cache::cached_local_player.parts.end() || !lp_it->second.address)
			lp_it = cache::cached_local_player.parts.find("Head");
		if (lp_it != cache::cached_local_player.parts.end() && lp_it->second.address)
		{
			rbx::primitive_t lp_prim = lp_it->second.get_primitive();
			if (lp_prim.address)
			{
				esp_local_pos = lp_prim.get_position();
				esp_has_local_pos = std::isfinite(esp_local_pos.x) && std::isfinite(esp_local_pos.y) && std::isfinite(esp_local_pos.z);
			}
		}
		// Fallback to camera position (needed for PF where local player has no cached parts)
		if (!esp_has_local_pos && game::camera != 0)
		{
			try {
				esp_local_pos = memory->read<math::vector3>(game::camera + Offsets::Camera::Position);
				esp_has_local_pos = std::isfinite(esp_local_pos.x) && std::isfinite(esp_local_pos.y) && std::isfinite(esp_local_pos.z);
			} catch (...) {}
		}
	}

	// Your own root once a frame, for distance labels (it used to be searched for per player).
	math::vector3 local_root_pos{};
	bool has_local_root_pos = false;
	{
		std::uint64_t local_root = 0;
		{
			std::lock_guard<std::mutex> lock(cache::mtx);
			auto it = cache::cached_local_player.parts.find("HumanoidRootPart");
			if (it != cache::cached_local_player.parts.end())
				local_root = it->second.address;
		}
		has_local_root_pos = local_root != 0 && rig::position(local_root, local_root_pos);
	}

	// The chams layer goes in first so boxes, names and bars draw over it.
	const bool gpu_chams = (chams_on || settings::visuals::client_chams)
		&& settings::visuals::chams_style != 2
		&& body_chams::begin(ImGui::GetBackgroundDrawList());
	rig::body_t body;

	for (cache::entity_t& entity : snapshot)
	{
		bool valid = false;
		float left = FLT_MAX, top = FLT_MAX;
		float right = -FLT_MAX, bottom = -FLT_MAX;

		// Skip invalid entities early
		if (entity.instance.address == 0)
		{
			continue;
		}

		const bool is_local_player = (entity.instance.address == local_player_address) ||
		(cache::cached_local_player.instance.address != 0 && entity.instance.address == cache::cached_local_player.instance.address);

		// Early skip for disabled player types
		if (!settings::visuals::view_hitbox)
		{
			if ((is_local_player && !settings::visuals::enable_client) ||
				(!is_local_player && !esp_on))
			{
				continue;
			}
		}

		// Skip knocked players only for non-local
		if (!is_local_player && settings::visuals::knock_check && entity.humanoid.address != 0 && is_player_knocked(entity))
		{
			continue;
		}

		// Skip teammates
		if (!is_local_player && settings::visuals::teamcheck && entity.team != 0 && entity.team == cache::cached_local_player.team)
		{
			continue;
		}

		// Skip whitelisted players if ignore setting is on
		bool is_whitelisted_player = !is_local_player && settings::rage::is_whitelisted(entity.name);
		if (is_whitelisted_player && settings::visuals::ignore_whitelisted)
		{
			continue;
		}

		// Skip players beyond max distance
		if (!is_local_player && settings::visuals::max_distance_enabled && esp_has_local_pos)
		{
			auto tgt_it = entity.parts.find("HumanoidRootPart");
			if (tgt_it == entity.parts.end() || !tgt_it->second.address)
				tgt_it = entity.parts.find("Head");
			if (tgt_it != entity.parts.end() && tgt_it->second.address)
			{
				rbx::primitive_t tgt_prim = tgt_it->second.get_primitive();
				if (tgt_prim.address)
				{
					math::vector3 tgt_pos = tgt_prim.get_position();
					math::vector3 diff = { tgt_pos.x - esp_local_pos.x, tgt_pos.y - esp_local_pos.y, tgt_pos.z - esp_local_pos.z };
					float dist = std::sqrtf(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);
					if (dist > settings::visuals::max_distance)
						continue;
				}
			}
		}

		// One read of the body per frame, shared by the box, skeleton, chams and wall check.
		if (!rig::read(entity, body))
			continue;

		for (const rig::part_t& p : body.parts)
		{
			const float half_x = p.size.x * 0.5f;
			const float half_y = p.size.y * 0.5f;
			const float half_z = p.size.z * 0.5f;
			for (int i = 0; i < 8; ++i)
			{
				const math::vector3& corner = corners[i];
				const math::vector3 world = p.pos + p.rot * math::vector3{ corner.x * half_x, corner.y * half_y, corner.z * half_z };
				math::vector2 screen{};
				if (game::visengine.world_to_screen(world, screen, dims, view))
				{
					valid = true;
					if (screen.x < left) left = screen.x;
					if (screen.y < top) top = screen.y;
					if (screen.x > right) right = screen.x;
					if (screen.y > bottom) bottom = screen.y;
				}
			}
		}

		ImDrawList* draw = ImGui::GetBackgroundDrawList();

		if (!valid || left >= right || top >= bottom)
		{
			if (settings::visuals::view_hitbox)
			{
				draw_hitbox_3d(entity, view, dims, draw, local_player_address);
			}
			continue;
		}

		ImVec2 c1(left, top);
		ImVec2 c2(right - left, bottom - top);

		float opacity_multiplier = 1.0f;
		bool is_behind_wall = false;
		if (settings::visuals::debug_wallcheck && has_camera && wallcheck_cached)
		{
			// Behind a wall only when the head and the torso are both hidden.
			static const char* const sight_points[] = { "Head", "UpperTorso", "Torso", "LowerTorso" };
			bool has_target = false;
			bool any_visible = false;
			for (const char* point : sight_points)
			{
				const rig::part_t* p = body.find(point);
				if (!p)
					continue;
				has_target = true;
				if (wall_snap->is_visible(camera_pos, p->pos))
				{
					any_visible = true;
					break;
				}
			}
			if (!has_target && !body.parts.empty())
			{
				has_target = true;
				any_visible = wall_snap->is_visible(camera_pos, body.parts.front().pos);
			}

			if (has_target && !any_visible)
			{
				is_behind_wall = true;
				opacity_multiplier = 0.3f;
			}
		}

		const bool want_skeleton = is_local_player ? settings::visuals::client_skeleton : settings::visuals::skeleton;
		if (want_skeleton)
		{
			const float* sc = is_local_player ? settings::visuals::client_skeleton_color : settings::visuals::skeleton_color;
			const float skeleton_fade = is_local_player ? g_esp_fade_state.client_skeleton_opacity : g_esp_fade_state.skeleton_opacity;
			ImU32 skeleton_col = helper::color_with_opacity(sc[0], sc[1], sc[2], sc[3], opacity_multiplier * skeleton_fade);
			if (!is_local_player && (settings::visuals::use_team_color || settings::visuals::mm2_esp) && entity.has_team_color)
				skeleton_col = helper::color_with_opacity(entity.team_color[0], entity.team_color[1], entity.team_color[2], sc[3], opacity_multiplier * skeleton_fade);
			esp::draw_skeleton(draw, body, view, dims, skeleton_col, settings::visuals::skeleton_thickness);
		}

		if (settings::visuals::esp_style == 1)
		{
			bool want_box = is_local_player ? settings::visuals::client_box : settings::visuals::box;
			float actual_box_left = roundf(left);
			float actual_box_right = roundf(left) + roundf(right - left);

			bool want_box_fill = is_local_player ? settings::visuals::client_box_fill : settings::visuals::box_fill;
			float box_fill_fade = is_local_player ? g_esp_fade_state.client_box_fill_opacity : g_esp_fade_state.box_fill_opacity;
			if (want_box_fill)
			{
				ImU32 fill_col;
				if (is_local_player)
				{
					fill_col = helper::color_with_opacity(
						settings::visuals::client_box_fill_color[0],
						settings::visuals::client_box_fill_color[1],
						settings::visuals::client_box_fill_color[2],
						settings::visuals::client_box_fill_color[3],
						opacity_multiplier * box_fill_fade
					);
				}
				else
				{
					fill_col = helper::color_with_opacity(
						settings::visuals::box_fill_color[0],
						settings::visuals::box_fill_color[1],
						settings::visuals::box_fill_color[2],
						settings::visuals::box_fill_color[3],
						opacity_multiplier * box_fill_fade
					);
				}

				ImRect fill_rect(c1.x, c1.y, c1.x + c2.x, c1.y + c2.y);
				draw->AddRectFilled(fill_rect.Min, fill_rect.Max, fill_col);
			}

			if (want_box)
			{
				float box_fade = is_local_player ? g_esp_fade_state.client_box_opacity : g_esp_fade_state.box_opacity;
				ImU32 box_col;
				if (is_local_player)
				{
					box_col = helper::color_with_opacity(
						settings::visuals::client_box_color[0],
						settings::visuals::client_box_color[1],
						settings::visuals::client_box_color[2],
						settings::visuals::client_box_color[3],
						opacity_multiplier * box_fade
					);
				}
				else if ((settings::visuals::use_team_color || settings::visuals::mm2_esp) && entity.has_team_color)
				{
					box_col = helper::color_with_opacity(
						entity.team_color[0],
						entity.team_color[1],
						entity.team_color[2],
						1.0f,
						opacity_multiplier * box_fade
					);
				}
				else
				{
					box_col = helper::color_with_opacity(
						settings::visuals::box_color[0],
						settings::visuals::box_color[1],
						settings::visuals::box_color[2],
						settings::visuals::box_color[3],
						opacity_multiplier * box_fade
					);
				}

				if (is_whitelisted_player) box_col = helper::apply_opacity(IM_COL32(77, 255, 77, 255), opacity_multiplier * box_fade);

				// Target override — red box for targeted player
				if (!is_local_player && !settings::rage::playerlist::target_name.empty() && entity.name == settings::rage::playerlist::target_name)
					box_col = helper::apply_opacity(IM_COL32(255, 50, 50, 255), opacity_multiplier * box_fade);

				ImRect rect_bb(c1.x, c1.y, c1.x + c2.x, c1.y + c2.y);
				if (settings::visuals::box_type == 0)
				{
					helper::box(c1, c2, box_col);
				}
				else if (settings::visuals::box_type == 1)
				{
					helper::corner_box(draw, rect_bb.Min, rect_bb.Max, box_col, 1.f);
				}
				else if (settings::visuals::box_type == 2)
				{
					helper::rounded_box(draw, rect_bb.Min, rect_bb.Max, box_col, 5.f);
				}
				else if (settings::visuals::box_type == 3)
				{
					helper::cross_box(draw, rect_bb.Min, rect_bb.Max, box_col);
				}
				else if (settings::visuals::box_type == 4)
				{
					helper::glow_box(draw, rect_bb.Min, rect_bb.Max, box_col, settings::visuals::box_glow_strength);
				}
				else if (settings::visuals::box_type == 5)
				{
					helper::diamond_box(draw, rect_bb.Min, rect_bb.Max, box_col);
				}
				actual_box_left = roundf(rect_bb.Min.x);
				actual_box_right = roundf(rect_bb.Max.x);
			}


			float box_center_x = left + (right - left) * 0.5f;
			float box_top = roundf(top);
			float box_bottom = roundf(bottom);
			float box_left = roundf(left);
			float box_right = roundf(right);
			float box_width = box_right - box_left;
			float box_height = box_bottom - box_top;

			ImFont* const font = esp_font ? 
				(settings::visuals::esp_font == 0 ? esp_font_tahoma : 
				 settings::visuals::esp_font == 1 ? esp_font_sp7 : esp_font_arial) :
				ImGui::GetFont();
			const float font_size = esp_font ? 
				(settings::visuals::esp_font == 0 ? font_config::tahoma::font_size :
				 settings::visuals::esp_font == 1 ? font_config::sp7::font_size : font_config::arial::font_size) :
				ImGui::GetFontSize();

			bool want_healthbar = is_local_player ? settings::visuals::client_healthbar : settings::visuals::healthbar;
			float healthbar_fade = is_local_player ? g_esp_fade_state.client_healthbar_opacity : g_esp_fade_state.healthbar_opacity;
			if (want_healthbar && entity.humanoid.address != 0 && entity.max_health > 0.f)
			{
				const float raw_health = entity.health;
				const float max_health = entity.max_health;

				health_smooth_state_t& state = g_health_smooth_states[entity.humanoid.address];
				state.smooth_health += (raw_health - state.smooth_health) * 0.15f;
				state.smooth_health = helper::clamp(state.smooth_health, 0.0f, max_health);

				const float health_percent = state.smooth_health / max_health;
				const float health_percent_actual = health_percent * 100.f;
				const bool use_health_based = settings::visuals::health_based_healthbar;

				// Optimized color selection with branchless approach where possible
				float color_r, color_g, color_b;
				if (use_health_based)
				{
					if (health_percent_actual > 60.f) { color_r = 0.f; color_g = 1.f; color_b = 0.f; }
					else if (health_percent_actual >= 50.f) { color_r = 1.f; color_g = 0.647f; color_b = 0.f; }
					else if (health_percent_actual <= 20.f) { color_r = 1.f; color_g = 0.f; color_b = 0.f; }
					else { color_r = 1.f; color_g = 0.647f; color_b = 0.f; }
				}
				else
				{
					const float* color = is_local_player ? settings::visuals::client_healthbar_color : settings::visuals::healthbar_color;
					color_r = color[0];
					color_g = color[1];
					color_b = color[2];
				}

				const float transparency = 255.f * opacity_multiplier * healthbar_fade;
				const float health_bar_height = box_height + 2.f;
				const int bg_alpha = static_cast<int>(255 * opacity_multiplier * healthbar_fade);
				draw->AddRectFilled(ImVec2(box_left - 7.f, box_top - 2.f), ImVec2(box_left - 3.f, box_bottom + 2.f), IM_COL32(0, 0, 0, bg_alpha));
				if (health_percent > 0.f)
				{
					const float health_fill_height = health_bar_height * health_percent;
					const ImVec2 fill_min(box_left - 6.f, box_bottom + 1.f - health_fill_height);
					const ImVec2 fill_max(box_left - 4.f, box_bottom + 1.f);
					draw->AddRectFilled(fill_min, fill_max, IM_COL32(static_cast<int>(color_r * 255.f), static_cast<int>(color_g * 255.f), static_cast<int>(color_b * 255.f), static_cast<int>(transparency)));
				}
			}

			bool want_health_percent = is_local_player ? settings::visuals::client_health_percent : settings::visuals::health_percent;
			float health_percent_fade = is_local_player ? g_esp_fade_state.client_health_percent_opacity : g_esp_fade_state.health_percent_opacity;
			if (want_health_percent && entity.humanoid.address != 0 && entity.max_health > 0.f)
			{
				float raw_health = entity.health;
				float max_health = entity.max_health;

				health_smooth_state_t& state = g_health_smooth_states[entity.humanoid.address];
				state.smooth_health += (raw_health - state.smooth_health) * 0.15f;
				state.smooth_health = max(0.0f, min(state.smooth_health, max_health));

				float health_percent = state.smooth_health / max_health;
				if (health_percent < 0.f) health_percent = 0.f;
				if (health_percent > 1.f) health_percent = 1.f;

				float health_percent_actual = health_percent * 100.f;
				int health_ceil = static_cast<int>(std::ceilf(health_percent_actual));

				if (health_ceil != static_cast<int>(max_health))
				{
					std::string health_text = std::to_string(health_ceil);
					ImVec2 text_size = font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, health_text.c_str());

					float health_bar_height = box_height + 2.f;
					float health_fill_height = health_bar_height * health_percent;
					float health_bar_top = roundf(box_bottom) + 1.f - health_fill_height;

					float health_bar_left = roundf(box_left) - 7.f;
					float health_bar_right = roundf(box_left) - 3.f;
					float health_bar_center_x = (health_bar_left + health_bar_right) * 0.5f;

					ImVec2 health_text_pos = ImVec2(roundf(health_bar_center_x - text_size.x * 0.5f), roundf(health_bar_top));

					ImU32 health_text_color;
					if (is_local_player)
					{
						health_text_color = helper::color_with_opacity(
							settings::visuals::client_health_percent_color[0],
							settings::visuals::client_health_percent_color[1],
							settings::visuals::client_health_percent_color[2],
							settings::visuals::client_health_percent_color[3],
							opacity_multiplier * health_percent_fade
						);
					}
					else
					{
						health_text_color = helper::color_with_opacity(
							settings::visuals::health_percent_color[0],
							settings::visuals::health_percent_color[1],
							settings::visuals::health_percent_color[2],
							settings::visuals::health_percent_color[3],
							opacity_multiplier * health_percent_fade
						);
					}

					helper::draw_text_outlined(draw, font, font_size, health_text_pos, health_text_color, health_text.c_str());
				}
			}

			bool want_armorbar = is_local_player ? settings::visuals::client_armorbar : settings::visuals::armorbar;
			float armorbar_fade = is_local_player ? g_esp_fade_state.client_armorbar_opacity : g_esp_fade_state.armorbar_opacity;
			bool armor_bar_rendered = false;

			if (want_armorbar && entity.humanoid.address != 0)
			{
				const float raw_armor_percent = entity.armor_percent;
				const int armor_value = entity.armor_value;

				health_smooth_state_t& state = g_health_smooth_states[entity.humanoid.address];
				state.smooth_armor += (raw_armor_percent - state.smooth_armor) * 0.15f;
				state.smooth_armor = helper::clamp(state.smooth_armor, 0.0f, 1.0f);

				float armor_percent = state.smooth_armor;

				if (armor_value > 0 && armor_percent > 0.001f)
				{
					armor_bar_rendered = true;
					float bar_height = 2.0f;
					float bar_y = box_bottom + 4.0f;
					float bar_left = box_left - 1.0f;
					float bar_right = box_right + 1.0f;
					float bar_width = bar_right - bar_left;
					float fill_width = bar_width * armor_percent;

					ImDrawListFlags old_flags = draw->Flags;
					draw->Flags &= ~ImDrawListFlags_AntiAliasedFill;
					draw->Flags |= ImDrawListFlags_AntiAliasedLines;

					draw->AddRectFilled(ImVec2(bar_left - 1.0f, bar_y - 1.0f), ImVec2(bar_right + 1.0f, bar_y + bar_height + 1.0f), IM_COL32(0, 0, 0, 255));
					draw->AddRectFilled(ImVec2(bar_left, bar_y), ImVec2(bar_right, bar_y + bar_height), IM_COL32(45, 45, 45, 220));

					ImU32 armor_col;
					if (is_local_player)
					{
						armor_col = helper::color_with_opacity(
							settings::visuals::client_armorbar_color[0],
							settings::visuals::client_armorbar_color[1],
							settings::visuals::client_armorbar_color[2],
							settings::visuals::client_armorbar_color[3],
							opacity_multiplier * armorbar_fade
						);
					}
					else
					{
						armor_col = helper::color_with_opacity(
							settings::visuals::armorbar_color[0],
							settings::visuals::armorbar_color[1],
							settings::visuals::armorbar_color[2],
							settings::visuals::armorbar_color[3],
							opacity_multiplier * armorbar_fade
						);
					}

					if (fill_width > 0.0f)
					{
						draw->AddRectFilled(ImVec2(bar_left, bar_y), ImVec2(bar_left + fill_width, bar_y + bar_height), armor_col);
					}

					draw->Flags = old_flags;
				}
			}

			bool want_name = is_local_player ? settings::visuals::client_name : settings::visuals::name;
			float name_fade = is_local_player ? g_esp_fade_state.client_name_opacity : g_esp_fade_state.name_opacity;
			if (want_name)
			{
				std::string name_to_display = (settings::visuals::name_display_type == 0) ? entity.display_name : entity.name;
				if (is_whitelisted_player) name_to_display = "[WL] " + name_to_display;
				float text_width = font->CalcTextSizeA(font_size, FLT_MAX, 0.f, name_to_display.c_str()).x;
				int text_len = static_cast<int>(name_to_display.length());
				float actual_width = text_width + (text_len > 0 ? (text_len - 1) * 1.0f : 0.0f);
				float name_x = box_center_x - actual_width * 0.5f;
				float name_y = roundf(box_top) - font_size - 5.f;

				float avatar_size = font_size * 1.5f;
				float avatar_spacing = 5.0f;

				bool want_avatar = is_local_player ? settings::visuals::client_avatar : settings::visuals::avatar;
				if (want_avatar && g_avatar_manager && entity.instance.address != 0)
				{
					std::uint64_t user_id = 0;
					try
					{
						user_id = memory->read<std::uint64_t>(entity.instance.address + Offsets::Player::UserId);
					}
					catch (...)
					{
					}

					if (user_id != 0 && user_id != 0xFFFFFFFFFFFFFFFF)
					{
						g_avatar_manager->requestAvatar(user_id);

						ImTextureID avatar_texture = g_avatar_manager->getAvatarTexture(user_id);
						if (avatar_texture)
						{
							float avatar_x = name_x - avatar_size - avatar_spacing;
							float avatar_y = name_y + (font_size - avatar_size) * 0.5f;

							ImVec2 avatar_pos(avatar_x, avatar_y);

							draw->AddImage(avatar_texture, avatar_pos, ImVec2(avatar_pos.x + avatar_size, avatar_pos.y + avatar_size), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, static_cast<int>(255 * opacity_multiplier * name_fade)));
						}
					}
				}

				if (settings::visuals::blend)
				{
					ImU32 name_col_start = ImGui::ColorConvertFloat4ToU32(
						{
							settings::visuals::name_color_blend_start[0],
							settings::visuals::name_color_blend_start[1],
							settings::visuals::name_color_blend_start[2],
							settings::visuals::name_color_blend_start[3]
						}
					);
					ImU32 name_col_end = ImGui::ColorConvertFloat4ToU32(
						{
							settings::visuals::name_color_blend_end[0],
							settings::visuals::name_color_blend_end[1],
							settings::visuals::name_color_blend_end[2],
							settings::visuals::name_color_blend_end[3]
						}
					);
					name_col_start = helper::apply_opacity(name_col_start, opacity_multiplier * name_fade);
					name_col_end = helper::apply_opacity(name_col_end, opacity_multiplier * name_fade);

					if (is_whitelisted_player)
					{
						ImU32 wl_green = helper::apply_opacity(IM_COL32(77, 255, 77, 255), opacity_multiplier * name_fade);
						name_col_start = wl_green;
						name_col_end = wl_green;
					}
					helper::draw_text_blended(draw, font, font_size,
						ImVec2(name_x, name_y),
						name_col_start, name_col_end, name_to_display.c_str());
				}
				else
				{
					ImU32 name_col;
					if (is_local_player)
					{
						name_col = ImGui::ColorConvertFloat4ToU32(
							{
								settings::visuals::client_name_color[0],
								settings::visuals::client_name_color[1],
								settings::visuals::client_name_color[2],
								settings::visuals::client_name_color[3]
							}
						);
					}
					else if ((settings::visuals::use_team_color || settings::visuals::mm2_esp) && entity.has_team_color)
					{
						name_col = ImGui::ColorConvertFloat4ToU32(
							{
								entity.team_color[0],
								entity.team_color[1],
								entity.team_color[2],
								1.0f
							}
						);
					}
					else
					{
						name_col = ImGui::ColorConvertFloat4ToU32(
							{
								settings::visuals::name_color[0],
								settings::visuals::name_color[1],
								settings::visuals::name_color[2],
								settings::visuals::name_color[3]
							}
						);
					}
					name_col = helper::apply_opacity(name_col, opacity_multiplier * name_fade);
					if (is_whitelisted_player) name_col = helper::apply_opacity(IM_COL32(77, 255, 77, 255), opacity_multiplier * name_fade);

					helper::draw_text_outlined(draw, font, font_size,
						ImVec2(name_x, name_y),
						name_col, name_to_display.c_str());
				}
			}

			math::vector3 target_hrp_pos{};
			bool has_target_hrp = false;
			auto target_hrp_it = entity.parts.find("HumanoidRootPart");
			if (target_hrp_it != entity.parts.end() && target_hrp_it->second.address != 0)
				has_target_hrp = rig::position(target_hrp_it->second.address, target_hrp_pos);

			bool want_distance = is_local_player ? settings::visuals::client_distance : settings::visuals::distance;
			float distance_fade = is_local_player ? g_esp_fade_state.client_distance_opacity : g_esp_fade_state.distance_opacity;
			if (want_distance && has_local_root_pos && has_target_hrp)
			{
				char distance_str[32];
				math::vector3 diff = { target_hrp_pos.x - local_root_pos.x, target_hrp_pos.y - local_root_pos.y, target_hrp_pos.z - local_root_pos.z };
				float distance = std::sqrtf(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);

				if (settings::visuals::distance_measurement == 1)
				{
					distance *= 0.28f;
					std::snprintf(distance_str, sizeof(distance_str), "%.1fm", distance);
				}
				else
				{
					std::snprintf(distance_str, sizeof(distance_str), "%.1f", distance);
				}

				float text_width = font->CalcTextSizeA(font_size, FLT_MAX, 0.f, distance_str).x;
				int text_len = static_cast<int>(std::strlen(distance_str));
				float actual_width = text_width + (text_len > 0 ? (text_len - 1) * 1.0f : 0.0f);

				ImU32 distance_col;
				if (is_local_player)
				{
					distance_col = ImGui::ColorConvertFloat4ToU32(
						{
							settings::visuals::client_distance_color[0],
							settings::visuals::client_distance_color[1],
							settings::visuals::client_distance_color[2],
							settings::visuals::client_distance_color[3]
						}
					);
				}
				else
				{
					distance_col = ImGui::ColorConvertFloat4ToU32(
						{
							settings::visuals::distance_color[0],
							settings::visuals::distance_color[1],
							settings::visuals::distance_color[2],
							settings::visuals::distance_color[3]
						}
					);
				}
				distance_col = helper::apply_opacity(distance_col, opacity_multiplier * distance_fade);

				float distance_y = roundf(box_bottom) + 5.f;
				if (armor_bar_rendered)
				{
					distance_y = roundf(box_bottom) + 5.0f + 2.0f + 3.0f;
				}

				if (settings::visuals::blend)
				{
					ImU32 distance_col_start = ImGui::ColorConvertFloat4ToU32(
						{
							settings::visuals::name_color_blend_start[0],
							settings::visuals::name_color_blend_start[1],
							settings::visuals::name_color_blend_start[2],
							settings::visuals::name_color_blend_start[3]
						}
					);
					ImU32 distance_col_end = ImGui::ColorConvertFloat4ToU32(
						{
							settings::visuals::name_color_blend_end[0],
							settings::visuals::name_color_blend_end[1],
							settings::visuals::name_color_blend_end[2],
							settings::visuals::name_color_blend_end[3]
						}
					);
					distance_col_start = helper::apply_opacity(distance_col_start, opacity_multiplier * distance_fade);
					distance_col_end = helper::apply_opacity(distance_col_end, opacity_multiplier * distance_fade);

					helper::draw_text_blended(draw, font, font_size,
						ImVec2(box_center_x - actual_width * 0.5f, distance_y),
						distance_col_start, distance_col_end, distance_str);
				}
				else
				{
					helper::draw_text_outlined(draw, font, font_size,
						ImVec2(box_center_x - actual_width * 0.5f, distance_y),
						distance_col, distance_str);
				}
			}

			bool team_name_rendered = false;
			if (!is_local_player && settings::visuals::team_name && !entity.team_name.empty())
			{
				float tn_y = roundf(box_bottom) + 5.f;
				if (armor_bar_rendered) tn_y = roundf(box_bottom) + 5.0f + 2.0f + 3.0f;
				if (want_distance) tn_y += font_size + 2.f;
				float tn_w = font->CalcTextSizeA(font_size, FLT_MAX, 0.f, entity.team_name.c_str()).x;
				ImU32 tn_col = helper::color_with_opacity(
					settings::visuals::distance_color[0], settings::visuals::distance_color[1],
					settings::visuals::distance_color[2], settings::visuals::distance_color[3], opacity_multiplier);
				helper::draw_text_outlined(draw, font, font_size,
					ImVec2(box_center_x - tn_w * 0.5f, tn_y), tn_col, entity.team_name.c_str());
				team_name_rendered = true;
			}

			bool want_tool = is_local_player ? settings::visuals::client_tool : settings::visuals::tool;
			float tool_fade = is_local_player ? g_esp_fade_state.client_tool_opacity : g_esp_fade_state.tool_opacity;
			if (want_tool)
			{
				const std::string& tool_name = entity.tool_name;

				if (!tool_name.empty())
				{
					float text_width = font->CalcTextSizeA(font_size, FLT_MAX, 0.f, tool_name.c_str()).x;
					int text_len = static_cast<int>(tool_name.length());
					float actual_width = text_width + (text_len > 0 ? (text_len - 1) * 1.0f : 0.0f);
					float tool_y = roundf(box_bottom) + 5.f;

					if (armor_bar_rendered)
					{
						tool_y = roundf(box_bottom) + 5.0f + 2.0f + 3.0f;
					}

					if (want_distance)
					{
						tool_y += font_size + 2.f;
					}
					if (team_name_rendered)
					{
						tool_y += font_size + 2.f;
					}

					if (settings::visuals::blend)
					{
						ImU32 tool_col_start = ImGui::ColorConvertFloat4ToU32(
							{
								settings::visuals::name_color_blend_start[0],
								settings::visuals::name_color_blend_start[1],
								settings::visuals::name_color_blend_start[2],
								settings::visuals::name_color_blend_start[3]
							}
						);
						ImU32 tool_col_end = ImGui::ColorConvertFloat4ToU32(
							{
								settings::visuals::name_color_blend_end[0],
								settings::visuals::name_color_blend_end[1],
								settings::visuals::name_color_blend_end[2],
								settings::visuals::name_color_blend_end[3]
							}
						);
						tool_col_start = helper::apply_opacity(tool_col_start, opacity_multiplier * tool_fade);
						tool_col_end = helper::apply_opacity(tool_col_end, opacity_multiplier * tool_fade);

						helper::draw_text_blended(draw, font, font_size,
							ImVec2(box_center_x - actual_width * 0.5f, tool_y),
							tool_col_start, tool_col_end, tool_name.c_str());
					}
					else
					{
						ImU32 tool_col;
						if (is_local_player)
						{
							tool_col = helper::color_with_opacity(
								settings::visuals::client_tool_color[0],
								settings::visuals::client_tool_color[1],
								settings::visuals::client_tool_color[2],
								settings::visuals::client_tool_color[3],
								opacity_multiplier * tool_fade
							);
						}
						else
						{
							tool_col = helper::color_with_opacity(
								settings::visuals::tool_color[0],
								settings::visuals::tool_color[1],
								settings::visuals::tool_color[2],
								settings::visuals::tool_color[3],
								opacity_multiplier * tool_fade
							);
						}

						helper::draw_text_outlined(draw, font, font_size,
							ImVec2(box_center_x - actual_width * 0.5f, tool_y),
							tool_col, tool_name.c_str());
					}
				}
			}

			// Target indicator below ESP box
			if (!is_local_player && !settings::rage::playerlist::target_name.empty() && entity.name == settings::rage::playerlist::target_name)
			{
				const char* tag_text = "[TARGET]";
				ImU32 tag_col = helper::apply_opacity(IM_COL32(255, 50, 50, 255), opacity_multiplier);
				float tag_width = font->CalcTextSizeA(font_size, FLT_MAX, 0.f, tag_text).x;
				float tag_y = box_bottom + 2.0f;
				helper::draw_text_outlined(draw, font, font_size,
					ImVec2(box_center_x - tag_width * 0.5f, tag_y),
					tag_col, tag_text);
			}

			// MM2 role indicator — only show role label for Sheriff/Murderer, not Innocent
			if (!is_local_player && settings::visuals::mm2_esp && entity.mm2_role > 1)
			{
				const char* role_text = nullptr;
				ImU32 role_col = 0;
				switch (entity.mm2_role)
				{
				case 3: role_text = "[MURDERER]"; role_col = IM_COL32(255, 38, 38, 255); break;
				case 2: role_text = "[SHERIFF]";  role_col = IM_COL32(38, 102, 255, 255); break;
				}
				if (role_text)
				{
					role_col = helper::apply_opacity(role_col, opacity_multiplier);
					float role_width = font->CalcTextSizeA(font_size, FLT_MAX, 0.f, role_text).x;
					float role_y = box_bottom + 2.0f;
					helper::draw_text_outlined(draw, font, font_size,
						ImVec2(box_center_x - role_width * 0.5f, role_y),
						role_col, role_text);
				}
			}

			bool want_flags = is_local_player ? settings::visuals::client_flags : settings::visuals::flags;
			float flags_fade = is_local_player ? g_esp_fade_state.client_flags_opacity : g_esp_fade_state.flags_opacity;
			if (want_flags)
			{
				float current_y = roundf(box_top);

				if (entity.humanoid.address != 0)
				{
					rbx::humanoid_t humanoid(entity.humanoid.address);
					int humanoid_state = static_cast<int>(humanoid.get_state());
					const char* state = "Unknown";

					if (humanoid_state == 8)
					{
						auto head_it = entity.parts.find("Head");
						if (head_it != entity.parts.end() && head_it->second.address != 0)
						{
							rbx::primitive_t head_prim = head_it->second.get_primitive();
							if (head_prim.address != 0)
							{
								math::vector3 head_velocity = memory->read<math::vector3>(head_prim.address + Offsets::Primitive::AssemblyLinearVelocity);
								if (head_velocity.length() < 0.1f)
								{
									state = "Idle";
								}
							}
						}
					}

					if (std::strcmp(state, "Unknown") == 0)
					{
						static const char* states[] = { "FallingDown", "Ragdoll", "GettingUp", "Jumping", "Swimming", "Freefall", "Flying", "Landed", "Running", "Unknown", "RunningNoPhysics", "StrafingNoPhysics", "Climbing", "Seated", "PlatformStanding", "Dead", "Physics", "Unknown", "None" };
						if (humanoid_state >= 0 && humanoid_state < 19)
						{
							state = states[humanoid_state];
						}
					}

					char state_buffer[64];
					std::snprintf(state_buffer, sizeof(state_buffer), "[%s]", state);

					float text_width = font->CalcTextSizeA(font_size, FLT_MAX, 0.f, state_buffer).x;
					float text_len = static_cast<float>(std::strlen(state_buffer));
					float actual_width = text_width + (text_len > 0 ? (text_len - 1) * 1.0f : 0.0f);
					float flag_x = box_right + 5.f;

					if (settings::visuals::blend)
					{
						ImU32 state_col_start = ImGui::ColorConvertFloat4ToU32(
							{
								settings::visuals::name_color_blend_start[0],
								settings::visuals::name_color_blend_start[1],
								settings::visuals::name_color_blend_start[2],
								settings::visuals::name_color_blend_start[3]
							}
						);
						ImU32 state_col_end = ImGui::ColorConvertFloat4ToU32(
							{
								settings::visuals::name_color_blend_end[0],
								settings::visuals::name_color_blend_end[1],
								settings::visuals::name_color_blend_end[2],
								settings::visuals::name_color_blend_end[3]
							}
						);
						state_col_start = helper::apply_opacity(state_col_start, opacity_multiplier * flags_fade);
						state_col_end = helper::apply_opacity(state_col_end, opacity_multiplier * flags_fade);

						helper::draw_text_blended(draw, font, font_size,
							ImVec2(flag_x, current_y),
							state_col_start, state_col_end, state_buffer);
					}
					else
					{
						ImU32 state_col;
						if (is_local_player)
						{
							state_col = helper::color_with_opacity(
								settings::visuals::client_flags_state_colour[0],
								settings::visuals::client_flags_state_colour[1],
								settings::visuals::client_flags_state_colour[2],
								settings::visuals::client_flags_state_colour[3],
								opacity_multiplier * flags_fade
							);
						}
						else
						{
							state_col = helper::color_with_opacity(
								settings::visuals::flags_state_colour[0],
								settings::visuals::flags_state_colour[1],
								settings::visuals::flags_state_colour[2],
								settings::visuals::flags_state_colour[3],
								opacity_multiplier * flags_fade
							);
						}

						helper::draw_text_outlined(draw, font, font_size, ImVec2(flag_x, current_y), state_col, state_buffer);
					}
				}
			}
		}
		else
		{
			// The natware look (esp_draw.cpp).
			const float dim = opacity_multiplier;
			const bool is_target_player = !is_local_player && !settings::rage::playerlist::target_name.empty()
				&& entity.name == settings::rage::playerlist::target_name;
			const bool team_tint = !is_local_player && (settings::visuals::use_team_color || settings::visuals::mm2_esp) && entity.has_team_color;
			auto rgba = [](const float* c, float a) { return helper::color_with_opacity(c[0], c[1], c[2], c[3], a); };

			esp_draw::player_t look{};
			look.min = ImVec2(left, top);
			look.max = ImVec2(right, bottom);

			if (is_local_player ? settings::visuals::client_box : settings::visuals::box)
			{
				const float fade = dim * (is_local_player ? g_esp_fade_state.client_box_opacity : g_esp_fade_state.box_opacity);
				look.box = rgba(is_local_player ? settings::visuals::client_box_color : settings::visuals::box_color, fade);
				if (team_tint)
					look.box = helper::color_with_opacity(entity.team_color[0], entity.team_color[1], entity.team_color[2], 1.0f, fade);
				if (is_whitelisted_player)
					look.box = helper::apply_opacity(IM_COL32(77, 255, 77, 255), fade);
				if (is_target_player)
					look.box = helper::apply_opacity(IM_COL32(255, 60, 60, 255), fade);
				look.box_style = settings::visuals::box_type == 1 ? 1 : settings::visuals::box_type == 4 ? 2 : 0;
			}
			if (is_local_player ? settings::visuals::client_box_fill : settings::visuals::box_fill)
				look.fill = rgba(is_local_player ? settings::visuals::client_box_fill_color : settings::visuals::box_fill_color,
					dim * (is_local_player ? g_esp_fade_state.client_box_fill_opacity : g_esp_fade_state.box_fill_opacity));

			std::string name_text;
			if (is_local_player ? settings::visuals::client_name : settings::visuals::name)
			{
				const float fade = dim * (is_local_player ? g_esp_fade_state.client_name_opacity : g_esp_fade_state.name_opacity);
				name_text = settings::visuals::name_display_type == 0 ? entity.display_name : entity.name;
				look.name = name_text.c_str();
				look.name_col = rgba(settings::visuals::blend ? settings::visuals::name_color_blend_start
					: (is_local_player ? settings::visuals::client_name_color : settings::visuals::name_color), fade);
				if (is_whitelisted_player)
					look.accent = helper::apply_opacity(IM_COL32(77, 255, 77, 255), fade);
				else if (is_target_player)
					look.accent = helper::apply_opacity(IM_COL32(255, 60, 60, 255), fade);
				else if (team_tint)
					look.accent = helper::color_with_opacity(entity.team_color[0], entity.team_color[1], entity.team_color[2], 1.0f, fade);

				const bool want_avatar = is_local_player ? settings::visuals::client_avatar : settings::visuals::avatar;
				if (want_avatar && g_avatar_manager && entity.instance.address != 0)
				{
					const std::uint64_t user_id = memory->read<std::uint64_t>(entity.instance.address + Offsets::Player::UserId);
					if (user_id != 0 && user_id != 0xFFFFFFFFFFFFFFFF)
					{
						g_avatar_manager->requestAvatar(user_id);
						look.avatar = g_avatar_manager->getAvatarTexture(user_id);
					}
				}
			}

			const bool bar_on = is_local_player ? settings::visuals::client_healthbar : settings::visuals::healthbar;
			const bool pct_on = is_local_player ? settings::visuals::client_health_percent : settings::visuals::health_percent;
			if ((bar_on || pct_on) && entity.max_health > 0.f)
			{
				health_smooth_state_t& state = g_health_smooth_states[entity.humanoid.address ? entity.humanoid.address : entity.instance.address];
				state.smooth_health += (entity.health - state.smooth_health) * 0.15f;
				state.smooth_health = helper::clamp(state.smooth_health, 0.0f, entity.max_health);
				look.health = state.smooth_health / entity.max_health;
				look.bar_alpha = bar_on ? dim * (is_local_player ? g_esp_fade_state.client_healthbar_opacity : g_esp_fade_state.healthbar_opacity) : 0.0f;
				if (bar_on && !settings::visuals::health_based_healthbar)
					look.health_col = rgba(is_local_player ? settings::visuals::client_healthbar_color : settings::visuals::healthbar_color, look.bar_alpha);
				if (pct_on)
				{
					look.health_text = true;
					look.health_text_col = rgba(is_local_player ? settings::visuals::client_health_percent_color : settings::visuals::health_percent_color,
						dim * (is_local_player ? g_esp_fade_state.client_health_percent_opacity : g_esp_fade_state.health_percent_opacity));
				}
			}

			if ((is_local_player ? settings::visuals::client_armorbar : settings::visuals::armorbar) && entity.armor_value > 0)
			{
				health_smooth_state_t& state = g_health_smooth_states[entity.humanoid.address ? entity.humanoid.address : entity.instance.address];
				state.smooth_armor += (entity.armor_percent - state.smooth_armor) * 0.15f;
				state.smooth_armor = helper::clamp(state.smooth_armor, 0.0f, 1.0f);
				if (state.smooth_armor > 0.001f)
				{
					look.armor = state.smooth_armor;
					look.armor_col = rgba(is_local_player ? settings::visuals::client_armorbar_color : settings::visuals::armorbar_color,
						dim * (is_local_player ? g_esp_fade_state.client_armorbar_opacity : g_esp_fade_state.armorbar_opacity));
				}
			}

			// Distance and tool share one line under the box.
			char info[160] = {};
			const bool dist_on = is_local_player ? settings::visuals::client_distance : settings::visuals::distance;
			const bool tool_on = (is_local_player ? settings::visuals::client_tool : settings::visuals::tool) && !entity.tool_name.empty();
			if (dist_on && !is_local_player && has_local_root_pos)
			{
				math::vector3 root{};
				auto hrp = entity.parts.find("HumanoidRootPart");
				if (hrp != entity.parts.end() && hrp->second.address && rig::position(hrp->second.address, root))
				{
					float d = root.distance(local_root_pos);
					if (settings::visuals::distance_measurement == 1)
						snprintf(info, sizeof(info), "%.0f m", d * 0.28f);
					else
						snprintf(info, sizeof(info), "%.0f studs", d);
				}
			}
			if (!is_local_player && settings::visuals::team_name && !entity.team_name.empty())
			{
				const size_t n = std::strlen(info);
				snprintf(info + n, sizeof(info) - n, "%s%s", n ? "  \xC2\xB7  " : "", entity.team_name.c_str());
			}
			if (tool_on)
			{
				const size_t n = std::strlen(info);
				snprintf(info + n, sizeof(info) - n, "%s%s", n ? "  \xC2\xB7  " : "", entity.tool_name.c_str());
			}
			if (info[0])
			{
				look.info = info;
				look.info_col = rgba(dist_on ? (is_local_player ? settings::visuals::client_distance_color : settings::visuals::distance_color)
					: (is_local_player ? settings::visuals::client_tool_color : settings::visuals::tool_color),
					dim * (dist_on ? (is_local_player ? g_esp_fade_state.client_distance_opacity : g_esp_fade_state.distance_opacity)
						: (is_local_player ? g_esp_fade_state.client_tool_opacity : g_esp_fade_state.tool_opacity)));
			}

			if ((is_local_player ? settings::visuals::client_flags : settings::visuals::flags) && entity.humanoid.address != 0)
			{
				static const char* states[] = { "Falling", "Ragdoll", "Getting up", "Jumping", "Swimming", "Freefall", "Flying", "Landed",
					"Running", "Unknown", "Running", "Strafing", "Climbing", "Seated", "Standing", "Dead", "Physics", "Unknown", "None" };
				const int st = static_cast<int>(rbx::humanoid_t(entity.humanoid.address).get_state());
				look.flag = (st >= 0 && st < 19) ? states[st] : "Unknown";
				look.flag_col = rgba(is_local_player ? settings::visuals::client_flags_state_colour : settings::visuals::flags_state_colour,
					dim * (is_local_player ? g_esp_fade_state.client_flags_opacity : g_esp_fade_state.flags_opacity));
			}

			if (is_target_player)
			{
				look.tag = "TARGET";
				look.tag_col = helper::apply_opacity(IM_COL32(255, 60, 60, 255), dim);
			}
			else if (!is_local_player && settings::visuals::mm2_esp && entity.mm2_role > 1)
			{
				look.tag = entity.mm2_role == 3 ? "MURDERER" : "SHERIFF";
				look.tag_col = helper::apply_opacity(entity.mm2_role == 3 ? IM_COL32(255, 38, 38, 255) : IM_COL32(38, 102, 255, 255), dim);
			}

			esp_draw::player(draw, look);
		}

		bool want_chams = is_local_player ? settings::visuals::client_chams : chams_on;
		float chams_fade = is_local_player ? g_esp_fade_state.client_chams_opacity : g_esp_fade_state.chams_opacity;
		if (want_chams && gpu_chams)
		{
			const float* fc = is_local_player ? settings::visuals::client_chams_fill_color : settings::visuals::chams_fill_color;
			const float* oc = is_local_player ? settings::visuals::client_chams_outline_color : settings::visuals::chams_outline_color;
			const float mul = opacity_multiplier * chams_fade;
			float fill[4] = { fc[0], fc[1], fc[2], settings::visuals::chams_fill_enabled ? fc[3] * mul : 0.0f };
			float outline[4] = { oc[0], oc[1], oc[2], settings::visuals::chams_outline_enabled ? oc[3] * mul : 0.0f };
			// Team and role colours tint the fill the way they tint the box.
			if (!is_local_player && (settings::visuals::use_team_color || settings::visuals::mm2_esp) && entity.has_team_color)
				for (int i = 0; i < 3; ++i)
					fill[i] = entity.team_color[i];
			if (is_whitelisted_player)
			{
				fill[0] = 0.3f;
				fill[1] = 1.0f;
				fill[2] = 0.3f;
			}
			body_chams::add(body, fill, outline);
		}

		draw_hitbox_3d(entity, view, dims, draw, local_player_address);
	}

	if (gpu_chams)
	{
		body_chams::frame_t frame;
		frame.view_projection = view;
		frame.dims = dims;
		frame.offset = { game::window_offset_x, game::window_offset_y };
		frame.effect = settings::visuals::chams_style == 1 ? body_chams::effect_water + settings::visuals::chams_effect : body_chams::effect_flat;
		frame.speed = settings::visuals::chams_texture_speed;
		frame.scale = settings::visuals::chams_texture_scale;
		body_chams::end(frame);
	}

	if (settings::custom_entities::show_custom_entities)
	{
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

				cache::entity_t entity;
				entity.instance = custom_entity.instance;
				entity.name = custom_entity.name;
				entity.display_name = custom_entity.name;

				for (const auto& part_pair : custom_entity.parts)
				{
					entity.parts[part_pair.first] = part_pair.second.part;
				}

				bool valid = false;
				float left = FLT_MAX, top = FLT_MAX;
				float right = -FLT_MAX, bottom = -FLT_MAX;

				static const math::vector3 corners[8] = {
					{ -1, -1, -1 },
					{ 1, -1, -1 },
					{ -1, 1, -1 },
					{ 1, 1, -1 },
					{ -1, -1, 1 },
					{ 1, -1, 1 },
					{ -1, 1, 1 },
					{ 1, 1, 1 }
				};

				for (const auto& part_pair : custom_entity.parts)
				{
					if (!part_pair.second.part.address) continue;

					try
					{
						rbx::part_t part_copy = part_pair.second.part;
						rbx::primitive_t primitive = part_copy.get_primitive();
						if (!primitive.address) continue;

						math::vector3 pos = primitive.get_position();
						math::vector3 size = primitive.get_size();
						math::matrix3 rot = primitive.get_rotation();

						if (size.x == 0 || size.y == 0 || size.z == 0)
							continue;

						for (const auto& corner : corners)
						{
							math::vector3 world = pos + rot * math::vector3
							{
								corner.x * size.x * 0.5f,
								corner.y * size.y * 0.5f,
								corner.z * size.z * 0.5f
							};

							math::vector2 out{};
							if (game::visengine.world_to_screen(world, out, dims, view))
							{
								valid = true;
								left = min(left, out.x);
								top = min(top, out.y);
								right = max(right, out.x);
								bottom = max(bottom, out.y);
							}
						}
					}
					catch (...)
					{
						continue;
					}
				}

				if (!valid || left >= right || top >= bottom) continue;

				ImDrawList* draw = ImGui::GetBackgroundDrawList();

				if (settings::visuals::box)
				{
					ImVec2 c1 = { left, top };
					ImVec2 c2 = { right - left, bottom - top };
					ImU32 box_col = IM_COL32(
						static_cast<int>(settings::visuals::box_color[0] * 255.f),
						static_cast<int>(settings::visuals::box_color[1] * 255.f),
						static_cast<int>(settings::visuals::box_color[2] * 255.f),
						static_cast<int>(settings::visuals::box_color[3] * 255.f)
					);
					helper::box(c1, c2, box_col);
				}

				if (settings::visuals::name)
				{
					std::string name_to_display = entity.name;
					ImVec2 text_pos = { left, top - 20.f };
					ImU32 name_col = IM_COL32(
						static_cast<int>(settings::visuals::name_color[0] * 255.f),
						static_cast<int>(settings::visuals::name_color[1] * 255.f),
						static_cast<int>(settings::visuals::name_color[2] * 255.f),
						static_cast<int>(settings::visuals::name_color[3] * 255.f)
					);
					helper::draw_text_outlined(draw, nullptr, 13.f, text_pos, name_col, name_to_display.c_str());
				}

				if (settings::visuals::distance)
				{
					char distance_str[32];
					snprintf(distance_str, sizeof(distance_str), "%.0f studs", custom_entity.distance);
					ImVec2 text_pos = { left, bottom + 5.f };
					ImU32 dist_col = IM_COL32(
						static_cast<int>(settings::visuals::distance_color[0] * 255.f),
						static_cast<int>(settings::visuals::distance_color[1] * 255.f),
						static_cast<int>(settings::visuals::distance_color[2] * 255.f),
						static_cast<int>(settings::visuals::distance_color[3] * 255.f)
					);
					helper::draw_text_outlined(draw, nullptr, 13.f, text_pos, dist_col, distance_str);
				}
			}
		}
	}
}