//================================================================//
//
//	Author:     Averta047 (widgets) + KiwiHax (compat widgets)
//	Purpose:	Unified ImAdd widget set for the external menu
//
//================================================================//

#pragma once

#include <vector> // TODO : concider using ImVector

#include "../imgui.h"
#include "../imgui_internal.h"

struct ImGuiWindow;
struct ImRect;

#ifndef IMADD_ANIMATIONS_SPEED
#define IMADD_ANIMATIONS_SPEED 1.0f
#endif

#ifndef IMADD_API
#define IMADD_API
#endif

typedef std::vector<std::pair<const char*, bool>> ImMultiComboItems;
typedef int ImAddTextFlags;
typedef int ImAddTabFlags;

enum ImAddTextFlags_
{
    ImAddTextFlags_None				= 0,
    ImAddTextFlags_ShadowRight		= 1 << 0,
    ImAddTextFlags_ShadowLeft		= 1 << 1,
    ImAddTextFlags_ShadowBottom		= 1 << 2,
    ImAddTextFlags_ShadowTop		= 1 << 3,
    ImAddTextFlags_ShadowBottomRight= 1 << 4,
    ImAddTextFlags_ShadowBottomLeft	= 1 << 5,
    ImAddTextFlags_ShadowTopRight	= 1 << 6,
    ImAddTextFlags_ShadowTopLeft	= 1 << 7,

    // Shortcuts
    ImAddTextFlags_ShadowSides		= ImAddTextFlags_ShadowRight | ImAddTextFlags_ShadowLeft | ImAddTextFlags_ShadowBottom | ImAddTextFlags_ShadowTop,
    ImAddTextFlags_ShadowCorners	= ImAddTextFlags_ShadowBottomRight | ImAddTextFlags_ShadowBottomLeft | ImAddTextFlags_ShadowTopRight | ImAddTextFlags_ShadowTopLeft,
    ImAddTextFlags_Outline			= ImAddTextFlags_ShadowSides | ImAddTextFlags_ShadowCorners
};

enum ImAddTabFlags_
{
    ImAddTabFlags_None = 0,
    ImAddTabFlags_First     = 1 << 0,
    ImAddTabFlags_Last      = 1 << 1,
};

namespace ImAdd
{
    // Main
    IMADD_API void      SetAnimationSpeed(float speed);
    IMADD_API float     GetAnimationSpeed();

	// Helpers
	IMADD_API ImColor	Hex2Color(ImU32 hex, float alpha = 1.0f);
	IMADD_API ImVec4	HexToColorVec4(unsigned int hex_color, float alpha = 1.0f);
	IMADD_API float		GetColorPickerWidth();

    // Separtor
    IMADD_API void		VSeparator(float margin = 0.0f, float thickness = 1.0f);
    IMADD_API void		SeparatorText(const char* label, float thickness = 1.0f);

    // Generic-Controls
    IMADD_API bool      Button(const char* label, const ImVec2& size_arg = ImVec2(0, 0), ImDrawFlags draw_flags = 0, ImGuiButtonFlags button_flags = 0);
    IMADD_API bool      CheckBox(const char* label, bool* v);
    IMADD_API bool		SelectableLabel(const char* label, bool selected, bool centered = false, const ImVec2& size_arg = ImVec2(0, 0));
    IMADD_API bool		ButtonAccent(const char* label, const ImVec2& size_arg = ImVec2(0, 0), ImDrawFlags draw_flags = 0, ImGuiButtonFlags button_flags = 0);

    // Keybind
    IMADD_API float     CalcKeyBindWidth(ImGuiKey key);
    IMADD_API bool	    KeyBind(const char* label, ImGuiKey* k, const ImVec2& size_arg = ImVec2(0, 0), int* activation_mode = nullptr, bool allow_mouse = true, bool allow_gamepad = true);

    // Sliders
    IMADD_API bool	    SliderScalar(const char* label, ImGuiDataType data_type, void* p_data, const void* p_min, const void* p_max, const char* format = NULL);
    IMADD_API bool	    SliderFloat(const char* label, float* v, float v_min, float v_max, const char* format = "%.1f");
    IMADD_API bool	    SliderInt(const char* label, int* v, int v_min, int v_max, const char* format = "%d");


