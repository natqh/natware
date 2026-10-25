//============ Copyright KiwiHax, All rights reserved ============//
//
//  Purpose: 
//
//================================================================//

#include "../imgui_internal.h"
#include "imgui_addons.h"
#include "../../src/menu/keybind/keybind.h"

#include <map>
#include <unordered_map>
#include <string>
#include <windows.h>

using namespace ImGui;

ImVec4 ImAdd::HexToColorVec4(unsigned int hex_color, float alpha)
{
    ImVec4 color;

    color.x = ((hex_color >> 16) & 0xFF) / 255.0f;
    color.y = ((hex_color >> 8) & 0xFF) / 255.0f;
    color.z = (hex_color & 0xFF) / 255.0f;
    color.w = alpha;

    return color;
}

float ImAdd::GetColorPickerWidth()
{
    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;

    return g.FontSize * 2.0f + style.CellPadding.x * 4.0f;
}

void ImAdd::RenderArrow(ImDrawList* draw_list, ImVec2 pos, ImU32 col, ImGuiDir dir, float scale)
{
    ImGui::RenderArrow(draw_list, pos, col, dir, scale);
}

void ImAdd::RenderFrame(ImVec2 p_min, ImVec2 p_max, ImU32 fill_col, bool borders, bool border_shadow, bool inverted, bool shadows, float rounding, ImDrawFlags draw_flags, ImGuiDir shadow_dir)
{
    (void)border_shadow;
    (void)inverted;
    (void)shadows;
    (void)draw_flags;
    (void)shadow_dir;
    ImGui::RenderFrame(p_min, p_max, fill_col, borders, rounding);
}

void ImAdd::RenderFrameBorder(ImVec2 p_min, ImVec2 p_max, bool borders, bool border_shadow, bool inverted, float rounding, ImDrawFlags draw_flags)
{
    (void)border_shadow;
    (void)inverted;
    (void)draw_flags;
    if (borders)
        ImGui::RenderFrameBorder(p_min, p_max, rounding);
}

void ImAdd::SeparatorText(const char* label, float thickness)
{
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return;
    
    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    const ImVec2 label_size = CalcTextSize(label, NULL, true);

    ImVec2 pos = window->DC.CursorPos;
    ImVec2 size = CalcItemSize(ImVec2(-0.1f, g.FontSize), label_size.x, g.FontSize);

    const ImRect total_bb(pos, pos + size);
    ItemSize(total_bb);
    if (!ItemAdd(total_bb, id)) {
        return;
    }

    window->DrawList->AddText(pos, GetColorU32(ImGuiCol_TextDisabled), label);

    if (thickness > 0)
        window->DrawList->AddLine(pos + ImVec2(label_size.x + style.ItemInnerSpacing.x, size.y / 2), pos + ImVec2(size.x, size.y / 2), GetColorU32(ImGuiCol_Border), thickness);
}

void ImAdd::VSeparator(float margin, float thickness)
{
    if (thickness <= 0)
        return;

    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;

    ImVec2 pos = window->DC.CursorPos;
    ImVec2 size = CalcItemSize(ImVec2(thickness, -0.1f), thickness, thickness);

    const ImRect bb(pos, pos + size);
    const ImRect bb_rect(pos + ImVec2(0, margin), pos + size - ImVec2(0, margin));

    ItemSize(ImVec2(thickness, 0.0f));
    if (!ItemAdd(bb, 0))
        return;

    window->DrawList->AddRectFilled(bb_rect.Min, bb_rect.Max, GetColorU32(ImGuiCol_Border));
}

bool ImAdd::SelectableLabel(const char* label, bool selected, bool centered, const ImVec2& size_arg)
{
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    const ImVec2 label_size = CalcTextSize(label, NULL, true);

    ImVec2 pos = window->DC.CursorPos;
    ImVec2 size = CalcItemSize(size_arg, label_size.x, label_size.y);

    const ImRect total_bb(pos, pos + size);
    ItemSize(size);
    if (!ItemAdd(total_bb, id))
        return false;

    // Behaviors
    bool hovered, held;
    bool pressed = ButtonBehavior(total_bb, id, &hovered, &held);

    // Colors
    ImVec4 colLabel = GetStyleColorVec4(hovered || selected ? ImGuiCol_Text : ImGuiCol_TextDisabled);

    ImVec4 colLineMain = GetStyleColorVec4(ImGuiCol_SliderGrab);
    ImVec4 colLineNull = colLineMain;
    colLineNull.w = 0.0f;

    ImVec4 colLine = selected ? colLineMain : colLineNull;

    // Animations
    struct stColors_State {
        ImColor Label;
        ImColor Line;
    };

    static std::map<ImGuiID, stColors_State> anim;
    auto it_anim = anim.find(id);

    if (it_anim == anim.end())
    {
        anim.insert({ id, stColors_State() });
        it_anim = anim.find(id);

        it_anim->second.Label = colLabel;
        it_anim->second.Line = colLine;
    }

    it_anim->second.Label.Value = ImLerp(it_anim->second.Label.Value, colLabel, 1.0f / IMADD_ANIMATIONS_SPEED * GetIO().DeltaTime);
    it_anim->second.Line.Value = ImLerp(it_anim->second.Line.Value, colLine, 1.0f / IMADD_ANIMATIONS_SPEED * GetIO().DeltaTime);

    RenderNavCursor(total_bb, id);

    window->DrawList->AddText(pos + ImTrunc(ImVec2(centered ? (size.x / 2 - label_size.x / 2) : 0.0f, size.y / 2 - label_size.y / 2)), it_anim->second.Label, label);

    return pressed;
}

