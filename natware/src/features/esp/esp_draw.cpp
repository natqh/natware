#include "esp_draw.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>

#include <menu/ui/ui.h>

namespace
{
	int alpha_of(ImU32 c) { return static_cast<int>((c >> IM_COL32_A_SHIFT) & 0xFF); }

	ImU32 with_alpha(ImU32 c, float a)
	{
		const int base = alpha_of(c);
		return (c & ~IM_COL32_A_MASK) | (static_cast<ImU32>(std::clamp(base * a, 0.0f, 255.0f)) << IM_COL32_A_SHIFT);
	}

	ImU32 black(float a) { return IM_COL32(0, 0, 0, static_cast<int>(std::clamp(a, 0.0f, 1.0f) * 255.0f)); }

	// Text with a soft one-pixel drop shadow: readable on any background without the chunky
	// eight-way outline.
	void shadow_text(ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImU32 col, const char* text)
	{
		pos.x = std::round(pos.x);
		pos.y = std::round(pos.y);
		const float a = alpha_of(col) / 255.0f;
		dl->AddText(font, size, ImVec2(pos.x + 1.0f, pos.y + 1.0f), black(0.70f * a), text);
		dl->AddText(font, size, pos, col, text);
	}

	ImVec2 text_size(ImFont* font, float size, const char* text)
	{
		return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
	}

}

ImU32 esp_draw::health_color(float health, float alpha)
{
	health = std::clamp(health, 0.0f, 1.0f);
	const ImVec4 red(1.0f, 0.30f, 0.32f, 1.0f), amber(1.0f, 0.76f, 0.26f, 1.0f), green(0.36f, 0.92f, 0.52f, 1.0f);
	const ImVec4 lo = health < 0.5f ? red : amber, hi = health < 0.5f ? amber : green;
	const float t = health < 0.5f ? health / 0.5f : (health - 0.5f) / 0.5f;
	return ImGui::ColorConvertFloat4ToU32(ImVec4(lo.x + (hi.x - lo.x) * t, lo.y + (hi.y - lo.y) * t, lo.z + (hi.z - lo.z) * t, alpha));
}