    // Combo
    IMADD_API bool	    ComboItem(const char* label, bool selected = false, const ImVec2& size_arg = ImVec2(0, 0));
    IMADD_API bool	    BeginCombo(const char* label, const char* preview_value);
    IMADD_API void	    EndCombo(); // only call EndCombo() if BeginCombo() returns true!
    IMADD_API bool	    Combo(const char* label, int* current_item, const char* const items[], int items_count, int popup_max_height_in_items = -1);
    IMADD_API bool	    Combo(const char* label, int* current_item, const char* items_separated_by_zeros, int popup_max_height_in_items = -1);
    IMADD_API bool	    Combo(const char* label, int* current_item, const char* (*getter)(void* user_data, int idx), void* user_data, int items_count, int popup_max_height_in_items = -1);
    IMADD_API bool	    Combo(const char* label, int* current_item, const std::vector<const char*>& items, int height_in_items = -1);
    IMADD_API bool	    ComboMultiSelect(const char* label, ImMultiComboItems* items, int height_in_items = -1);

    // Color stuff
    IMADD_API bool	    ColorButton(const char* desc_id, const ImVec4& col, const ImVec2& size_arg = ImVec2(0, 0), bool has_alpha = true);
    IMADD_API bool      ColorPicker(const char* label, float col[4], bool has_alpha = true, const float* ref_col = NULL);
    IMADD_API bool	    ColorEditor(const char* label, float col[4], bool enable_alpha = true);
    IMADD_API bool	    ColorEdit4(const char* label, float col[4]);

    // Combo exposed internals
    IMADD_API bool	    BeginComboPopup(ImGuiID popup_id, const ImRect& bb);

    // Navigation & Specials (custom widgets)
    IMADD_API bool      TabItem(const char* label, int* v, int index, const ImVec2& size_arg = ImVec2(0, 0), ImAddTabFlags flags = 0);
    IMADD_API bool      Tab(const char* label, bool selected, const ImVec2& size_arg = ImVec2(0, 0));

    IMADD_API void	    ScrollBarY(const char* str_id, float* scroll_y, const ImVec2& size_arg = ImVec2(0, 0));
    IMADD_API void	    ScrollBar(const char* str_id, ImGuiWindow* window, const ImVec2& size_arg = ImVec2(0, 0));

    // Child windows / sections
    IMADD_API bool		BeginChild(const char* str_id, std::vector<const char*> tabs, int* selected_tab_index_callback, const ImVec2& size_arg = ImVec2(0, 0));
    IMADD_API bool		BeginChild(const char* str_id, std::vector<const char*> tabs, const ImVec2& size_arg = ImVec2(0, 0));
    IMADD_API bool		BeginChild(const char* str_id, const ImVec2& size_arg = ImVec2(0, 0));
    IMADD_API void      EndChild();

    IMADD_API bool      BeginSection(const char* label, const ImVec2& size_arg = ImVec2(0, 0), bool disable_scrollbar = false);
    IMADD_API void      EndSection();

	// Rendering helpers
	IMADD_API void		RenderArrow(ImDrawList* draw_list, ImVec2 pos, ImU32 col, ImGuiDir dir, float scale = 1.0f);
	IMADD_API void		RenderCheckMark(ImDrawList* draw_list, ImVec2 pos, ImU32 col, float sz);
    IMADD_API void      RenderText(ImDrawList* drawlist, ImVec2 pos, const char* text, const char* text_end = NULL, bool hide_text_after_hash = true, ImAddTextFlags flags = ImAddTextFlags_None);
    IMADD_API void      RenderText(ImVec2 pos, const char* text, const char* text_end = NULL, bool hide_text_after_hash = true, ImAddTextFlags flags = ImAddTextFlags_None);
	IMADD_API void		RenderLine(const ImVec2& p1, const ImVec2& p2, ImU32 col, float thickness = 1.0f);
	IMADD_API void		RenderFrame(ImVec2 p_min, ImVec2 p_max, ImU32 fill_col, bool borders = true, bool border_shadow = true, bool inverted = false, bool shadows = true, float rounding = 0.0f, ImDrawFlags draw_flags = 0, ImGuiDir shadow_dir = ImGuiDir_Up);
	IMADD_API void		RenderFrameBorder(ImVec2 p_min, ImVec2 p_max, bool borders = true, bool border_shadow = true, bool inverted = false, float rounding = 0.0f, ImDrawFlags draw_flags = 0);
	IMADD_API void		RenderRectFilledMultiColorRounded(ImDrawList* drawlist, const ImVec2& p_min, const ImVec2& p_max, ImU32 col_upr_left, ImU32 col_upr_right, ImU32 col_bot_right, ImU32 col_bot_left, float rounding = 0.0f, ImDrawFlags draw_flags = 0);
}