bool ImAdd::CheckBox(const char* label, bool* v)
{
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    const ImVec2 label_size = CalcTextSize(label, NULL, true);

    const float square_sz = GetFontSize() + style.CellPadding.y * 2.0f;

    const ImVec2 pos = window->DC.CursorPos;
    const ImRect total_bb(pos, pos + ImVec2(square_sz + (label_size.x > 0.0f ? style.ItemInnerSpacing.x + label_size.x : 0.0f), label_size.y + style.CellPadding.y * 2.0f));

    ItemSize(total_bb, style.CellPadding.y);
    if (!ItemAdd(total_bb, id))
    {
        return false;
    }

    bool hovered, held, checked = false;
    bool pressed = ButtonBehavior(total_bb, id, &hovered, &held);

    if (v)
    {
        checked = *v;
        if (pressed) *v = !*v;
    }

    const ImRect check_bb(pos, pos + ImVec2(square_sz, square_sz));

    // Colors
    ImU32 colFrame = GetColorU32(checked ? ImGuiCol_Header : (held && hovered) ? ImGuiCol_FrameBgActive : hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);

    RenderNavHighlight(total_bb, id);
    RenderFrame(check_bb.Min, check_bb.Max, colFrame, false);
    window->DrawList->AddRectFilledMultiColor(check_bb.Min, check_bb.Max, 
        GetColorU32(checked ? ImGuiCol_HeaderActive : ImGuiCol_FrameBgShadow, 0.0f), GetColorU32(checked ? ImGuiCol_HeaderActive : ImGuiCol_FrameBgShadow, 0.0f),
        GetColorU32(checked ? ImGuiCol_HeaderActive : ImGuiCol_FrameBgShadow), GetColorU32(checked ? ImGuiCol_HeaderActive : ImGuiCol_FrameBgShadow));
    RenderFrameBorder(check_bb.Min, check_bb.Max);

    if (label_size.x > 0.0f)
    {
        const ImVec2 label_pos = ImVec2(check_bb.Max.x + style.ItemInnerSpacing.x, check_bb.Min.y + style.CellPadding.y);

        //PushStyleColor(ImGuiCol_Text, style.Colors[checked ? ImGuiCol_Text : ImGuiCol_TextDisabled]);
        RenderText(label_pos, label, NULL, true, true);
        //PopStyleColor();
    }

    return pressed;
}

bool ImAdd::Button(const char* label, const ImVec2& size_arg, ImDrawFlags draw_flags, ImGuiButtonFlags button_flags)
{
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    const ImVec2 label_size = CalcTextSize(label, NULL, true);

    ImVec2 pos = window->DC.CursorPos;
    ImVec2 size = CalcItemSize(size_arg, label_size.x + style.FramePadding.x * 2.0f, label_size.y + style.FramePadding.y * 2.0f);

    const ImRect total_bb(pos, pos + size);
    ItemSize(size);
    if (!ItemAdd(total_bb, id))
        return false;

    // Behaviors
    bool hovered, held;
    bool pressed = ButtonBehavior(total_bb, id, &hovered, &held);

    // Colors
    ImU32 colFrame = GetColorU32((hovered && held) ? ImGuiCol_ButtonActive : hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);

    RenderNavCursor(total_bb, id);
    RenderFrame(total_bb.Min, total_bb.Max, colFrame, true, held);
	window->DrawList->AddRectFilledMultiColor(total_bb.Min, total_bb.Max, GetColorU32(ImGuiCol_ButtonShadow, 0.0f), GetColorU32(ImGuiCol_ButtonShadow, 0.0f), GetColorU32(ImGuiCol_ButtonShadow), GetColorU32(ImGuiCol_ButtonShadow));
	RenderText(pos + ImTrunc((size - label_size) / 2), label, NULL, true, true);

    return pressed;
}

bool ImAdd::ButtonAccent(const char* label, const ImVec2& size_arg, ImDrawFlags draw_flags, ImGuiButtonFlags button_flags)
{
    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;

    PushStyleColor(ImGuiCol_Button, style.Colors[ImGuiCol_Header]);
    PushStyleColor(ImGuiCol_ButtonHovered, style.Colors[ImGuiCol_HeaderHovered]);
    PushStyleColor(ImGuiCol_ButtonActive, style.Colors[ImGuiCol_HeaderActive]);
    PushStyleColor(ImGuiCol_ButtonShadow, style.Colors[ImGuiCol_FrameBgShadow]);

    bool result = Button(label, size_arg, draw_flags);

	PopStyleColor(3);

    return result;
}