void esp_draw::player(ImDrawList* dl, const player_t& p)
{
	// Restraint is the design: one crisp box that owns the eye, text that sits directly on the
	// game with a shadow (no pills, no containers), colour only where it means something (health,
	// a target, a team), and every piece aligned to the box's own edges and centre.
	const ImDrawListFlags old_flags = dl->Flags;
	dl->Flags |= ImDrawListFlags_AntiAliasedLines | ImDrawListFlags_AntiAliasedFill;

	const ImVec2 mn(std::round(p.min.x), std::round(p.min.y));
	const ImVec2 mx(std::round(p.max.x), std::round(p.max.y));
	const float w = mx.x - mn.x, h = mx.y - mn.y;
	const float cx = std::round((mn.x + mx.x) * 0.5f);
	ImFont* font = ui::EspFont();

	// ---- fill: flat, as set ----
	if (alpha_of(p.fill) > 0)
		dl->AddRectFilled(ImVec2(mn.x + 1.0f, mn.y + 1.0f), ImVec2(mx.x - 1.0f, mx.y - 1.0f), p.fill);

	// ---- box ----
	if (alpha_of(p.box) > 0)
	{
		const float a = alpha_of(p.box) / 255.0f;
		if (p.box_style == 1)
		{
			// Corner brackets: a dark line under a 2px coloured one.
			const float len = std::clamp((std::min)(w, h) * 0.28f, 8.0f, 26.0f);
			const ImVec2 c[4] = { mn, ImVec2(mx.x, mn.y), mx, ImVec2(mn.x, mx.y) };
			const ImVec2 dir[4][2] = { { { 1, 0 }, { 0, 1 } }, { { -1, 0 }, { 0, 1 } }, { { -1, 0 }, { 0, -1 } }, { { 1, 0 }, { 0, -1 } } };
			for (int pass = 0; pass < 2; ++pass)
				for (int i = 0; i < 4; ++i)
					for (int k = 0; k < 2; ++k)
						dl->AddLine(c[i], ImVec2(c[i].x + dir[i][k].x * len, c[i].y + dir[i][k].y * len),
							pass == 0 ? black(0.6f * a) : p.box, pass == 0 ? 4.0f : 2.0f);
		}
		else
		{
			// A 1px coloured line between two 1px dark lines: crisp on any background. The glow
			// style adds a soft halo in the box's colour.
			if (p.box_style == 2)
				for (int i = 4; i >= 1; --i)
					dl->AddRect(ImVec2(mn.x - i - 1.0f, mn.y - i - 1.0f), ImVec2(mx.x + i + 1.0f, mx.y + i + 1.0f), with_alpha(p.box, 0.10f), 0.0f, 0, 2.0f);
			dl->AddRect(ImVec2(mn.x - 1.0f, mn.y - 1.0f), ImVec2(mx.x + 1.0f, mx.y + 1.0f), black(0.75f * a), 0.0f, 0, 1.0f);
			dl->AddRect(ImVec2(mn.x + 1.0f, mn.y + 1.0f), ImVec2(mx.x - 1.0f, mx.y - 1.0f), black(0.55f * a), 0.0f, 0, 1.0f);
			dl->AddRect(mn, mx, p.box, 0.0f, 0, 1.0f);
		}
	}

	// ---- health: a 2px bar down the left, its number beside the fill's top ----
	float below = mx.y + 4.0f;
	if (p.health >= 0.0f)
	{
		const float health = std::clamp(p.health, 0.0f, 1.0f);
		const ImU32 col = p.health_col ? p.health_col : health_color(health, p.bar_alpha);
		const ImVec2 a(mn.x - 6.0f, mn.y), b(mn.x - 4.0f, mx.y);
		dl->AddRectFilled(ImVec2(a.x - 1.0f, a.y - 1.0f), ImVec2(b.x + 1.0f, b.y + 1.0f), black(0.75f * p.bar_alpha));
		const float top = std::round(b.y - (b.y - a.y) * health);
		if (health > 0.0f)
			dl->AddRectFilled(ImVec2(a.x, top), b, col);
		if (p.health_text && health < 0.995f)
		{
			char text[8];
			snprintf(text, sizeof(text), "%d", static_cast<int>(std::ceil(health * 100.0f)));
			const ImVec2 ts = text_size(font, 11.0f, text);
			shadow_text(dl, font, 11.0f, ImVec2(a.x - 4.0f - ts.x, std::clamp(top - ts.y * 0.5f, a.y, b.y - ts.y)), p.health_text_col, text);
		}
	}
	if (p.armor >= 0.0f)
	{
		const float armor = std::clamp(p.armor, 0.0f, 1.0f);
		const ImVec2 a(mn.x, mx.y + 4.0f), b(mx.x, mx.y + 6.0f);
		dl->AddRectFilled(ImVec2(a.x - 1.0f, a.y - 1.0f), ImVec2(b.x + 1.0f, b.y + 1.0f), black(0.75f * p.bar_alpha));
		if (armor > 0.0f)
			dl->AddRectFilled(a, ImVec2(a.x + (b.x - a.x) * armor, b.y), p.armor_col);
		below = mx.y + 9.0f;
	}

	// ---- name above, centred; the accent (team, target, whitelist) colours the name itself ----
	if (p.name && *p.name && alpha_of(p.name_col) > 0)
	{
		const float size = 13.0f;
		const ImU32 col = alpha_of(p.accent) > 0 ? (p.accent & ~IM_COL32_A_MASK) | (p.name_col & IM_COL32_A_MASK) : p.name_col;
		const ImVec2 ts = text_size(font, size, p.name);
		const float pic = p.avatar ? 16.0f + 5.0f : 0.0f;
		float x = std::round(cx - (ts.x + pic) * 0.5f);
		const float y = std::round(mn.y - 5.0f - ts.y);
		if (p.avatar)
		{
			const ImVec2 c(x + 8.0f, y + ts.y * 0.5f);
			dl->AddCircleFilled(c, 9.0f, black(0.6f * alpha_of(p.name_col) / 255.0f), 20);
			dl->AddImageRounded(p.avatar, ImVec2(c.x - 8.0f, c.y - 8.0f), ImVec2(c.x + 8.0f, c.y + 8.0f), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, alpha_of(p.name_col)), 8.0f);
			x += pic;
		}
		shadow_text(dl, font, size, ImVec2(x, y), col, p.name);
	}

	// ---- under the box: distance and tool, then a tag; quiet text, centred ----
	if (p.info && *p.info && alpha_of(p.info_col) > 0)
	{
		const ImVec2 ts = text_size(font, 11.5f, p.info);
		shadow_text(dl, font, 11.5f, ImVec2(cx - ts.x * 0.5f, below), p.info_col, p.info);
		below += ts.y + 1.0f;
	}
	if (p.tag && *p.tag && alpha_of(p.tag_col) > 0)
	{
		const ImVec2 ts = text_size(font, 11.5f, p.tag);
		shadow_text(dl, font, 11.5f, ImVec2(cx - ts.x * 0.5f, below), p.tag_col, p.tag);
	}

	// ---- state, right of the box's top edge ----
	if (p.flag && *p.flag && alpha_of(p.flag_col) > 0)
		shadow_text(dl, font, 11.0f, ImVec2(mx.x + 5.0f, mn.y), p.flag_col, p.flag);

	dl->Flags = old_flags;
}


void esp_draw::skeleton(ImDrawList* dl, const std::vector<std::pair<ImVec2, ImVec2>>& bones, ImU32 color, float thickness)
{
	if (bones.empty() || alpha_of(color) == 0)
		return;
	const ImDrawListFlags old_flags = dl->Flags;
	dl->Flags |= ImDrawListFlags_AntiAliasedLines | ImDrawListFlags_AntiAliasedFill;
	const float a = alpha_of(color) / 255.0f;

	for (const auto& [p, q] : bones)
		dl->AddLine(p, q, black(0.45f * a), thickness + 2.5f);
	for (const auto& [p, q] : bones)
		dl->AddLine(p, q, color, thickness);

	// Round joints where bones meet (each point once).
	std::vector<ImVec2> joints;
	joints.reserve(bones.size() * 2);
	auto add_joint = [&](ImVec2 v)
	{
		for (const ImVec2& j : joints)
			if (std::fabs(j.x - v.x) < 1.0f && std::fabs(j.y - v.y) < 1.0f)
				return;
		joints.push_back(v);
	};
	for (const auto& [p, q] : bones)
	{
		add_joint(p);
		add_joint(q);
	}
	for (const ImVec2& j : joints)
	{
		dl->AddCircleFilled(j, thickness * 1.15f + 1.0f, black(0.45f * a), 12);
		dl->AddCircleFilled(j, thickness * 1.15f, color, 12);
	}
	dl->Flags = old_flags;
}
