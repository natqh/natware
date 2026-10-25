#pragma once
#include <utility>
#include <vector>
#include <imgui/imgui.h>

// natware's own ESP look: smooth type with soft shadows instead of pixel fonts and hard outlines,
// rounded boxes, a name pill with an accent dot, capsule health and armor bars, and a skeleton
// with round joints. Everything is drawn from screen-space values, so the in-game ESP and the
// menu's preview share it.
namespace esp_draw
{
	struct player_t
	{
		ImVec2 min, max;                  // the box around the body
		ImU32 box = 0;                    // box line (alpha 0 = no box)
		int box_style = 0;                // 0 rounded, 1 corners, 2 glow
		ImU32 fill = 0;                   // box fill (alpha 0 = none)

		const char* name = nullptr;
		ImU32 name_col = 0;
		ImU32 accent = 0;                 // the dot in the name pill (team, role, whitelist)
		ImTextureID avatar = 0;

		float health = -1.0f;             // 0..1; below 0 = no bar
		ImU32 health_col = 0;             // 0 = coloured by health
		float bar_alpha = 1.0f;
		bool health_text = false;
		ImU32 health_text_col = 0;

		float armor = -1.0f;              // 0..1; below 0 = no bar
		ImU32 armor_col = 0;

		const char* info = nullptr;       // "42 m  ·  Rifle"
		ImU32 info_col = 0;
		const char* flag = nullptr;       // state, right of the box
		ImU32 flag_col = 0;
		const char* tag = nullptr;        // TARGET, MURDERER...
		ImU32 tag_col = 0;
	};

	void player(ImDrawList* dl, const player_t& p);

	// Bones as screen-space segments, with a soft shadow and round joints.
	void skeleton(ImDrawList* dl, const std::vector<std::pair<ImVec2, ImVec2>>& bones, ImU32 color, float thickness);

	// Health 0..1 to a colour: red, amber, green.
	ImU32 health_color(float health, float alpha);
}