bool ImAdd::Combo(const char* label, int* selected_index, const std::vector<const char*>& items, int height_in_items)
{
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    const ImVec2 label_size = CalcTextSize(label, NULL, true);
    const float square_sz = g.FontSize + style.FramePadding.y * 2.0f;

    const float width = CalcItemWidth();
    const float height = GetFrameHeight();

    int items_count = static_cast<int>(items.size());

    ImVec2 pos = window->DC.CursorPos;
    ImVec2 size = ImVec2(width, height);

    const ImRect total_bb(pos, pos + size);
    ItemSize(size);
    if (!ItemAdd(total_bb, id))
        return false;

    // Behaviors
    bool hovered, held;
    bool pressed = ButtonBehavior(total_bb, id, &hovered, &held);

    std::string popup_str_id = std::string(std::string(label) + "::combo_popup");

    if (pressed)
    {
        OpenPopup(popup_str_id.c_str());
    }

    PushStyleVar(ImGuiStyleVar_WindowPadding, style.FramePadding);
    PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, style.FramePadding.y));
    if (BeginPopupEx(GetID(popup_str_id.c_str()), ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove))
    {
        SetWindowPos(pos + ImVec2(0, height + style.FramePadding.y), ImGuiCond_Always);
        SetWindowSize(ImVec2(width, ImGui::GetFontSize() * items_count + style.FramePadding.y * (items_count + 1)), ImGuiCond_Always);

        for (int i = 0; i < items_count; i++)
        {
            if (ImAdd::SelectableLabel(items[i], i == *selected_index, false, ImVec2(GetContentRegionAvail().x, GetFontSize())))
            {
                *selected_index = i;
                CloseCurrentPopup();
            }
        }

        EndPopup();
    }
    PopStyleVar(2);

    // Colors
    ImVec4 colFrame = GetStyleColorVec4((hovered && held) ? ImGuiCol_FrameBgActive : hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);

    // Animations
    struct stColors_State {
        ImColor Frame;
    };

    static std::map<ImGuiID, stColors_State> anim;
    auto it_anim = anim.find(id);

    if (it_anim == anim.end())
    {
        anim.insert({ id, stColors_State() });
        it_anim = anim.find(id);

        it_anim->second.Frame = colFrame;
    }

    it_anim->second.Frame.Value = ImLerp(it_anim->second.Frame.Value, colFrame, 1.0f / IMADD_ANIMATIONS_SPEED * GetIO().DeltaTime);

    RenderNavCursor(total_bb, id);

    window->DrawList->AddRectFilled(total_bb.Min, total_bb.Max, it_anim->second.Frame, style.FrameRounding);
    window->DrawList->AddRectFilledMultiColorRounded(total_bb.Min, total_bb.Max, GetColorU32(ImGuiCol_FrameBgShadow, 0.0f), GetColorU32(ImGuiCol_FrameBgShadow, 0.0f), GetColorU32(ImGuiCol_FrameBgShadow), GetColorU32(ImGuiCol_FrameBgShadow), style.FrameRounding);

    if (style.FrameBorderSize > 0)
    {
        window->DrawList->AddRect(total_bb.Min, total_bb.Max, GetColorU32(ImGuiCol_Border), style.FrameRounding, 0, style.FrameBorderSize);
    }

    std::string preview_item;
    if (*selected_index > items.size()) {
        preview_item = "*unknown item*";
    }
    else
    {
        preview_item = items[*selected_index];
    }

    ImVec2 text_pos = pos + style.FramePadding;
    ImU32 text_col = GetColorU32(ImGuiCol_Text);
    ImU32 outline_col = IM_COL32(0, 0, 0, 255);
    ImFont* font = g.Font;
    float font_size = g.FontSize;

    text_pos.x = roundf(text_pos.x);
    text_pos.y = roundf(text_pos.y);

    for (int x = -1; x <= 1; x++)
    {
        for (int y = -1; y <= 1; y++)
        {
            if (x == 0 && y == 0)
            {
                continue;
            }
            window->DrawList->AddText(font, font_size, ImVec2(text_pos.x + x, text_pos.y + y), outline_col, preview_item.c_str());
        }
    }
    window->DrawList->AddText(font, font_size, text_pos, text_col, preview_item.c_str());

    RenderArrow(window->DrawList, pos + ImVec2(width - GetFontSize() - style.FramePadding.x, style.FramePadding.y), GetColorU32(ImGuiCol_Text), ImGuiDir_Down);

    return pressed;
}

bool ImAdd::ColorEdit4(const char* label, float col[4])
{
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;

    const ImVec4 col_v4(col[0], col[1], col[2], col[3]);
	const ImGuiColorEditFlags flags = ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoTooltip;

    bool pressed = ImGui::ColorButton(label, col_v4, flags, ImVec2(g.FontSize * 2 + style.CellPadding.x * 4.0f, g.FontSize + style.CellPadding.y * 2.0f - style.FrameBorderSize));

    if (pressed)
    {
		OpenPopup(label);
    }

    if (BeginPopup(label))
    {
        ColorPicker4(label, col, flags);
        EndPopup();
    }

    return false;
}

