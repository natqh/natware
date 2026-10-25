#pragma once

#include <imgui/imgui.h>
#include <cstddef>
#include <initializer_list>

// Widget kit for the menu. Every control is drawn here so the whole UI shares
// one set of metrics and colours: black & white over the Dark Veil background.
namespace ui
{
    // ---- setup -------------------------------------------------------------
    void LoadFonts(ImFont* fallback);
    void ApplyStyle();
    ImFont* Regular();
    ImFont* SemiBold();
    ImFont* Brand();
    ImFont* EspFont();  // crisp face for in-game labels   // Monocraft, the original external wordmark
    constexpr float kBrandSize = 18.0f;

    // ---- motion ------------------------------------------------------------
    // One critically damped spring (SmoothDamp) drives every transition: zero speed at both
    // ends, no overshoot. kSmoothTime settles in ~0.6 s.
    constexpr float kSmoothTime = 0.2f;
    float Spring(float value, float& velocity, float target, float smooth_time = kSmoothTime);
    // The spring's step response from rest: 0 -> 1 over ~3 smooth times.
    float SpringIn(float seconds, float smooth_time = kSmoothTime);

    namespace col
    {
        constexpr ImU32 text       = IM_COL32(240, 240, 243, 255);
        constexpr ImU32 text_dim   = IM_COL32(240, 240, 243, 150);
        constexpr ImU32 text_faint = IM_COL32(240, 240, 243, 95);
        constexpr ImU32 white      = IM_COL32(248, 248, 250, 255);
        constexpr ImU32 shell      = IM_COL32(10, 10, 10, 255);
        constexpr ImU32 line       = IM_COL32(255, 255, 255, 14);
        constexpr ImU32 danger     = IM_COL32(255, 112, 112, 255);
    }

    enum class Icon
    {
        Crosshair, Target, Users, User, Eye, Globe, Zap, Sliders, Save, Layers, List,
        Close, Check, ChevronDown, Minus, Plus, Activity, Server, Move,
    };
    void DrawIcon(ImDrawList* dl, Icon icon, ImVec2 center, ImU32 color, float scale = 1.0f);

    // `own_highlight`: draw the selection pane here; false when the caller draws one shared pane
    // that glides between items.
    bool NavItem(const char* label, Icon icon, bool selected, ImVec2 min, ImVec2 max, bool own_highlight = true);
    float Appear(ImGuiID id, float delay = 0.0f, float smooth_time = kSmoothTime);

    // React Bits Click Spark: call once per frame after the menu windows are drawn.
    void ClickSparks();

    // ---- layout ------------------------------------------------------------
    // Pages are one scrolling column of titled sections, scrolled smoothly by the wheel.
    // Columns are for genuinely side-by-side content (the player list and its details).
    void BeginPage(const char* id);
    void EndPage();
    void BeginColumns(const char* id);
    void NextColumn();
    void EndColumns();
    // A titled section: the title sits above the card, rows inside are divided by hairlines,
    // and the card carries a cursor-following spotlight (React Bits Spotlight Card).
    void BeginCard(const char* title);
    void EndCard();
    // The cards' cursor spotlight on any card-shaped pane drawn by hand (the launcher's cards).
    void Spotlight(ImDrawList* dl, ImGuiID id, ImVec2 min, ImVec2 max);
    // Compact spacing (shorter rows, buttons and gaps) for the tool windows; false restores the
    // page metrics.
    void SetCompact(bool compact);
    void BeginSub();   // indented block for settings that depend on the row above
    void EndSub();
    void Spacing(float h = 8.0f);
    float RowWidth();

    // ---- rows --------------------------------------------------------------
    bool Toggle(const char* label, bool* v);
    bool ToggleKey(const char* label, bool* v, int* key, int* key_mode);
    bool ToggleColor(const char* label, bool* v, float* color, float* color2 = nullptr);
    bool ToggleKeyColor(const char* label, bool* v, int* key, int* key_mode, float* color, float* color2 = nullptr);
    bool Slider(const char* label, float* v, float min, float max, const char* fmt = "%.1f");
    bool SliderInt(const char* label, int* v, int min, int max, const char* fmt = "%d");
    bool Combo(const char* label, int* v, std::initializer_list<const char*> items);
    bool Keybind(const char* label, int* key, int* key_mode = nullptr);
    bool ColorRow(const char* label, float* color);
    bool InputText(const char* id, char* buf, size_t size, const char* hint);
    void Text(const char* text, ImU32 color = col::text_dim);
    void Value(const char* label, const char* value, ImU32 value_color = col::text);
    void Legend(const char* text, ImU32 swatch);
    void Empty(const char* text);

    enum class Btn { Primary, Secondary, Danger };
    bool Button(const char* label, Btn kind = Btn::Secondary, float width = 0.0f);
    bool ListItem(const char* label, bool selected, const char* detail = nullptr, ImU32 label_color = col::text);
}
