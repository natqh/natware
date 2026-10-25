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

	// A bar with round ends: dark track, coloured fill, a faint glow around the fill.
	void capsule(ImDrawList* dl, ImVec2 a, ImVec2 b, float amount, bool vertical, ImU32 col, float alpha)
	{
		const float r = vertical ? (b.x - a.x) * 0.5f : (b.y - a.y) * 0.5f;
		dl->AddRectFilled(ImVec2(a.x - 1.0f, a.y - 1.0f), ImVec2(b.x + 1.0f, b.y + 1.0f), black(0.55f * alpha), r + 1.0f);
		amount = std::clamp(amount, 0.0f, 1.0f);
		if (amount <= 0.0f)
			return;
		ImVec2 fa = a, fb = b;
		if (vertical)
			fa.y = b.y - (b.y - a.y) * amount;
		else
			fb.x = a.x + (b.x - a.x) * amount;
		dl->AddRectFilled(ImVec2(fa.x - 1.0f, fa.y - 1.0f), ImVec2(fb.x + 1.0f, fb.y + 1.0f), with_alpha(col, 0.25f), r + 1.0f);
		dl->AddRectFilled(fa, fb, col, r);
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
	const ImDrawListFlags old_flags = dl->Flags;
	dl->Flags |= ImDrawListFlags_AntiAliasedLines | ImDrawListFlags_AntiAliasedFill;

	const ImVec2 mn(std::round(p.min.x), std::round(p.min.y));
	const ImVec2 mx(std::round(p.max.x), std::round(p.max.y));
	const float w = mx.x - mn.x, h = mx.y - mn.y;
	const float radius = std::clamp((std::min)(w, h) * 0.08f, 2.0f, 6.0f);
	ImFont* semibold = ui::EspFont();
	ImFont* regular = ui::EspFont();

	// ---- box ----
	if (alpha_of(p.fill) > 0)
		dl->AddRectFilled(mn, mx, p.fill, radius);

	if (alpha_of(p.box) > 0)
	{
		const float a = alpha_of(p.box) / 255.0f;
		if (p.box_style == 1)
		{
			// Corner brackets with round caps.
			const float len = std::clamp((std::min)(w, h) * 0.25f, 6.0f, 22.0f);
			const ImVec2 c[4] = { mn, ImVec2(mx.x, mn.y), mx, ImVec2(mn.x, mx.y) };
			const ImVec2 dir[4][2] = { { { 1, 0 }, { 0, 1 } }, { { -1, 0 }, { 0, 1 } }, { { -1, 0 }, { 0, -1 } }, { { 1, 0 }, { 0, -1 } } };
			for (int pass = 0; pass < 2; ++pass)
				for (int i = 0; i < 4; ++i)
					for (int k = 0; k < 2; ++k)
						dl->AddLine(c[i], ImVec2(c[i].x + dir[i][k].x * len, c[i].y + dir[i][k].y * len),
							pass == 0 ? black(0.55f * a) : p.box, pass == 0 ? 3.5f : 1.6f);
		}
		else
		{
			if (p.box_style == 2)
				for (int i = 3; i >= 1; --i)
					dl->AddRect(ImVec2(mn.x - i, mn.y - i), ImVec2(mx.x + i, mx.y + i), with_alpha(p.box, 0.12f * (4 - i)), radius + i, 0, 2.0f);
			dl->AddRect(mn, mx, black(0.55f * a), radius, 0, 3.5f);
			dl->AddRect(mn, mx, p.box, radius, 0, 1.5f);
		}
	}

	// ---- health (left) and armor (below) ----
	float below = mx.y + 5.0f;
	if (p.health >= 0.0f)
	{
		const ImU32 col = p.health_col ? p.health_col : health_color(p.health, p.bar_alpha);
		const ImVec2 a(mn.x - 7.0f, mn.y), b(mn.x - 4.0f, mx.y);
		capsule(dl, a, b, p.health, true, col, p.bar_alpha);
		if (p.health_text && p.health < 0.995f)
		{
			char text[8];
			snprintf(text, sizeof(text), "%d", static_cast<int>(std::ceil(p.health * 100.0f)));
			const ImVec2 ts = text_size(regular, 11.0f, text);
			const float y = std::clamp(b.y - (b.y - a.y) * p.health - ts.y * 0.5f, a.y - ts.y * 0.5f, b.y - ts.y * 0.5f);
			shadow_text(dl, regular, 11.0f, ImVec2(a.x - 3.0f - ts.x, y), p.health_text_col, text);
		}
	}
	if (p.armor >= 0.0f)
	{
		capsule(dl, ImVec2(mn.x, mx.y + 4.0f), ImVec2(mx.x, mx.y + 6.5f), p.armor, false, p.armor_col, p.bar_alpha);
		below = mx.y + 10.0f;
	}

	// ---- name pill above ----
	if (p.name && *p.name && alpha_of(p.name_col) > 0)
	{
		const float a = alpha_of(p.name_col) / 255.0f;
		const float size = 12.5f;
		const ImVec2 ts = text_size(semibold, size, p.name);
		const bool dot = alpha_of(p.accent) > 0;
		const float pad = 7.0f, dot_w = dot ? 9.0f : 0.0f, ph = 18.0f;
		const float pw = ts.x + pad * 2.0f + dot_w;
		const float cx = (mn.x + mx.x) * 0.5f;
		ImVec2 pa(std::round(cx - pw * 0.5f), std::round(mn.y - 6.0f - ph));
		ImVec2 pb(pa.x + pw, pa.y + ph);
		if (p.avatar)
		{
			const float s = ph;
			pa.x += (s + 4.0f) * 0.5f;
			pb.x += (s + 4.0f) * 0.5f;
			dl->AddImageRounded(p.avatar, ImVec2(pa.x - 4.0f - s, pa.y), ImVec2(pa.x - 4.0f, pb.y), ImVec2(0, 0), ImVec2(1, 1),
				IM_COL32(255, 255, 255, static_cast<int>(255 * a)), s * 0.5f);
		}
		dl->AddRectFilled(pa, pb, IM_COL32(10, 10, 14, static_cast<int>(165 * a)), ph * 0.5f);
		dl->AddRect(pa, pb, IM_COL32(255, 255, 255, static_cast<int>(22 * a)), ph * 0.5f, 0, 1.0f);
		float tx = pa.x + pad;
		if (dot)
		{
			dl->AddCircleFilled(ImVec2(tx + 2.5f, (pa.y + pb.y) * 0.5f), 2.75f, p.accent, 12);
			tx += dot_w;
		}
		dl->AddText(semibold, size, ImVec2(std::round(tx), std::round((pa.y + pb.y) * 0.5f - ts.y * 0.5f)), p.name_col, p.name);
	}

	// ---- info and tag below ----
	if (p.info && *p.info && alpha_of(p.info_col) > 0)
	{
		const ImVec2 ts = text_size(regular, 12.0f, p.info);
		shadow_text(dl, regular, 12.0f, ImVec2((mn.x + mx.x) * 0.5f - ts.x * 0.5f, below), p.info_col, p.info);
		below += ts.y + 1.0f;
	}
	if (p.tag && *p.tag && alpha_of(p.tag_col) > 0)
	{
		const ImVec2 ts = text_size(semibold, 11.0f, p.tag);
		shadow_text(dl, semibold, 11.0f, ImVec2((mn.x + mx.x) * 0.5f - ts.x * 0.5f, below), p.tag_col, p.tag);
	}

	// ---- state flag, right ----
	if (p.flag && *p.flag && alpha_of(p.flag_col) > 0)
		shadow_text(dl, regular, 11.0f, ImVec2(mx.x + 6.0f, mn.y), p.flag_col, p.flag);

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