bool ImAdd::KeyBind(const char* str_id, ImGuiKey* k, const ImVec2& size_arg, int* activation_mode, bool allow_mouse, bool allow_gamepad)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    ImGuiIO& io = g.IO;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(str_id);

    ImVec2 pos = window->DC.CursorPos;

    char buf_display[32] = "[unbinded]";

    bool is_selecing = false;

    if (*k != ImGuiKey_None && *k != 0 && g.ActiveId != id)
    {
        int vk = keybind::imgui_key_to_vk(*k);
        const char* name = keybind::get_key_name(vk);
        snprintf(buf_display, sizeof(buf_display), "[%s]", name);
    }
    else if (g.ActiveId == id)
    {
        is_selecing = true;
        strcpy_s(buf_display, sizeof buf_display, "[...]");
    }

    const float square_sz = GetFontSize() + style.CellPadding.y * 2.0f;
    ImVec2 text_size = CalcTextSize(buf_display, NULL, true);
    ImVec2 size = CalcItemSize(size_arg, text_size.x + style.FramePadding.x * 2.0f, square_sz);
    ImRect frame_bb(pos, pos + size);
    ImRect total_bb(pos, frame_bb.Max);

    ImGui::ItemSize(total_bb, style.CellPadding.y);
    if (!ImGui::ItemAdd(total_bb, id))
        return false;

    const bool hovered = ImGui::ItemHoverable(frame_bb, id, 0);

    // Colors
    ImVec4 colLabel = GetStyleColorVec4(is_selecing ? ImGuiCol_Text : ImGuiCol_TextDisabled);

    // Animations
    struct stColors_State {
        ImColor Label;
    };

    static std::map<ImGuiID, stColors_State> anim;
    auto it_anim = anim.find(id);

    if (it_anim == anim.end())
    {
        anim.insert({ id, stColors_State() });
        it_anim = anim.find(id);

        it_anim->second.Label = colLabel;
    }

    it_anim->second.Label.Value = ImLerp(it_anim->second.Label.Value, colLabel, 1.0f / IMADD_ANIMATIONS_SPEED * GetIO().DeltaTime);

    const bool user_clicked = hovered && IsMouseClicked(ImGuiMouseButton_Left);
    const bool right_clicked = hovered && IsMouseClicked(ImGuiMouseButton_Right);

    std::string popup_id = std::string(str_id) + "##keybind_popup";

    bool value_changed = false;

    if (right_clicked)
    {
        SetNextWindowPos(GetMousePosOnOpeningCurrentPopup());
        OpenPopup(popup_id.c_str());
    }

    int current_mode = activation_mode != nullptr ? *activation_mode : 1;
    
    float max_text_width = 0.0f;
    max_text_width = ImMax(max_text_width, CalcTextSize("Hold").x);
    max_text_width = ImMax(max_text_width, CalcTextSize("Toggle").x);
    max_text_width = ImMax(max_text_width, CalcTextSize("Always").x);
    max_text_width = ImMax(max_text_width, CalcTextSize("Clear").x);
    
    float popup_width = max_text_width + style.FramePadding.x * 2.0f;
    
    PushStyleVar(ImGuiStyleVar_WindowPadding, style.FramePadding);
    SetNextWindowSize(ImVec2(popup_width, 0), ImGuiCond_Always);
    if (BeginPopup(popup_id.c_str()))
    {
        if (ImAdd::SelectableLabel("Hold", current_mode == 1))
        {
            if (activation_mode != nullptr)
                *activation_mode = 1;
            CloseCurrentPopup();
        }
        if (ImAdd::SelectableLabel("Toggle", current_mode == 0))
        {
            if (activation_mode != nullptr)
                *activation_mode = 0;
            CloseCurrentPopup();
        }
        if (ImAdd::SelectableLabel("Always", current_mode == 2))
        {
            if (activation_mode != nullptr)
                *activation_mode = 2;
            CloseCurrentPopup();
        }
        Separator();
        if (ImAdd::SelectableLabel("Clear", false))
        {
            *k = ImGuiKey_None;
            value_changed = true;
            CloseCurrentPopup();
        }
        EndPopup();
    }
    PopStyleVar();

    static std::map<ImGuiID, int> activation_frame;
    static std::map<ImGuiID, bool> mouse_was_down[5];

    if (user_clicked)
    {
        ImGui::SetActiveID(id, window);
        ImGui::FocusWindow(window);
        activation_frame[id] = g.FrameCount;
        mouse_was_down[0][id] = IsMouseDown(ImGuiMouseButton_Left);
        mouse_was_down[1][id] = IsMouseDown(ImGuiMouseButton_Right);
        mouse_was_down[2][id] = IsMouseDown(ImGuiMouseButton_Middle);
    }
    else if (IsMouseClicked(ImGuiMouseButton_Left))
    {
        if (g.ActiveId == id)
            ImGui::ClearActiveID();
    }
    int key = *k;

    if (hovered && IsMouseClicked(ImGuiMouseButton_Left))
    {
        if (g.ActiveId != id)
        {
            // Start capturing
            memset(io.MouseDown, 0, sizeof(io.MouseDown));
            SetActiveID(id, window);
            FocusWindow(window);
            activation_frame[id] = g.FrameCount;
            mouse_was_down[0][id] = IsMouseDown(ImGuiMouseButton_Left);
            mouse_was_down[1][id] = IsMouseDown(ImGuiMouseButton_Right);
            mouse_was_down[2][id] = IsMouseDown(ImGuiMouseButton_Middle);
        }
    }

    if (IsMouseClicked(ImGuiMouseButton_Left) && g.ActiveId == id && !hovered)
    {
        // Clicked outside - cancel
        ClearActiveID();
        activation_frame.erase(id);
        mouse_was_down[0].erase(id);
        mouse_was_down[1].erase(id);
        mouse_was_down[2].erase(id);
        mouse_was_down[3].erase(id);
        mouse_was_down[4].erase(id);
    }

    // Handle key capture
    if (g.ActiveId == id)
    {
        static std::map<ImGuiID, bool> prev_mouse_down[5];
        
        bool skip_mouse = false;
        auto it_frame = activation_frame.find(id);
        if (it_frame != activation_frame.end())
        {
            if (g.FrameCount <= it_frame->second + 3)
            {
                skip_mouse = true;
            }
            else
            {
                activation_frame.erase(id);
            }
        }

        bool left_now = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        bool right_now = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
        bool middle_now = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
        bool x1_now = (GetAsyncKeyState(VK_XBUTTON1) & 0x8000) != 0;
        bool x2_now = (GetAsyncKeyState(VK_XBUTTON2) & 0x8000) != 0;
        
        if (skip_mouse)
        {
            prev_mouse_down[0][id] = left_now;
            prev_mouse_down[1][id] = right_now;
            prev_mouse_down[2][id] = middle_now;
            prev_mouse_down[3][id] = x1_now;
            prev_mouse_down[4][id] = x2_now;
        }
        else if (!value_changed)
        {
            bool left_prev = prev_mouse_down[0][id];
            bool right_prev = prev_mouse_down[1][id];
            bool middle_prev = prev_mouse_down[2][id];
            bool x1_prev = prev_mouse_down[3][id];
            bool x2_prev = prev_mouse_down[4][id];
            
            if (left_now && !left_prev)
            {
                *k = ImGuiKey_MouseLeft;
                value_changed = true;
                ClearActiveID();
                activation_frame.erase(id);
                mouse_was_down[0].erase(id);
                mouse_was_down[1].erase(id);
                mouse_was_down[2].erase(id);
                prev_mouse_down[0].erase(id);
                prev_mouse_down[1].erase(id);
                prev_mouse_down[2].erase(id);
                prev_mouse_down[3].erase(id);
                prev_mouse_down[4].erase(id);
            }
            else if (right_now && !right_prev)
            {
                *k = ImGuiKey_MouseRight;
                value_changed = true;
                ClearActiveID();
                activation_frame.erase(id);
                mouse_was_down[0].erase(id);
                mouse_was_down[1].erase(id);
                mouse_was_down[2].erase(id);
                prev_mouse_down[0].erase(id);
                prev_mouse_down[1].erase(id);
                prev_mouse_down[2].erase(id);
                prev_mouse_down[3].erase(id);
                prev_mouse_down[4].erase(id);
            }
            else if (middle_now && !middle_prev)
            {
                *k = ImGuiKey_MouseMiddle;
                value_changed = true;
                ClearActiveID();
                activation_frame.erase(id);
                mouse_was_down[0].erase(id);
                mouse_was_down[1].erase(id);
                mouse_was_down[2].erase(id);
                prev_mouse_down[0].erase(id);
                prev_mouse_down[1].erase(id);
                prev_mouse_down[2].erase(id);
                prev_mouse_down[3].erase(id);
                prev_mouse_down[4].erase(id);
            }
            else if (x1_now && !x1_prev)
            {
                *k = ImGuiKey_MouseX1;
                value_changed = true;
                ClearActiveID();
                activation_frame.erase(id);
                mouse_was_down[0].erase(id);
                mouse_was_down[1].erase(id);
                mouse_was_down[2].erase(id);
                prev_mouse_down[0].erase(id);
                prev_mouse_down[1].erase(id);
                prev_mouse_down[2].erase(id);
                prev_mouse_down[3].erase(id);
                prev_mouse_down[4].erase(id);
            }
            else if (x2_now && !x2_prev)
            {
                *k = ImGuiKey_MouseX2;
                value_changed = true;
                ClearActiveID();
                activation_frame.erase(id);
                mouse_was_down[0].erase(id);
                mouse_was_down[1].erase(id);
                mouse_was_down[2].erase(id);
                prev_mouse_down[0].erase(id);
                prev_mouse_down[1].erase(id);
                prev_mouse_down[2].erase(id);
                prev_mouse_down[3].erase(id);
                prev_mouse_down[4].erase(id);
            }
            else
            {
                prev_mouse_down[0][id] = left_now;
                prev_mouse_down[1][id] = right_now;
                prev_mouse_down[2][id] = middle_now;
                prev_mouse_down[3][id] = x1_now;
                prev_mouse_down[4][id] = x2_now;
            }
        }

        // Check keyboard keys if no mouse button was pressed
        if (!value_changed)
        {
            // Check all possible keys
            for (int i = ImGuiKey_NamedKey_BEGIN; i < ImGuiKey_NamedKey_END; i++) // only named keyboard/gamepad keys
            {
                ImGuiKey key_test = (ImGuiKey)i;

                // Skip mouse inputs (already handled above) and escape
                if ((key_test >= ImGuiKey_MouseLeft && key_test <= ImGuiKey_MouseWheelY) || key_test == ImGuiKey_Escape)
                    continue;

                if (IsKeyPressed(key_test)) // Pressed, not Down, avoids "instant bind"
                {
                    *k = key_test;
                    value_changed = true;
                    ClearActiveID();
                    activation_frame.erase(id);
                    mouse_was_down[0].erase(id);
                    mouse_was_down[1].erase(id);
                    mouse_was_down[2].erase(id);
                    break;
                }
            }
        }

        // Escape cancels
        if (IsKeyPressed(ImGuiKey_Escape))
        {
            ClearActiveID();
            activation_frame.erase(id);
            mouse_was_down[0].erase(id);
            mouse_was_down[1].erase(id);
            mouse_was_down[2].erase(id);
        }
    }

    // Render

    ImGui::RenderNavHighlight(total_bb, id);

    window->DrawList->AddRectFilled(pos, pos + size, GetColorU32(ImGuiCol_FrameBg), style.FrameRounding);

    if (style.FrameBorderSize > 0)
    {
        window->DrawList->AddRect(pos, pos + size, GetColorU32(ImGuiCol_Border), style.FrameRounding, 0, style.FrameBorderSize);
    }

    window->DrawList->AddRectFilledMultiColorRounded(pos, pos + size, GetColorU32(ImGuiCol_FrameBgShadow, 0.0f), GetColorU32(ImGuiCol_FrameBgShadow, 0.0f), GetColorU32(ImGuiCol_FrameBgShadow), GetColorU32(ImGuiCol_FrameBgShadow), style.FrameRounding);

    ImVec2 buf_display_size = ImGui::CalcTextSize(buf_display, NULL, true);
    ImVec2 text_pos = pos + ImTrunc((size - buf_display_size) * 0.5f);
    window->DrawList->AddText(text_pos, it_anim->second.Label, buf_display);

    return value_changed;
}

bool ImAdd::Tab(const char* label, bool selected, const ImVec2& size_arg)
{
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    const ImVec2 label_size = CalcTextSize(label, NULL, true);

    ImVec2 pos = window->DC.CursorPos;
    ImVec2 size = CalcItemSize(size_arg, label_size.x + style.FramePadding.x * 2.0f, label_size.y + style.FramePadding.y * 2.0f);

    const ImRect bb(pos, pos + size);
    ItemSize(size, style.FramePadding.y);
    if (!ItemAdd(bb, id))
        return false;

    bool hovered, held;
    bool pressed = ButtonBehavior(bb, id, &hovered, &held);

    // Colors
    const ImU32 col = GetColorU32(selected ? ImGuiCol_ChildBg : hovered && held ? ImGuiCol_TabActive : hovered ? ImGuiCol_TabHovered : ImGuiCol_Tab);

    // Render
    RenderNavCursor(bb, id);
    RenderFrame(bb.Min, bb.Max, col, false);

    window->DrawList->AddRectFilledMultiColor(bb.Min, bb.Max,
        GetColorU32(selected ? ImGuiCol_ButtonShadow : ImGuiCol_FrameBgShadow, selected ? 1.0f : 0.0f), GetColorU32(selected ? ImGuiCol_ButtonShadow : ImGuiCol_FrameBgShadow, selected ? 1.0f : 0.0f),
        GetColorU32(selected ? ImGuiCol_ButtonShadow : ImGuiCol_FrameBgShadow, selected ? 0.0f : 1.0f), GetColorU32(selected ? ImGuiCol_ButtonShadow : ImGuiCol_FrameBgShadow, selected ? 0.0f : 1.0f)
    );

    if (!selected)
    {
        window->DrawList->AddLine(ImVec2(bb.Min.x, bb.Max.y - style.ChildBorderSize), ImVec2(bb.Max.x, bb.Max.y - style.ChildBorderSize), GetColorU32(ImGuiCol_Border), style.ChildBorderSize);
    }

    RenderText(pos + ImTrunc((size - label_size) / 2) - ImVec2(0.0f, style.ChildBorderSize), label, NULL, true, true);

    return pressed;
}

void ImAdd::ScrollBar(const char* str_id, ImGuiWindow* window, const ImVec2& size_arg)
{
    if (!window || window->SkipItems)
        return;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(str_id);

    ImVec2 pos = window->DC.CursorPos;
    ImVec2 size = CalcItemSize(size_arg, GetFrameHeight(), CalcItemWidth());
    const ImRect total_bb(pos, pos + size);
    ItemSize(size);
    if (!ItemAdd(total_bb, id))
        return;

    bool hovered, held;
    bool pressed = ButtonBehavior(total_bb, id, &hovered, &held);

    // Scroll metrics
    float visible_height = size.y;
    float total_height = window->ContentSize.y;
    float scroll_max = ImMax(window->ScrollMax.y, 0.0f);
    float scroll_y = window->Scroll.y;

    float scroll_height = (total_height > 0.0f)
        ? (visible_height / total_height) * visible_height
        : visible_height;

    scroll_height = ImClamp(scroll_height, 15.0f, visible_height);

    float scroll_top = (scroll_max > 0.0f)
        ? (scroll_y / scroll_max) * (visible_height - scroll_height)
        : 0.0f;

    // Handle drag-to-scroll
    if (held && scroll_max > 0.0f)
    {
        float mouse_delta = g.IO.MouseDelta.y;
        float scrollable_range = visible_height - scroll_height;
        if (scrollable_range > 0.0f)
        {
            float ratio = scroll_max / scrollable_range;
            window->Scroll.y = ImClamp(window->Scroll.y + mouse_delta * ratio, 0.0f, scroll_max);
        }
    }

	// Handle mouse wheel scrolling
    bool hovered_window = IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    if (!held && (hovered || hovered_window) && scroll_max > 0.0f && !IsKeyDown(ImGuiKey_LeftShift))
    {
        // Disable native ImGui scrolling
        window->Flags |= ImGuiWindowFlags_NoScrollWithMouse;

        const float wheel_speed = 40.0f; // tweak to taste
        window->Scroll.y = ImClamp(
            window->Scroll.y - g.IO.MouseWheel * wheel_speed,
            0.0f,
            scroll_max
        );
    }

    // Colors
    ImU32 colGrab = GetColorU32((hovered && held) ? ImGuiCol_ScrollbarGrabActive : hovered ? ImGuiCol_ScrollbarGrabHovered : ImGuiCol_ScrollbarGrab);

    // Draw background and grab
    window->DrawList->AddRectFilled(total_bb.Min, total_bb.Max, GetColorU32(ImGuiCol_ScrollbarBg), style.ScrollbarRounding);
    window->DrawList->AddRectFilled(
        ImVec2(total_bb.Min.x, total_bb.Min.y + scroll_top),
        ImVec2(total_bb.Max.x, total_bb.Min.y + scroll_top + scroll_height),
        colGrab,
        style.ScrollbarRounding
    );

    RenderNavCursor(total_bb, id);
}

bool ImAdd::BeginChild(const char* str_id, std::vector<const char*> tabs, int* selected_tab_index_callback, const ImVec2& size_arg)
{
    ImGuiContext& g = *GImGui;
    ImGuiWindow* parent_window = g.CurrentWindow;
    //if (parent_window->SkipItems)
    //    return;

    const ImGuiID id = parent_window->GetID(str_id);
    const ImGuiStyle& style = g.Style;

	std::string str_id_tabs         = std::string(str_id) + "##child##tabs";
	std::string str_id_scrollbar    = std::string(str_id) + "##child##scrollbar";

    PushStyleVar(ImGuiStyleVar_WindowPadding, style.ChildPadding);
	bool result = ImGui::BeginChild(str_id, size_arg, ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    PopStyleVar();

    //if (result)
    {
        ImGuiWindow*    window  = GetCurrentWindow();
        ImVec2          cur_pos = window->DC.CursorPos;
        ImVec2          pos     = window->Pos;
        ImVec2          size    = window->Size;
		ImRect		    window_bb(pos, pos + size);
        bool            has_scroll = window->ScrollMax.y > 0;

        bool has_tabs = tabs.size() > 0;
        float tabs_height = GetFrameHeight();

        if (has_tabs)
        {
            SetCursorScreenPos(pos + ImVec2(0.0f, style.ChildBorderSize * 4.0f));
            PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(style.ChildBorderSize, 0.0f));
            if (ImGui::BeginChild(str_id_tabs.c_str(), ImVec2(size.x, tabs_height), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoBackground))
            {
                PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, style.ItemSpacing.y));
                PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);

                float tab_width = ImTrunc((size.x - style.ChildBorderSize * (tabs.size() + 1)) / tabs.size());

                for (int i = 0; i < tabs.size(); i++)
                {
                    bool selected = false;

                    static std::map<ImGuiID, int> anim;
                    auto it_anim = anim.find(id);

                    if (it_anim == anim.end())
                    {
                        anim.insert({ id, int() });
                        it_anim = anim.find(id);

                        it_anim->second = 0;
                    }

                    if (selected_tab_index_callback)
                    {
						selected = (*selected_tab_index_callback == i);
                    }
                    else
                    {
						selected = (it_anim->second == i);
                    }

                    if (Tab(tabs[i], selected, ImVec2(i == (tabs.size() - 1) ? GetContentRegionAvail().x : tab_width, tabs_height)))
                    {
                        if (selected_tab_index_callback)
                        {
							*selected_tab_index_callback = i;
                        }
                        else
                        {
                            it_anim->second = i;
                        }
                    }

                    if (i < tabs.size() - 1)
                    {
                        SameLine();
						VSeparator(0.0f, style.ChildBorderSize);
                        SameLine();
                    }
                }

                PopStyleVar(2);
            }
            ImGui::EndChild();
            PopStyleVar();
        }

        if (ImGui::GetCurrentWindow()->Flags & ImGuiWindowFlags_NoBackground)
        {
			window->DrawList->AddRectFilled(pos, pos + size, GetColorU32(ImGuiCol_ChildBg));

            if (style.ChildBorderSize > 0.0f)
            {
                // Window outer border
                window->DrawList->AddRect(window_bb.Min, window_bb.Max, ImGui::GetColorU32(ImGuiCol_Border), style.WindowRounding, ImDrawFlags_None, style.ChildBorderSize);

                // Window top decoration
                window->DrawList->AddLine(window_bb.Min + ImVec2(style.ChildBorderSize, style.ChildBorderSize), ImVec2(window_bb.Max.x - style.ChildBorderSize, window_bb.Min.y + style.ChildBorderSize), ImGui::GetColorU32(ImGuiCol_Header), style.ChildBorderSize);
                window->DrawList->AddLine(window_bb.Min + ImVec2(style.ChildBorderSize, style.ChildBorderSize * 2.0f), ImVec2(window_bb.Max.x - style.ChildBorderSize, window_bb.Min.y + style.ChildBorderSize * 2.0f), ImGui::GetColorU32(ImGuiCol_HeaderActive), style.ChildBorderSize);
                window->DrawList->AddLine(window_bb.Min + ImVec2(style.ChildBorderSize, style.ChildBorderSize * 3.0f), ImVec2(window_bb.Max.x - style.ChildBorderSize, window_bb.Min.y + style.ChildBorderSize * 3.0f), ImGui::GetColorU32(ImGuiCol_Border), style.ChildBorderSize);
            }
        }

		float scroll_offset_y = style.ChildBorderSize * 3.0f + (has_tabs ? tabs_height : style.ChildBorderSize);

        if (has_scroll)
        {
            SetCursorScreenPos(pos + ImVec2(size.x - style.ScrollbarSize - style.ChildPadding.x, scroll_offset_y + style.ChildPadding.y));
            ImAdd::ScrollBar(str_id_scrollbar.c_str(), window, ImVec2(style.ScrollbarSize, size.y - scroll_offset_y - style.ChildPadding.y * 2.0f));
        }

        SetCursorScreenPos(cur_pos + ImVec2(0.0f, style.ChildBorderSize * 3.0f + (has_tabs ? tabs_height : 0.0f)));

        if (has_scroll)
        {
            window->ContentRegionRect.Max.x -= style.ScrollbarSize + style.ChildPadding.x;
        }

        window->DrawList->PushClipRect(window_bb.Min + ImVec2(style.ChildBorderSize, style.ChildBorderSize * 3.0f + (has_tabs ? tabs_height : style.ChildBorderSize)), window_bb.Max - ImVec2(style.ChildBorderSize, has_tabs ? 0.0f : style.ChildBorderSize), true);
    }

    PushItemWidth(GetContentRegionAvail().x);

    return result;
}

bool ImAdd::BeginChild(const char* str_id, std::vector<const char*> tabs, const ImVec2& size_arg)
{
	return ImAdd::BeginChild(str_id, tabs, (int*)NULL, size_arg);
}

bool ImAdd::BeginChild(const char* str_id, const ImVec2& size_arg)
{
	return ImAdd::BeginChild(str_id, {}, (int*)NULL, size_arg);
}

void ImAdd::EndChild()
{
    PopItemWidth();

    ImGuiWindow* window = GetCurrentWindow();

    window->DrawList->PopClipRect();

    ImGui::EndChild();
}

bool ImAdd::SliderScalar(const char* label, ImGuiDataType data_type, void* p_data, const void* p_min, const void* p_max, const char* format)
{
    ImGuiWindow* window = GetCurrentWindow();
    if (window->SkipItems)
        return false;

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);

    const float width = CalcItemWidth();
    const ImVec2 pos = window->DC.CursorPos;

    const ImVec2 label_size = CalcTextSize(label, NULL, true);
    const bool has_label = label_size.x > 0;
    const float frame_pos_y = has_label ? (g.FontSize + style.ItemInnerSpacing.y) : 0.0f;
    const float frame_height = g.FontSize;

    const ImRect frame_bb(pos + ImVec2(0, frame_pos_y), pos + ImVec2(width, frame_pos_y + frame_height));
    const ImRect total_bb(pos, frame_bb.Max);

    ItemSize(total_bb);
    if (!ItemAdd(total_bb, id, &frame_bb, 0))
        return false;

    if (format == NULL)
        format = DataTypeGetInfo(data_type)->PrintFmt;

    const bool hovered = ItemHoverable(frame_bb, id, g.LastItemData.ItemFlags);
    const bool clicked = hovered && IsMouseClicked(0, ImGuiInputFlags_None, id);
    const bool held = g.ActiveId == id;
    const bool make_active = (clicked || g.NavActivateId == id);

    if (make_active)
    {
        SetActiveID(id, window);
        SetFocusID(id, window);
        FocusWindow(window);
        g.ActiveIdUsingNavDirMask |= (1 << ImGuiDir_Left) | (1 << ImGuiDir_Right);
    }

    // Colors
    ImU32 colFrame = GetColorU32((hovered && held) ? ImGuiCol_ButtonActive : hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
    ImU32 colLine = GetColorU32(held ? ImGuiCol_SliderGrabActive : ImGuiCol_SliderGrab);

    // Grab logic
    ImRect grab_bb;
    const bool value_changed = SliderBehavior(frame_bb, id, data_type, p_data, p_min, p_max, format, 0, &grab_bb);
    if (value_changed)
        MarkItemEdited(id);

    float relative_value = 0.0f;
    if (data_type == ImGuiDataType_Float)
    {
        float val = *(float*)p_data;
        float min_val = *(float*)p_min;
        float max_val = *(float*)p_max;
        relative_value = (val - min_val) / (max_val - min_val);
    }
    else if (data_type == ImGuiDataType_S32)
    {
        int val = *(int*)p_data;
        int min_val = *(int*)p_min;
        int max_val = *(int*)p_max;
        relative_value = (float)(val - min_val) / (float)(max_val - min_val);
    }

    relative_value = ImClamp(relative_value, 0.0f, 1.0f);

    // Draw frame

    const float pad = ImTrunc(frame_height / 3.0f);

    window->DrawList->AddRectFilled(frame_bb.Min, frame_bb.Max, colFrame, style.FrameRounding);

    ImVec2 fill_end = ImTrunc(ImVec2(frame_bb.Min.x + relative_value * frame_bb.GetWidth(), frame_bb.Max.y));

    ImRect slider_bb = ImRect(ImTrunc(frame_bb.Min), ImTrunc(fill_end));

    if (slider_bb.Max.x > slider_bb.Min.x + style.FrameRounding)
    {
        window->DrawList->AddRectFilled(slider_bb.Min, slider_bb.Max, colLine, style.FrameRounding);
    }

    window->DrawList->AddRectFilledMultiColorRounded(frame_bb.Min, frame_bb.Max, GetColorU32(ImGuiCol_FrameBgShadow, 0.0f), GetColorU32(ImGuiCol_FrameBgShadow, 0.0f), GetColorU32(ImGuiCol_FrameBgShadow), GetColorU32(ImGuiCol_FrameBgShadow), style.FrameRounding);
    
    if (style.FrameBorderSize > 0.0f)
    {
        window->DrawList->AddRect(frame_bb.Min, frame_bb.Max, GetColorU32(ImGuiCol_Border), style.FrameRounding, 0, style.FrameBorderSize);
    }

    char value_buf[64];
    const char* value_buf_end = value_buf + DataTypeFormatString(value_buf, IM_ARRAYSIZE(value_buf), data_type, p_data, format);

    if (has_label) {
        RenderText(total_bb.Min, label, NULL, true, true);
		PushStyleColor(ImGuiCol_Text, style.Colors[ImGuiCol_TextDisabled]);
        RenderText(total_bb.Min + ImVec2(width - CalcTextSize(value_buf).x, 0), value_buf, NULL, true, true);
        PopStyleColor();
    }

    return value_changed;
}

bool ImAdd::SliderFloat(const char* label, float* v, float v_min, float v_max, const char* format)
{
    return ImAdd::SliderScalar(label, ImGuiDataType_Float, v, &v_min, &v_max, format);
}

bool ImAdd::SliderInt(const char* label, int* v, int v_min, int v_max, const char* format)
{
    return ImAdd::SliderScalar(label, ImGuiDataType_S32, v, &v_min, &v_max, format);
}

void ImAdd::RenderText(ImVec2 pos, const char* text, const char* text_end, bool hide_text_after_hash, ImAddTextFlags flags)
{
    ImGuiContext& g = *GImGui;

    if (flags)
    {
        PushStyleColor(ImGuiCol_Text, g.Style.Colors[ImGuiCol_Border]);

        ImGui::RenderText(pos + ImVec2(0, 1), text, text_end, hide_text_after_hash);
        ImGui::RenderText(pos + ImVec2(0, -1), text, text_end, hide_text_after_hash);
        ImGui::RenderText(pos + ImVec2(1, 0), text, text_end, hide_text_after_hash);
        ImGui::RenderText(pos + ImVec2(-1, 0), text, text_end, hide_text_after_hash);
        ImGui::RenderText(pos + ImVec2(1, 1), text, text_end, hide_text_after_hash);
        ImGui::RenderText(pos + ImVec2(-1, -1), text, text_end, hide_text_after_hash);
        ImGui::RenderText(pos + ImVec2(1, -1), text, text_end, hide_text_after_hash);
        ImGui::RenderText(pos + ImVec2(-1, 1), text, text_end, hide_text_after_hash);

        PopStyleColor();
    }

    ImGui::RenderText(pos, text, text_end, hide_text_after_hash);
}