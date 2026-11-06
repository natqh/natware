#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "ui.h"
#include "veil.h"

#include <imgui/imgui_internal.h>
#include <imgui/misc/imgui_freetype.h>
#include <menu/keybind/keybind.h>
#include "../../../ext/font/monocraft.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cctype>
#include <cfloat>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace ui
{
namespace
{
    // Spacing comes in two densities: roomy for the menu's pages, compact for the tool windows
    // (the Explorer and Player list felt too spacious at the page metrics).
    struct Metrics
    {
        float row;          // a toggle / combo / value row
        float card_pad;     // horizontal padding inside a card
        float card_pad_y;   // rows carry their own vertical air
        float card_round;
        float card_gap;     // between sections
        float title_h;      // section title above a card
        float sub_indent;
        float button_h;
        float list_h;       // a ListItem
        float block_gap;    // before a non-row block opening a card
    };
    constexpr Metrics kRoomy   = { 44.0f, 16.0f, 4.0f, 12.0f, 28.0f, 32.0f, 10.0f, 36.0f, 38.0f, 10.0f };
    constexpr Metrics kCompact = { 34.0f, 12.0f, 3.0f, 10.0f, 14.0f, 30.0f, 10.0f, 28.0f, 30.0f, 8.0f };
    const Metrics* g_metrics = &kRoomy;
    const Metrics& M() { return *g_metrics; }

    constexpr float kSliderH    = 66.0f;
    constexpr float kColGap     = 18.0f;
    constexpr float kGutter     = 12.0f;   // right gutter of a scroll area, holds the scroll indicator
    constexpr float kSwitchW    = 36.0f;
    constexpr float kSwitchH    = 20.0f;
    constexpr float kSwatch     = 20.0f;
    constexpr float kPopupItemH = 28.0f;

    constexpr float kLabelSize  = 15.0f;   // Arial
    constexpr float kSmallSize  = 14.0f;
    constexpr float kKeySize    = 13.0f;   // text in a keybind pill
    constexpr float kSectionSize = 18.0f;  // Monocraft section labels: twice its pixel grid

    constexpr float kCardFrost  = 0.62f;  // Glass Surface backgroundOpacity
    constexpr float kGlassBend  = -180.0f; // Glass Surface distortionScale (the component's default)
    constexpr ImU32 kCtrlFill   = IM_COL32(255, 255, 255, 12);
    constexpr ImU32 kCtrlHover  = IM_COL32(255, 255, 255, 20);
    constexpr ImU32 kCtrlBorder = IM_COL32(255, 255, 255, 16);
    constexpr ImU32 kPopupFill  = IM_COL32(20, 15, 12, 255);
    constexpr ImU32 kPopupLine  = IM_COL32(255, 225, 190, 26);

    ImFont* g_regular  = nullptr;
    ImFont* g_semibold = nullptr;
    ImFont* g_brand    = nullptr;
    ImFont* g_esp = nullptr;

    // A card's recessed sub-blocks are remembered as y ranges and painted in EndCard, under the rows.
    struct CardState { ImVec2 start; float width; ImGuiID id; int rows; int items; std::vector<ImVec2> subs; };
    std::vector<CardState> g_cards;
    bool g_last_was_row = false;   // decides the card's bottom padding
    struct SubState { float start_y; int items; };
    std::vector<SubState>  g_subs;

    // True for the first thing in a sub-block (it needs no divider: the panel's edge is its top).
    bool SubFirst()
    {
        return !g_subs.empty() && g_subs.back().items++ == 0;
    }
    float g_col_w = 0.0f;
    float g_col_h = 0.0f;

    ImGuiID g_capture_id = 0;
    int     g_capture_frame = 0;
    bool    g_key_prev[256] = {};

    // ID of the most recent row. BeginSub scopes its rows under it, so sub-settings can
    // reuse a label ("Speed" under "Speed", "Mode" under both Speed and Fly) without clashing.
    ImGuiID g_last_row_id = 0;

    // Every custom colour goes through C() so BeginDisabled() dims it like ImGui's own widgets.
    ImU32 C(ImU32 c)
    {
        const float a = GImGui->Style.Alpha;
        if (a >= 1.0f)
            return c;
        const ImU32 alpha = (ImU32)((float)((c >> IM_COL32_A_SHIFT) & 0xFF) * a);
        return (c & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
    }

    ImU32 Fade(ImU32 c, float a)
    {
        const ImU32 alpha = (ImU32)((float)((c >> IM_COL32_A_SHIFT) & 0xFF) * ImClamp(a, 0.0f, 1.0f));
        return (c & ~IM_COL32_A_MASK) | (alpha << IM_COL32_A_SHIFT);
    }

    ImU32 Mix(ImU32 a, ImU32 b, float t)
    {
        return ImGui::ColorConvertFloat4ToU32(ImLerp(ImGui::ColorConvertU32ToFloat4(a), ImGui::ColorConvertU32ToFloat4(b), t));
    }

    struct Motion { float value, velocity; };

    std::unordered_map<ImGuiID, Motion>& AnimStore()
    {
        static std::unordered_map<ImGuiID, Motion> values;
        return values;
    }

    // Every UI transition runs on a critically damped spring: it leaves at zero speed, eases
    // in and out, never overshoots, and retargets mid-flight without a jolt. With the default
    // smooth time it settles in ~0.6 s.
    float Anim(ImGuiID id, float target, float smooth_time = kSmoothTime)
    {
        Motion& m = AnimStore().try_emplace(id, Motion{ target, 0.0f }).first->second;
        m.value = ui::Spring(m.value, m.velocity, target, smooth_time);
        return m.value;
    }

    ImGuiID Sub(ImGuiID id, const char* salt) { return ImHashStr(salt, 0, id); }

    // React Bits Animated List: an item fades in and scales 0.7 -> 1 each time it comes into
    // view, after a 0.1 s delay, on the house spring rather than the component's 0.2 s tween.
    float ListEnter(ImGuiID id) { return Appear(id, 0.1f, 0.16f); }

    void ScaleVerts(ImDrawList* dl, int from, ImVec2 center, float scale)
    {
        for (int i = from; i < dl->VtxBuffer.Size; i++)
            dl->VtxBuffer[i].pos = center + (dl->VtxBuffer[i].pos - center) * scale;
    }

    ImVec2 TextSize(ImFont* f, float size, const char* text, const char* end = nullptr)
    {
        return f->CalcTextSizeA(size, FLT_MAX, 0.0f, text, end);
    }

    void DrawText(ImDrawList* dl, ImFont* f, float size, ImVec2 pos, ImU32 col, const char* text, const char* end = nullptr)
    {
        dl->AddText(f, size, ImVec2(floorf(pos.x + 0.5f), floorf(pos.y + 0.5f)), C(col), text, end);
    }

    // Draws `text` (up to any "##") and ends it with an ellipsis if it would cross max_x.
    void DrawLabel(ImDrawList* dl, ImFont* f, float size, ImVec2 pos, float max_x, ImU32 col, const char* text)
    {
        const char* end = ImGui::FindRenderedTextEnd(text);
        const float avail = max_x - pos.x;
        if (TextSize(f, size, text, end).x <= avail)
        {
            DrawText(dl, f, size, pos, col, text, end);
            return;
        }
        const char* ellipsis = "\xE2\x80\xA6";
        const char* cut = text;
        f->CalcTextSizeA(size, ImMax(0.0f, avail - TextSize(f, size, ellipsis).x), 0.0f, text, end, &cut);
        DrawText(dl, f, size, pos, col, text, cut);
        DrawText(dl, f, size, ImVec2(pos.x + TextSize(f, size, text, cut).x, pos.y), col, ellipsis);
    }

    struct Row { ImRect bb; ImGuiID id; };

    float RightEdge(ImGuiWindow* w)
    {
        // Inside a sub-block the controls keep the same margin from the panel's right edge as the
        // labels have from its left.
        if (!g_cards.empty())
            return g_cards.back().start.x + g_cards.back().width - M().card_pad - M().sub_indent * (float)g_subs.size();
        return w->DC.CursorPos.x + ImGui::GetContentRegionAvail().x;
    }

    // A hairline across the current row's width: to the card's edge, or to the panel's edge
    // inside a sub-block.
    void RowLine(ImGuiWindow* w, float x0, float y)
    {
        const float x1 = RightEdge(w) + (g_subs.empty() ? M().card_pad : 0.0f);
        w->DrawList->AddLine(ImVec2(x0, floorf(y) + 0.5f), ImVec2(x1, floorf(y) + 0.5f),
            IM_COL32(255, 255, 255, (int)(14 * GImGui->Style.Alpha)), 1.0f);
    }

    Row MakeRow(const char* label, float height)
    {
        ImGuiWindow* w = ImGui::GetCurrentWindow();
        const ImVec2 pos = w->DC.CursorPos;
        Row r{ ImRect(pos, ImVec2(RightEdge(w), pos.y + height)), w->GetID(label) };
        ImGui::ItemSize(r.bb.GetSize());
        g_last_row_id = r.id;
        g_last_was_row = true;

        // Hairline between rows of a card, starting at the row's (possibly indented) left edge.
        const bool sub_first = SubFirst();
        if (!g_cards.empty())
            g_cards.back().items++;
        if (!g_cards.empty() && g_cards.back().rows++ > 0 && !sub_first)
            RowLine(w, r.bb.Min.x, r.bb.Min.y);
        return r;
    }

    ImU32 LabelColor(bool hovered)
    {
        if (hovered)
            return col::white;
        return g_subs.empty() ? col::text : IM_COL32(240, 240, 243, 190);
    }

    float LabelY(float center_y) { return center_y - kLabelSize * 0.5f - 1.0f; }

    // ---- popups ---------------------------------------------------------------

    // Popups stay anchored to their control and fade in while settling 6px into place.
    bool PopupBegin(ImGuiID id, float width, ImVec2 pos, ImVec2 pivot, float max_height = FLT_MAX)
    {
        if (!ImGui::IsPopupOpen(id, ImGuiPopupFlags_None))
            return false;

        const float enter = Appear(ImHashStr("##enter", 0, id), 0.0f, 0.1f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * enter);
        ImGui::SetNextWindowPos(ImVec2(pos.x, pos.y - (1.0f - enter) * 6.0f), ImGuiCond_Always, pivot);
        ImGui::SetNextWindowSizeConstraints(ImVec2(width, 0.0f), ImVec2(width, max_height));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(5.0f, 5.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 9.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, kPopupFill);
        ImGui::PushStyleColor(ImGuiCol_Border, kPopupLine);
        const bool open = ImGui::BeginPopupEx(id, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar
            | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);
        if (!open)
        {
            ImGui::PopStyleVar();   // alpha
            return false;
        }
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 2.0f));
        return true;
    }

    void PopupEnd()
    {
        ImGui::PopStyleVar();   // item spacing
        ImGui::EndPopup();
        ImGui::PopStyleVar();   // alpha
    }

    bool PopupItem(const char* label, bool selected)
    {
        ImGuiWindow* w = ImGui::GetCurrentWindow();
        const ImGuiID id = w->GetID(label);
        const ImVec2 pos = w->DC.CursorPos;
        const ImRect bb(pos, ImVec2(pos.x + ImGui::GetContentRegionAvail().x, pos.y + kPopupItemH));
        ImGui::ItemSize(bb.GetSize());
        if (!ImGui::ItemAdd(bb, id))
            return false;

        bool hovered = false, held = false;
        const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
        ImDrawList* dl = w->DrawList;
        if (hovered)
            dl->AddRectFilled(bb.Min, bb.Max, C(IM_COL32(255, 255, 255, 15)), 6.0f);

        const float cy = bb.GetCenter().y;
        DrawLabel(dl, g_regular, kLabelSize, ImVec2(bb.Min.x + 10.0f, LabelY(cy)), bb.Max.x - 26.0f,
            selected || hovered ? col::white : col::text_dim, label);
        if (selected)
            DrawIcon(dl, Icon::Check, ImVec2(bb.Max.x - 14.0f, cy), C(col::accent), 0.75f);

        if (pressed)
            ImGui::CloseCurrentPopup();
        return pressed;
    }

    // ---- sub-controls placed inside a row ---------------------------------------

    void DrawSwitch(ImDrawList* dl, const ImRect& bb, float t, bool hovered)
    {
        const float r = bb.GetHeight() * 0.5f;
        const ImU32 off = hovered ? IM_COL32(255, 255, 255, 40) : IM_COL32(255, 255, 255, 28);
        dl->AddRectFilled(bb.Min, bb.Max, C(Mix(off, col::accent, t)), r);
        dl->AddRect(bb.Min, bb.Max, C(Mix(IM_COL32(255, 255, 255, hovered ? 38 : 26), IM_COL32(255, 255, 255, 0), t)), r);
        const float kx = bb.Min.x + r + (bb.GetWidth() - 2.0f * r) * t;
        const ImVec2 knob(kx, bb.Min.y + r);
        dl->AddCircleFilled(ImVec2(knob.x, knob.y + 1.0f), r - 2.5f, C(IM_COL32(0, 0, 0, (int)(70 * t + 30))), 0);   // soft knob shadow
        dl->AddCircleFilled(knob, r - 3.0f, C(Mix(IM_COL32(236, 230, 222, 230), col::white, t)), 0);   // stays white when on (the user's call)
    }

    bool Swatch(const char* str_id, const ImRect& bb, float* color)
    {
        ImGuiWindow* w = ImGui::GetCurrentWindow();
        const ImGuiID id = w->GetID(str_id);
        ImGui::ItemAdd(bb, id);
        bool hovered = false, held = false;
        const ImGuiID popup = Sub(id, "##picker");
        if (ImGui::ButtonBehavior(bb, id, &hovered, &held))
            ImGui::OpenPopupEx(popup);

        ImDrawList* dl = w->DrawList;
        if (color[3] < 1.0f)
        {
            // Checkerboard so transparency reads as transparency.
            const ImVec2 mid = bb.GetCenter();
            dl->AddRectFilled(bb.Min, bb.Max, C(IM_COL32(205, 205, 210, 255)), 5.0f);
            dl->AddRectFilled(bb.Min, mid, C(IM_COL32(120, 120, 126, 255)), 5.0f, ImDrawFlags_RoundCornersTopLeft);
            dl->AddRectFilled(mid, bb.Max, C(IM_COL32(120, 120, 126, 255)), 5.0f, ImDrawFlags_RoundCornersBottomRight);
        }
        dl->AddRectFilled(bb.Min, bb.Max, C(ImGui::ColorConvertFloat4ToU32(ImVec4(color[0], color[1], color[2], color[3]))), 5.0f);
        dl->AddRect(bb.Min, bb.Max, C(hovered ? IM_COL32(255, 255, 255, 110) : IM_COL32(255, 255, 255, 45)), 5.0f);

        bool changed = false;
        if (PopupBegin(popup, 230.0f, ImVec2(bb.Max.x, bb.Max.y + 6.0f), ImVec2(1.0f, 0.0f)))
        {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 6.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7.0f, 5.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(6.0f, 6.0f));
            ImGui::SetNextItemWidth(-FLT_MIN);
            changed = ImGui::ColorPicker4("##picker", color,
                ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoLabel
                | ImGuiColorEditFlags_DisplayHex);
            ImGui::PopStyleVar(3);
            PopupEnd();
        }
        return changed;
    }

    const char* KeyName(int key)
    {
        if (key <= 0)
            return "None";
        const char* name = keybind::get_key_name(key);
        if (!name || !*name)
            return "?";
        static char upper[32];
        size_t n = 0;
        for (; name[n] && n < sizeof(upper) - 1; n++)
            upper[n] = (char)toupper((unsigned char)name[n]);
        upper[n] = '\0';
        return upper;
    }

    const char* KeyText(ImGuiID id, int key)
    {
        return g_capture_id == id ? "Press key" : (key == 0 ? "Bind" : KeyName(key));
    }

    float KeyWidth(ImGuiID id, int key)
    {
        return ImMax(62.0f, TextSize(g_regular, kKeySize, KeyText(id, key)).x + 26.0f);
    }

    // Click to listen for the next key or mouse button (Esc clears the key, clicking elsewhere
    // cancels).
    // Right-click picks the activation mode: 1 = Hold, 0 = Toggle, 2 = Always.
    bool KeyPill(ImGuiID id, const ImRect& bb, int* key, int* mode)
    {
        ImGuiContext& g = *GImGui;
        ImGuiWindow* w = ImGui::GetCurrentWindow();
        ImGui::ItemAdd(bb, id);
        const bool hovered = ImGui::ItemHoverable(bb, id, ImGuiItemFlags_None);
        bool changed = false;

        if (g_capture_id != id)
        {
            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                g_capture_id = id;
                g_capture_frame = g.FrameCount;
                for (int k = 1; k < 256; k++)
                    g_key_prev[k] = (GetAsyncKeyState(k) & 0x8000) != 0;
            }
        }
        else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !hovered)
        {
            g_capture_id = 0;
        }
        else if (g.FrameCount > g_capture_frame + 1)
        {
            for (int k = 1; k < 256; k++)
            {
                const bool down = (GetAsyncKeyState(k) & 0x8000) != 0;
                if (down && !g_key_prev[k])
                {
                    const int bound = k == VK_ESCAPE ? 0 : k;   // Esc unbinds
                    if (*key != bound)
                    {
                        *key = bound;
                        changed = true;
                    }
                    g_capture_id = 0;
                    break;
                }
                g_key_prev[k] = down;
            }
        }

        const ImGuiID popup = Sub(id, "##mode");
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            ImGui::OpenPopupEx(popup);

        const bool capturing = g_capture_id == id;
        const float h = Anim(id, (hovered || capturing) ? 1.0f : 0.0f);
        ImDrawList* dl = w->DrawList;
        dl->AddRectFilled(bb.Min, bb.Max, C(Mix(kCtrlFill, kCtrlHover, h)), 6.0f);
        dl->AddRect(bb.Min, bb.Max, C(capturing ? IM_COL32(210, 200, 232, 110) : IM_COL32(255, 255, 255, 23)), 5.0f);
        dl->AddLine(ImVec2(bb.Min.x + 7, bb.Max.y - 3), ImVec2(bb.Max.x - 7, bb.Max.y - 3), C(IM_COL32(255, 255, 255, 10)));
        const char* text = KeyText(id, *key);
        const ImVec2 ts = TextSize(g_regular, kKeySize, text);
        DrawText(dl, g_regular, kKeySize, bb.GetCenter() - ts * 0.5f,
            capturing ? col::white : (*key > 0 ? col::text : col::text_faint), text);

        if (hovered && !capturing && g.HoveredIdTimer > 0.6f && !ImGui::IsPopupOpen(popup, ImGuiPopupFlags_None))
            ImGui::SetTooltip("%s", mode ? "Click to bind, right-click for mode" : "Click to bind");

        if (PopupBegin(popup, 132.0f, ImVec2(bb.Max.x, bb.Max.y + 6.0f), ImVec2(1.0f, 0.0f)))
        {
            if (mode)
            {
                if (PopupItem("Hold", *mode == 1))   *mode = 1;
                if (PopupItem("Toggle", *mode == 0)) *mode = 0;
                if (PopupItem("Always", *mode == 2)) *mode = 2;
                ImGui::ItemSize(ImVec2(0.0f, 2.0f));
                const ImVec2 p = ImGui::GetCursorScreenPos();
                ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x + 6.0f, p.y), ImVec2(p.x + ImGui::GetContentRegionAvail().x - 6.0f, p.y), C(kPopupLine));
                ImGui::ItemSize(ImVec2(0.0f, 2.0f));
            }
            if (PopupItem("Clear", false))
            {
                *key = 0;
                changed = true;
            }
            PopupEnd();
        }
        return changed;
    }

    bool ToggleImpl(const char* label, bool* v, int* key, int* key_mode, float* c1, float* c2)
    {
        ImGuiWindow* w = ImGui::GetCurrentWindow();
        if (w->SkipItems)
            return false;

        const Row r = MakeRow(label, M().row);
        ImDrawList* dl = w->DrawList;
        const float cy = r.bb.GetCenter().y;

        float x = r.bb.Max.x;
        const ImRect sw(ImVec2(x - kSwitchW, cy - kSwitchH * 0.5f), ImVec2(x, cy + kSwitchH * 0.5f));
        x = sw.Min.x - 10.0f;

        ImGui::PushID(label);
        if (c2)
        {
            const ImRect s(ImVec2(x - kSwatch, cy - kSwatch * 0.5f), ImVec2(x, cy + kSwatch * 0.5f));
            Swatch("##c2", s, c2);
            x = s.Min.x - 6.0f;
        }
        if (c1)
        {
            const ImRect s(ImVec2(x - kSwatch, cy - kSwatch * 0.5f), ImVec2(x, cy + kSwatch * 0.5f));
            Swatch("##c1", s, c1);
            x = s.Min.x - 8.0f;
        }
        if (key)
        {
            const ImGuiID kid = w->GetID("##key");
            const float kw = KeyWidth(kid, *key);
            const ImRect k(ImVec2(x - kw, cy - 12.0f), ImVec2(x, cy + 12.0f));
            KeyPill(kid, k, key, key_mode);
            x = k.Min.x - 8.0f;
        }
        ImGui::PopID();

        // Added after the sub-controls so they win the hover when the cursor is over them.
        ImGui::ItemAdd(r.bb, r.id);
        bool hovered = false, held = false;
        const bool pressed = ImGui::ButtonBehavior(r.bb, r.id, &hovered, &held);
        if (pressed)
        {
            *v = !*v;
            ImGui::MarkItemEdited(r.id);
        }

        const float t = Anim(r.id, *v ? 1.0f : 0.0f);
        DrawLabel(dl, g_regular, kLabelSize, ImVec2(r.bb.Min.x, LabelY(cy)), x, LabelColor(hovered), label);
        DrawSwitch(dl, sw, t, hovered);
        return pressed;
    }

    // ---- Elastic Slider (React Bits) ---------------------------------------------
    // No thumb: the bar fills to the value. Hovering grows it 6 -> 12 px and fades it in;
    // dragging past an end stretches it (sigmoid decay, anchored on the far side), thins it,
    // and pushes and pops that end's icon; on release the stretch springs back.

    // React Bits uses 50; inside a card the end icon would slide out of the column at that reach.
    constexpr float kMaxOverflow = 32.0f;

    struct ElasticState
    {
        float hover = 0.0f, hover_v = 0.0f;
        float shown = -1.0f, shown_v = 0.0f;   // the fill, gliding to the value
        float overflow = 0.0f;
        float velocity = 0.0f;
        float client_x = 0.0f;
        int   region = 0;            // -1 left of the track, 0 on it, 1 right of it
        float pop[2] = { 1.0f, 1.0f };  // progress of each icon's 1 -> 1.4 -> 1 pop
    };

    std::unordered_map<ImGuiID, ElasticState>& ElasticStore()
    {
        static std::unordered_map<ImGuiID, ElasticState> states;
        return states;
    }

    float Decay(float value, float max)
    {
        if (max == 0.0f)
            return 0.0f;
        const float entry = value / max;
        const float sigmoid = 2.0f * (1.0f / (1.0f + expf(-entry)) - 0.5f);
        return sigmoid * max;
    }

    // 1 -> 1.4 -> 1 on a sine arch, so the pop eases at both ends.
    float PopScale(float t)
    {
        return 1.0f + 0.4f * sinf(ImClamp(t, 0.0f, 1.0f) * IM_PI);
    }

    double ReadValue(ImGuiDataType type, const void* v)
    {
        return type == ImGuiDataType_S32 ? (double)*(const int*)v : (double)*(const float*)v;
    }

    bool WriteValue(ImGuiDataType type, void* v, double value)
    {
        if (type == ImGuiDataType_S32)
        {
            const int n = (int)floor(value + 0.5);
            if (*(int*)v == n) return false;
            *(int*)v = n;
            return true;
        }
        const float f = (float)value;
        if (*(float*)v == f) return false;
        *(float*)v = f;
        return true;
    }

    bool SliderImpl(const char* label, ImGuiDataType type, void* v, const void* vmin, const void* vmax, const char* fmt)
    {
        ImGuiWindow* w = ImGui::GetCurrentWindow();
        if (w->SkipItems)
            return false;
        ImGuiContext& g = *GImGui;
        const ImGuiIO& io = ImGui::GetIO();

        const Row r = MakeRow(label, kSliderH);
        ImDrawList* dl = w->DrawList;
        const float label_cy = r.bb.Min.y + 21.0f;
        const float bar_cy = r.bb.Min.y + 47.0f;

        char value_buf[64];
        ImGui::DataTypeFormatString(value_buf, IM_ARRAYSIZE(value_buf), type, v, fmt);
        const float vw = TextSize(g_regular, kSmallSize, value_buf).x;
        const ImRect value_bb(ImVec2(r.bb.Max.x - vw - 6.0f, label_cy - 10.0f), ImVec2(r.bb.Max.x + 6.0f, label_cy + 10.0f));

        bool changed = false;

        // Click the value to type an exact number (unclamped, like ImGui's own slider input).
        ImGui::PushID(label);
        const ImGuiID vid = w->GetID("##value");
        ImGui::ItemAdd(value_bb, vid);
        const bool value_hovered = ImGui::ItemHoverable(value_bb, vid, ImGuiItemFlags_None);
        bool editing = ImGui::TempInputIsActive(vid);
        if (!editing && value_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            editing = true;
        if (value_hovered && !editing)
            ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
        if (editing)
        {
            const ImRect edit_bb(ImVec2(ImMin(value_bb.Min.x, r.bb.Max.x - 84.0f), value_bb.Min.y), value_bb.Max);
            const ImVec2 cursor = w->DC.CursorPos, cursor_prev = w->DC.CursorPosPrevLine;
            const ImVec2 prev_size = w->DC.PrevLineSize;
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 2.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
            ImGui::PushFont(g_regular, kSmallSize);
            changed |= ImGui::TempInputScalar(edit_bb, vid, "##value", type, v, fmt, nullptr, nullptr);
            ImGui::PopFont();
            ImGui::PopStyleVar(2);
            // TempInputScalar lays itself out at edit_bb; put the cursor back below the row.
            w->DC.CursorPos = cursor;
            w->DC.CursorPosPrevLine = cursor_prev;
            w->DC.PrevLineSize = prev_size;
        }
        ImGui::PopID();

        // Track geometry at rest: icons sit at the ends of the row, the bar between them.
        constexpr float icon_w = 14.0f, gap = 12.0f;
        const float x0 = r.bb.Min.x + icon_w + gap;
        const float x1 = r.bb.Max.x - icon_w - gap;
        const float width = ImMax(1.0f, x1 - x0);
        const ImRect hit(ImVec2(r.bb.Min.x, bar_cy - 13.0f), ImVec2(r.bb.Max.x, bar_cy + 13.0f));

        ImGui::ItemAdd(hit, r.id);
        const bool hovered = ImGui::ItemHoverable(hit, r.id, ImGuiItemFlags_None);
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            ImGui::SetActiveID(r.id, w);
            ImGui::SetFocusID(r.id, w);
            ImGui::FocusWindow(w);
        }
        if (hovered || g.ActiveId == r.id)
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

        ElasticState& s = ElasticStore()[r.id];
        const float dt = ImMin(io.DeltaTime, 0.05f);
        const double lo = ReadValue(type, vmin), hi = ReadValue(type, vmax);

        if (g.ActiveId == r.id)
        {
            if (io.MouseDown[0])
            {
                const float mx = io.MousePos.x;
                const double t = ImClamp((double)(mx - x0) / width, 0.0, 1.0);
                if (WriteValue(type, v, lo + t * (hi - lo)))
                {
                    changed = true;
                    ImGui::MarkItemEdited(r.id);
                }

                s.client_x = mx;
                const int region = mx < x0 ? -1 : (mx > x1 ? 1 : 0);
                if (region != s.region)
                {
                    if (region != 0)
                        s.pop[region < 0 ? 0 : 1] = 0.0f;
                    s.region = region;
                }
                s.overflow = Decay(region < 0 ? x0 - mx : (region > 0 ? mx - x1 : 0.0f), kMaxOverflow);
                s.velocity = 0.0f;
            }
            else
            {
                ImGui::ClearActiveID();
            }
        }
        if (g.ActiveId != r.id && (s.overflow != 0.0f || s.velocity != 0.0f))
        {
            // Spring back to rest with a soft, slow bounce (framer-motion bounce 0.5, eased down).
            constexpr float omega = 7.5f, zeta = 0.55f;
            for (float left = dt; left > 0.0f; left -= 1.0f / 240.0f)
            {
                const float step = ImMin(left, 1.0f / 240.0f);
                s.velocity += (-omega * omega * s.overflow - 2.0f * zeta * omega * s.velocity) * step;
                s.overflow += s.velocity * step;
            }
            if (fabsf(s.overflow) < 0.05f && fabsf(s.velocity) < 0.5f)
                s.overflow = s.velocity = 0.0f;
        }

        s.hover = Spring(s.hover, s.hover_v, (hovered || g.ActiveId == r.id) ? 1.0f : 0.0f);
        for (float& p : s.pop)
            p = ImMin(1.0f, p + dt / 0.45f);

        // Draw: the whole slider fades 0.7 -> 1 with hover, the bar grows 6 -> 12 px.
        const float opacity = ImLerp(0.7f, 1.0f, s.hover);
        const float bar_h = ImLerp(6.0f, 12.0f, s.hover) * ImLerp(1.0f, 0.8f, ImClamp(fabsf(s.overflow) / kMaxOverflow, 0.0f, 1.0f));
        const float scale_x = 1.0f + s.overflow / width;
        const bool anchor_right = s.client_x < x0 + width * 0.5f;
        const float bx0 = anchor_right ? x1 - width * scale_x : x0;
        const float bx1 = anchor_right ? x1 : x0 + width * scale_x;
        const float target_frac = (float)ImClamp(hi > lo ? (ReadValue(type, v) - lo) / (hi - lo) : 0.0, 0.0, 1.0);
        if (s.shown < 0.0f)
            s.shown = target_frac;
        // Shorter while dragging so the fill stays under the cursor, longer for clicks and typed values.
        s.shown = Spring(s.shown, s.shown_v, target_frac, g.ActiveId == r.id ? 0.06f : kSmoothTime);
        const float frac = s.shown;

        dl->AddRectFilled(ImVec2(bx0, bar_cy - bar_h * 0.5f), ImVec2(bx1, bar_cy + bar_h * 0.5f),
            C(Fade(IM_COL32(255, 255, 255, 40), opacity)), bar_h * 0.5f);
        if (frac > 0.0f)
            dl->AddRectFilled(ImVec2(bx0, bar_cy - bar_h * 0.5f), ImVec2(bx0 + (bx1 - bx0) * frac, bar_cy + bar_h * 0.5f),
                C(Fade(col::accent, opacity)), bar_h * 0.5f);

        const ImU32 icon_col = C(Fade(IM_COL32(221, 221, 221, 255), opacity));
        DrawIcon(dl, Icon::Minus, ImVec2(r.bb.Min.x + icon_w * 0.5f - (s.region < 0 ? s.overflow : 0.0f), bar_cy), icon_col, 0.8f * PopScale(s.pop[0]));
        DrawIcon(dl, Icon::Plus, ImVec2(r.bb.Max.x - icon_w * 0.5f + (s.region > 0 ? s.overflow : 0.0f), bar_cy), icon_col, 0.8f * PopScale(s.pop[1]));

        DrawLabel(dl, g_regular, kLabelSize, ImVec2(r.bb.Min.x, LabelY(label_cy)), value_bb.Min.x - 8.0f,
            LabelColor(hovered || g.ActiveId == r.id), label);
        if (!editing)
        {
            if (value_hovered)
                dl->AddRectFilled(value_bb.Min, value_bb.Max, C(kCtrlHover), 5.0f);
            DrawText(dl, g_regular, kSmallSize, ImVec2(r.bb.Max.x - vw, label_cy - kSmallSize * 0.5f - 1.0f),
                (g.ActiveId == r.id || value_hovered) ? col::text : col::text_dim, value_buf);
        }
        return changed;
    }

    void ScrollIndicator()
    {
        ImGuiWindow* w = ImGui::GetCurrentWindow();
        if (w->ScrollMax.y <= 0.0f)
            return;
        const float h = w->Size.y;
        const float bar = ImMax(28.0f, h * h / (h + w->ScrollMax.y));
        const float y = w->Pos.y + (w->Scroll.y / w->ScrollMax.y) * (h - bar);
        const float x = w->Pos.x + w->Size.x - kGutter * 0.5f;
        w->DrawList->AddRectFilled(ImVec2(x - 1.0f, y), ImVec2(x + 1.0f, y + bar), C(IM_COL32(255, 255, 255, 26)), 1.0f);
    }
}

// ---- setup ------------------------------------------------------------------

void LoadFonts(ImFont* fallback)
{
    ImGuiIO& io = ImGui::GetIO();

    // All UI text is regular Arial (the user's call; no bold anywhere, so the "semibold" role is
    // the same face), rasterized with Arial's own TrueType hinting. Side by side (--preview
    // --fonts), that was the only setting that looked like Windows' Arial: unhinted and light-
    // hinted text came out soft and unevenly spaced, which read as pixely and squished. Coverage
    // is lifted a touch so light text on the dark shell keeps its weight.
    auto load = [&](const char* path, float size) -> ImFont*
    {
        ImFontConfig cfg;
        cfg.FontLoaderFlags = 0;   // the font's own hinting
        cfg.RasterizerMultiply = 1.1f;
        cfg.PixelSnapH = true;
        if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES)
            return nullptr;
        return io.Fonts->AddFontFromFileTTF(path, size, &cfg);
    };

    g_regular = load("C:\\Windows\\Fonts\\arial.ttf", kLabelSize);
    if (!g_regular)
        g_regular = load("C:\\Windows\\Fonts\\segoeui.ttf", kLabelSize);
    if (!g_regular)
        g_regular = fallback;
    g_semibold = g_regular;

    // Original external interface wordmark.
    {
        ImFontConfig cfg;
        cfg.FontDataOwnedByAtlas = false;
        cfg.FontLoaderFlags = ImGuiFreeTypeLoaderFlags_Monochrome | ImGuiFreeTypeLoaderFlags_MonoHinting;
        cfg.PixelSnapH = true;
        g_brand = io.Fonts->AddFontFromMemoryTTF((void*)kMonocraftTtf, (int)sizeof(kMonocraftTtf), kBrandSize, &cfg);
        if (!g_brand)
            g_brand = g_semibold;
    }

    // The ESP's own face: Segoe UI Semibold with light hinting. Small text over a moving game
    // needs crisp stems; the menu's unhinted rendering looked soft and smeared there.
    {
        ImFontConfig cfg;
        cfg.FontLoaderFlags = ImGuiFreeTypeLoaderFlags_LightHinting;
        cfg.RasterizerMultiply = 1.1f;
        cfg.PixelSnapH = true;
        const char* faces[] = { "C:\\Windows\\Fonts\\seguisb.ttf", "C:\\Windows\\Fonts\\segoeui.ttf", "C:\\Windows\\Fonts\\arialbd.ttf" };
        for (const char* face : faces)
            if (!g_esp && GetFileAttributesA(face) != INVALID_FILE_ATTRIBUTES)
                g_esp = io.Fonts->AddFontFromFileTTF(face, 13.0f, &cfg);
        if (!g_esp)
            g_esp = g_regular;
    }

    io.FontDefault = g_regular;
}

ImFont* EspFont() { return g_esp; }

ImFont* Regular()  { return g_regular; }
ImFont* SemiBold() { return g_semibold; }
ImFont* Brand()    { return g_brand; }

// Monocraft with Minecraft's own text shadow: a copy one font pixel down and right (Monocraft's
// pixel is size / 9), in the text colour at a quarter of its brightness. Sizes that are multiples
// of 9 keep every stroke on whole pixels.
void PixelText(ImDrawList* dl, ImVec2 pos, ImU32 col, const char* text, float size)
{
    const float px = floorf(size / 9.0f + 0.5f);
    const ImU32 shadow = IM_COL32(((col >> IM_COL32_R_SHIFT) & 0xFF) / 4, ((col >> IM_COL32_G_SHIFT) & 0xFF) / 4,
        ((col >> IM_COL32_B_SHIFT) & 0xFF) / 4, (col >> IM_COL32_A_SHIFT) & 0xFF);
    DrawText(dl, g_brand, size, ImVec2(pos.x + px, pos.y + px), shadow, text);
    DrawText(dl, g_brand, size, pos, col, text);
}

// Game Programming Gems 4's SmoothDamp: an exact-enough critically damped spring step that
// stays stable at any frame time.
float Spring(float value, float& velocity, float target, float smooth_time)
{
    const float dt = ImClamp(ImGui::GetIO().DeltaTime, 0.0f, 0.1f);
    if (dt <= 0.0f)
        return value;
    const float omega = 2.0f / ImMax(0.0001f, smooth_time);
    const float x = omega * dt;
    const float decay = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
    const float change = value - target;
    const float temp = (velocity + omega * change) * dt;
    velocity = (velocity - omega * temp) * decay;
    float out = target + (change + temp) * decay;
    // Never pass the target.
    if ((target - value > 0.0f) == (out > target))
    {
        out = target;
        velocity = 0.0f;
    }
    if (fabsf(out - target) < 0.0005f && fabsf(velocity) < 0.005f)
    {
        out = target;
        velocity = 0.0f;
    }
    return out;
}

float SpringIn(float seconds, float smooth_time)
{
    const float t = ImMax(0.0f, seconds) * 2.0f / ImMax(0.0001f, smooth_time);
    const float v = 1.0f - (1.0f + t) * expf(-t);
    return v > 0.999f ? 1.0f : v;
}

// 0 -> 1 on the house spring from the first frame `id` is asked for after a frame without it.
float Appear(ImGuiID id, float delay, float smooth_time)
{
    struct Seen { int frame; double start; };
    static std::unordered_map<ImGuiID, Seen> seen;
    const int frame = ImGui::GetFrameCount();
    const double now = ImGui::GetTime();
    Seen& s = seen.try_emplace(id, Seen{ frame, now }).first->second;
    if (s.frame < frame - 1)
        s.start = now;
    s.frame = frame;
    return SpringIn((float)(now - s.start) - delay, smooth_time);
}

void ApplyStyle()
{
    ImGuiStyle& s = ImGui::GetStyle();

    s.WindowRounding = 14.0f;
    s.ChildRounding = 8.0f;
    s.FrameRounding = 6.0f;
    s.PopupRounding = 9.0f;
    s.GrabRounding = 4.0f;
    s.ScrollbarRounding = 4.0f;
    s.TabRounding = 6.0f;

    s.WindowBorderSize = 0.0f;
    s.ChildBorderSize = 0.0f;   // also disables ImAdd's three-line "top decoration" on its panels
    s.FrameBorderSize = 0.0f;
    s.PopupBorderSize = 1.0f;

    s.WindowPadding = ImVec2(12.0f, 12.0f);
    s.ChildPadding = ImVec2(10.0f, 10.0f);
    s.FramePadding = ImVec2(8.0f, 6.0f);
    s.CellPadding = ImVec2(4.0f, 4.0f);
    s.ItemSpacing = ImVec2(8.0f, 8.0f);
    s.ItemInnerSpacing = ImVec2(6.0f, 6.0f);
    s.WindowMinSize = ImVec2(0.0f, 0.0f);
    s.ScrollbarSize = 6.0f;
    s.GrabMinSize = 10.0f;

    ImVec4* c = s.Colors;
    const ImVec4 white(0.973f, 0.973f, 0.98f, 1.0f);
    auto w = [](float a) { return ImVec4(1.0f, 1.0f, 1.0f, a); };

    c[ImGuiCol_Text]                 = ImVec4(0.941f, 0.941f, 0.953f, 1.0f);
    c[ImGuiCol_TextDisabled]         = ImVec4(0.941f, 0.941f, 0.953f, 0.42f);
    c[ImGuiCol_WindowBg]             = ImVec4(0.039f, 0.039f, 0.039f, 1.0f);
    c[ImGuiCol_ChildBg]              = w(0.03f);
    c[ImGuiCol_PopupBg]              = ImVec4(0.078f, 0.078f, 0.094f, 1.0f);
    c[ImGuiCol_Border]               = w(0.08f);
    c[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg]              = w(0.045f);
    c[ImGuiCol_FrameBgHovered]       = w(0.075f);
    c[ImGuiCol_FrameBgActive]        = w(0.10f);
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab]        = w(0.12f);
    c[ImGuiCol_ScrollbarGrabHovered] = w(0.20f);
    c[ImGuiCol_ScrollbarGrabActive]  = w(0.28f);
    c[ImGuiCol_CheckMark]            = white;
    c[ImGuiCol_SliderGrab]           = white;
    c[ImGuiCol_SliderGrabActive]     = white;
    c[ImGuiCol_Button]               = w(0.05f);
    c[ImGuiCol_ButtonHovered]        = w(0.09f);
    c[ImGuiCol_ButtonActive]         = w(0.13f);
    c[ImGuiCol_Header]               = w(0.07f);
    c[ImGuiCol_HeaderHovered]        = w(0.10f);
    c[ImGuiCol_HeaderActive]         = w(0.14f);
    c[ImGuiCol_Separator]            = w(0.07f);
    c[ImGuiCol_SeparatorHovered]     = w(0.14f);
    c[ImGuiCol_SeparatorActive]      = w(0.20f);
    c[ImGuiCol_Tab]                  = w(0.03f);
    c[ImGuiCol_TabHovered]           = w(0.07f);
    c[ImGuiCol_TabActive]            = w(0.05f);
    c[ImGuiCol_TextSelectedBg]       = w(0.18f);
    c[ImGuiCol_FrameBgShadow]        = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ButtonShadow]         = ImVec4(0, 0, 0, 0);
}

// ---- icons: Lucide geometry on its 24px grid ---------------------------------

void DrawIcon(ImDrawList* dl, Icon icon, ImVec2 c, ImU32 color, float scale)
{
    const float u = 0.62f * scale;
    const float th = 1.4f * ImMax(0.8f, scale);
    auto P = [&](float x, float y) { return ImVec2(c.x + (x - 12.0f) * u, c.y + (y - 12.0f) * u); };
    auto L = [&](float x1, float y1, float x2, float y2) { dl->AddLine(P(x1, y1), P(x2, y2), color, th); };
    auto Poly = [&](std::initializer_list<ImVec2> pts, bool closed)
    {
        for (const ImVec2& p : pts)
            dl->PathLineTo(P(p.x, p.y));
        dl->PathStroke(color, closed ? ImDrawFlags_Closed : ImDrawFlags_None, th);
    };

    switch (icon)
    {
    case Icon::Crosshair:
        dl->AddCircle(c, 10.0f * u, color, 0, th);
        L(22, 12, 18, 12); L(6, 12, 2, 12); L(12, 6, 12, 2); L(12, 22, 12, 18);
        break;

    case Icon::Target:
        dl->AddCircle(c, 10.0f * u, color, 0, th);
        dl->AddCircle(c, 6.0f * u, color, 0, th);
        dl->AddCircleFilled(c, 1.8f * u + 0.3f, color);
        break;

    case Icon::Users:
        dl->AddCircle(P(9, 7), 4.0f * u, color, 0, th);
        dl->PathLineTo(P(2, 21));
        dl->PathLineTo(P(2, 19));
        dl->PathArcTo(P(6, 19), 4.0f * u, IM_PI, IM_PI * 1.5f);
        dl->PathLineTo(P(12, 15));
        dl->PathArcTo(P(12, 19), 4.0f * u, IM_PI * 1.5f, IM_PI * 2.0f);
        dl->PathLineTo(P(16, 21));
        dl->PathStroke(color, ImDrawFlags_None, th);
        dl->PathArcTo(P(15, 7), 4.0f * u, -1.318f, 1.318f);
        dl->PathStroke(color, ImDrawFlags_None, th);
        dl->PathLineTo(P(22, 21));
        dl->PathArcTo(P(18, 19), 4.0f * u, 0.0f, -1.318f);
        dl->PathStroke(color, ImDrawFlags_None, th);
        break;

    case Icon::User:
        dl->AddCircle(P(12, 7), 4.0f * u, color, 0, th);
        dl->PathLineTo(P(5, 21));
        dl->PathLineTo(P(5, 19));
        dl->PathArcTo(P(9, 19), 4.0f * u, IM_PI, IM_PI * 1.5f);
        dl->PathLineTo(P(15, 15));
        dl->PathArcTo(P(15, 19), 4.0f * u, IM_PI * 1.5f, IM_PI * 2.0f);
        dl->PathLineTo(P(19, 21));
        dl->PathStroke(color, ImDrawFlags_None, th);
        break;

    case Icon::Eye:
        // Lucide's two r=10.75 arcs between (2.06, 12) and (21.94, 12): the lids reach y=5 and
        // y=19 (quadratic curves through (12, 2) flattened it to 7..17).
        dl->PathArcTo(P(12, 15.75f), 10.75f * u, -2.7503f, -0.3913f);
        dl->PathArcTo(P(12, 8.25f), 10.75f * u, 0.3913f, 2.7503f);
        dl->PathStroke(color, ImDrawFlags_Closed, th);
        dl->AddCircle(c, 3.0f * u, color, 0, th);
        break;

    case Icon::Globe:
        dl->AddCircle(c, 10.0f * u, color, 0, th);
        L(2, 12, 22, 12);
        dl->AddEllipse(c, ImVec2(4.6f * u, 10.0f * u), color, 0.0f, 0, th);
        break;

    case Icon::Zap:
        Poly({ { 4, 14 }, { 13.5f, 2.5f }, { 11.5f, 10 }, { 20, 10 }, { 10.5f, 21.5f }, { 12.5f, 14 } }, true);
        break;

    case Icon::Sliders:
        L(21, 4, 14, 4); L(10, 4, 3, 4);
        L(21, 12, 12, 12); L(8, 12, 3, 12);
        L(21, 20, 16, 20); L(12, 20, 3, 20);
        L(14, 2, 14, 6); L(8, 10, 8, 14); L(16, 18, 16, 22);
        break;

    case Icon::Save:
        Poly({ { 5, 3 }, { 15.2f, 3 }, { 21, 8.8f }, { 21, 19 }, { 19, 21 }, { 5, 21 }, { 3, 19 }, { 3, 5 } }, true);
        Poly({ { 7, 21 }, { 7, 14 }, { 17, 14 }, { 17, 21 } }, false);
        Poly({ { 7, 3 }, { 7, 8 }, { 15, 8 } }, false);
        break;

    case Icon::Layers:
        Poly({ { 12, 2 }, { 22, 7 }, { 12, 12 }, { 2, 7 } }, true);
        Poly({ { 2, 12 }, { 12, 17 }, { 22, 12 } }, false);
        Poly({ { 2, 17 }, { 12, 22 }, { 22, 17 } }, false);
        break;

    case Icon::List:
        L(8, 6, 21, 6); L(8, 12, 21, 12); L(8, 18, 21, 18);
        dl->AddCircleFilled(P(3.5f, 6), 1.1f * u + 0.4f, color);
        dl->AddCircleFilled(P(3.5f, 12), 1.1f * u + 0.4f, color);
        dl->AddCircleFilled(P(3.5f, 18), 1.1f * u + 0.4f, color);
        break;

    case Icon::Close:
        // Drawn heavier than the line icons, so the X reads as a solid, confident button.
    {
        // Bolder than the line icons. Each arm is one capsule shape (a line plus end circles
        // double-covered at the ends and left knobs), with the arms long next to the stroke.
        const float w = th * 1.5f;
        auto arm = [&](ImVec2 a, ImVec2 b)
        {
            const float ang = atan2f(b.y - a.y, b.x - a.x);
            dl->PathArcTo(a, w * 0.5f, ang + IM_PI * 0.5f, ang + IM_PI * 1.5f, 10);
            dl->PathArcTo(b, w * 0.5f, ang - IM_PI * 0.5f, ang + IM_PI * 0.5f, 10);
            dl->PathFillConvex(color);
        };
        arm(P(19.0f, 5.0f), P(5.0f, 19.0f));
        arm(P(5.0f, 5.0f), P(19.0f, 19.0f));
        break;
    }

    case Icon::Check:
        Poly({ { 20, 6 }, { 9, 17 }, { 4, 12 } }, false);
        break;

    case Icon::ChevronDown:
        Poly({ { 6, 9 }, { 12, 15 }, { 18, 9 } }, false);
        break;

    case Icon::Minus:
        L(5, 12, 19, 12);
        break;

    case Icon::Plus:
        L(5, 12, 19, 12); L(12, 5, 12, 19);
        break;

    case Icon::Activity:
        Poly({ { 22, 12 }, { 18, 12 }, { 15, 21 }, { 9, 3 }, { 6, 12 }, { 2, 12 } }, false);
        break;

    case Icon::Move:
        Poly({ { 5, 9 }, { 2, 12 }, { 5, 15 } }, false);
        Poly({ { 9, 5 }, { 12, 2 }, { 15, 5 } }, false);
        Poly({ { 15, 19 }, { 12, 22 }, { 9, 19 } }, false);
        Poly({ { 19, 9 }, { 22, 12 }, { 19, 15 } }, false);
        L(2, 12, 22, 12); L(12, 2, 12, 22);
        break;

    case Icon::Server:
        dl->AddRect(P(2, 2), P(22, 10), color, 2.0f * u, 0, th);
        dl->AddRect(P(2, 14), P(22, 22), color, 2.0f * u, 0, th);
        dl->AddCircleFilled(P(6, 6), th * 0.65f, color);    // Lucide's round-capped dots
        dl->AddCircleFilled(P(6, 18), th * 0.65f, color);
        break;
    }
}

bool NavItem(const char* label, Icon icon, bool selected, ImVec2 min, ImVec2 max, bool own_highlight)
{
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    ImDrawList* dl = w->DrawList;
    const ImRect bb(min, max);
    const ImGuiID id = w->GetID(label);
    ImGui::ItemAdd(bb, id);
    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);

    const float ts = Anim(id, selected ? 1.0f : 0.0f);
    const float th = Anim(Sub(id, "hover"), hovered ? 1.0f : 0.0f);
    const float a = 8.0f * th * (1.0f - ts);
    if (a > 0.0f)
        dl->AddRectFilled(bb.Min, bb.Max, C(IM_COL32(255, 255, 255, (int)a)), 8.0f);
    if (own_highlight && ts > 0.002f)
    {
        // The selection is a Glass Surface pane, faded in through the style alpha.
        const float alpha = GImGui->Style.Alpha;
        GImGui->Style.Alpha = alpha * ts;
        veil::Glass(dl, bb.Min, bb.Max, 8.0f, 0.35f, kGlassBend);
        GImGui->Style.Alpha = alpha;
    }

    const ImU32 fg = Mix(Mix(IM_COL32(240, 240, 243, 178), col::white, th), col::white, ts);
    const float cy = bb.GetCenter().y;
    DrawIcon(dl, icon, ImVec2(bb.Min.x + 21.0f, cy), C(Mix(fg, col::accent, ts)), 0.95f);   // the open page's icon lights up
    DrawText(dl, ts > 0.5f ? g_semibold : g_regular, kLabelSize, ImVec2(bb.Min.x + 42.0f, LabelY(cy)), fg, label);
    return pressed;
}

// ---- layout --------------------------------------------------------------------

namespace
{
    // The wheel moves a target and the view glides after it on the spring (ImGui's own wheel
    // scrolling is off for these children, so it can't jump the view first).
    constexpr ImGuiWindowFlags kScrollAreaFlags = ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    void SmoothScroll()
    {
        struct Glide { float target, shown, velocity; };
        static std::unordered_map<ImGuiID, Glide> glides;
        ImGuiWindow* w = ImGui::GetCurrentWindow();
        Glide& s = glides.try_emplace(w->ID, Glide{ w->Scroll.y, w->Scroll.y, 0.0f }).first->second;

        // Something else moved the view (a popup scrolling itself into view, a page reset):
        // start from where it is instead of pulling it back.
        if (fabsf(w->Scroll.y - s.shown) > 1.0f && s.velocity == 0.0f)
            s.target = s.shown = w->Scroll.y;

        const ImGuiIO& io = ImGui::GetIO();
        if (io.MouseWheel != 0.0f && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
            s.target -= io.MouseWheel * 110.0f;
        s.target = ImClamp(s.target, 0.0f, w->ScrollMax.y);
        s.shown = Spring(s.shown, s.velocity, s.target, 0.15f);
        if (fabsf(s.shown - w->Scroll.y) > 0.01f)
            ImGui::SetScrollY(s.shown);
    }
}

void BeginColumns(const char* id)
{
    ImGui::PushID(id);
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    g_col_w = floorf((avail.x - (kColGap - kGutter)) * 0.5f);
    g_col_h = avail.y;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::BeginChild("##left", ImVec2(g_col_w, g_col_h), ImGuiChildFlags_None, kScrollAreaFlags);
    SmoothScroll();
}

void NextColumn()
{
    ScrollIndicator();
    ImGui::EndChild();
    ImGui::SameLine(0.0f, kColGap - kGutter);
    ImGui::BeginChild("##right", ImVec2(g_col_w, g_col_h), ImGuiChildFlags_None, kScrollAreaFlags);
    SmoothScroll();
}

void EndColumns()
{
    ScrollIndicator();
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopID();
}

void BeginPage(const char* id)
{
    ImGui::PushID(id);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::BeginChild("##page", ImGui::GetContentRegionAvail(), ImGuiChildFlags_None, kScrollAreaFlags);
    SmoothScroll();
}

void EndPage()
{
    ScrollIndicator();
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopID();
}

namespace
{
    // React Bits Spotlight Card, dark theme defaults: a white light of radius 240 at 15% with
    // softness 0.7 and a 0.6 border glow; it wakes within 80px of the card, follows with 0.3
    // smoothing, and flares (1.2x size, 1.7x strength) while the card is pressed.
    // Intensity and border are a fifth of the component's 0.15 / 0.6: the user found its hover
    // glow too strong, twice.
    constexpr float kSpotSize      = 240.0f;
    constexpr float kSpotIntensity = 0.03f;
    constexpr float kSpotSoftness  = 0.7f;
    constexpr float kSpotBorder    = 0.1f;
    constexpr float kSpotProximity = 80.0f;
    constexpr float kSpotSmoothing = 0.3f;

    struct SpotState { float x = 0.0f, y = 0.0f, presence = 0.0f, flare = 0.0f; };

    std::unordered_map<ImGuiID, SpotState>& SpotStore()
    {
        static std::unordered_map<ImGuiID, SpotState> states;
        return states;
    }

    // The radial-gradient stops SpotlightCard.jsx builds in falloff(), as a function of r/size.
    float Falloff(float t, float softness)
    {
        if (t >= 1.0f)
            return 0.0f;
        const float core = powf(1.0f - softness, 1.5f) * 0.85f;
        if (t <= core)
            return 1.0f;
        const float g = (t - core) / (1.0f - core);
        const float smooth = 1.0f - g * g * (3.0f - 2.0f * g);
        const float glow = expf(-4.6f * g * g) * (1.0f - powf(g, 6.0f));
        return smooth + (glow - smooth) * softness;
    }

    SpotState& UpdateSpotlight(ImGuiID id, const ImRect& card)
    {
        SpotState& s = SpotStore()[id];
        const ImGuiIO& io = ImGui::GetIO();
        const float dt = ImClamp(io.DeltaTime, 0.001f, 0.05f);

        float tx = s.x, ty = s.y, target = 0.0f;
        bool over = false;
        if (ImGui::IsMousePosValid())
        {
            tx = io.MousePos.x - card.Min.x;
            ty = io.MousePos.y - card.Min.y;
            const float gx = ImMax(0.0f, ImMax(-tx, tx - card.GetWidth()));
            const float gy = ImMax(0.0f, ImMax(-ty, ty - card.GetHeight()));
            const float gap = sqrtf(gx * gx + gy * gy);
            over = gap == 0.0f;
            target = over ? 1.0f : ImMax(0.0f, 1.0f - gap / kSpotProximity);
            target *= over ? 1.0f : target;
        }
        if (s.presence < 0.02f && target > 0.0f)
        {
            s.x = tx;
            s.y = ty;
        }
        const float follow = 1.0f - expf(-dt / (0.04f + kSpotSmoothing * 0.4f));
        s.x += (tx - s.x) * follow;
        s.y += (ty - s.y) * follow;
        s.presence += (target - s.presence) * (1.0f - expf(-dt / (target > s.presence ? 0.22f : 0.5f)));
        if (fabsf(target - s.presence) < 0.002f)
            s.presence = target;

        const bool pressed = io.MouseDown[0] && card.Contains(io.MouseClickedPos[0]);
        const float press = pressed ? 1.0f : 0.0f;
        s.flare += (press - s.flare) * (1.0f - expf(-dt / (pressed ? 0.12f : 0.45f)));
        if (fabsf(press - s.flare) < 0.002f)
            s.flare = press;
        return s;
    }

    // Outline of a rounded rect, subdivided so no segment is longer than `step`.
    void RoundedOutline(ImDrawList* dl, const ImRect& r, float rounding, float step, ImVector<ImVec2>& out)
    {
        dl->PathRect(r.Min, r.Max, rounding);
        ImVector<ImVec2> path;
        path.swap(dl->_Path);
        dl->PathClear();
        out.resize(0);
        for (int i = 0; i < path.Size; i++)
        {
            const ImVec2 a = path[i], b = path[(i + 1) % path.Size];
            const int n = ImMax(1, (int)ceilf(sqrtf(ImLengthSqr(b - a)) / step));
            for (int k = 0; k < n; k++)
                out.push_back(ImLerp(a, b, (float)k / n));
        }
    }

    void DrawSpotlight(ImDrawList* dl, const ImRect& card, const SpotState& s)
    {
        const float boost = 1.0f + 0.7f * s.flare;
        const float size = kSpotSize * (1.0f + 0.2f * s.flare);
        const float fill = ImMin(1.0f, s.presence * kSpotIntensity * boost) * GImGui->Style.Alpha;
        const float edge = ImMin(1.0f, s.presence * kSpotBorder * boost) * GImGui->Style.Alpha;
        const ImVec2 light(card.Min.x + s.x, card.Min.y + s.y);
        auto color = [&](ImVec2 p, float strength, float radius, float softness)
        {
            const float a = strength * Falloff(sqrtf(ImLengthSqr(p - light)) / radius, softness);
            return IM_COL32(255, 255, 255, (int)(a * 255.0f + 0.5f));
        };

        // Fill: a fan from the card's centre out to its rounded outline, so the light is clipped
        // to the card's real shape (a clip rect would leak at the corners). Rings give the falloff
        // enough vertices to stay smooth.
        static ImVector<ImVec2> outline;
        RoundedOutline(dl, card, M().card_round, 18.0f, outline);
        const int n = outline.Size;
        constexpr int rings = 12;
        const ImVec2 c = card.GetCenter();
        const ImVec2 uv = dl->_Data->TexUvWhitePixel;
        dl->PrimReserve(n * rings * 6, 1 + n * rings);
        const unsigned int base = dl->_VtxCurrentIdx;
        dl->PrimWriteVtx(c, uv, color(c, fill, size, kSpotSoftness));
        for (int k = 0; k < n; k++)
            for (int j = 1; j <= rings; j++)
            {
                const ImVec2 p = ImLerp(c, outline[k], (float)j / rings);
                dl->PrimWriteVtx(p, uv, color(p, fill, size, kSpotSoftness));
            }
        auto V = [&](int k, int j) { return (ImDrawIdx)(j == 0 ? base : base + 1 + (k % n) * rings + (j - 1)); };
        for (int k = 0; k < n; k++)
            for (int j = 0; j < rings; j++)
            {
                dl->PrimWriteIdx(V(k, j)); dl->PrimWriteIdx(V(k, j + 1)); dl->PrimWriteIdx(V(k + 1, j + 1));
                dl->PrimWriteIdx(V(k, j)); dl->PrimWriteIdx(V(k + 1, j + 1)); dl->PrimWriteIdx(V(k + 1, j));
            }

        // Border glow: the rim lights up near the cursor (softness 1, 0.6x the radius).
        RoundedOutline(dl, ImRect(card.Min + ImVec2(0.5f, 0.5f), card.Max - ImVec2(0.5f, 0.5f)), M().card_round, 6.0f, outline);
        for (int k = 0; k < outline.Size; k++)
        {
            const ImVec2 a = outline[k], b = outline[(k + 1) % outline.Size];
            const ImU32 col = color((a + b) * 0.5f, edge, size * 0.6f, 1.0f);
            if ((col >> IM_COL32_A_SHIFT) & 0xFF)
                dl->AddLine(a, b, col, 1.0f);
        }
    }

    // Non-row blocks (buttons, inputs, list items, text) get breathing room when they open a card,
    // and a divider with a gap when they follow a row, so they read as part of the list instead of
    // hanging off its bottom.
    void BeforeBlock()
    {
        if (!g_cards.empty())
        {
            ImGuiWindow* w = ImGui::GetCurrentWindow();
            const bool sub_first = SubFirst();
            if (g_cards.back().items++ == 0)
                ImGui::ItemSize(ImVec2(0.0f, M().block_gap));
            else if (g_last_was_row && !sub_first)
            {
                RowLine(w, w->DC.CursorPos.x, w->DC.CursorPos.y);
                ImGui::ItemSize(ImVec2(0.0f, 10.0f));
            }
        }
        g_last_was_row = false;
    }
}

void BeginCard(const char* title)
{
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    if (title && *title)
    {
        // Section label in the Minecraft face, upper case, with Minecraft's text shadow.
        char up[64];
        size_t n = 0;
        for (; title[n] && n < sizeof(up) - 1; n++)
            up[n] = (char)toupper((unsigned char)title[n]);
        up[n] = '\0';
        PixelText(w->DrawList, ImVec2(w->DC.CursorPos.x + 2.0f, w->DC.CursorPos.y + 2.0f), IM_COL32(245, 239, 230, 255), up, kSectionSize);
        ImGui::ItemSize(ImVec2(0.0f, M().title_h));
    }
    g_cards.push_back({ w->DC.CursorPos, ImGui::GetContentRegionAvail().x - kGutter, w->GetID(title), 0, 0 });
    g_last_was_row = false;

    // Content goes on channel 1 so the card background (drawn in EndCard, once the
    // height is known) can sit underneath it on channel 0.
    w->DrawList->ChannelsSplit(2);
    w->DrawList->ChannelsSetCurrent(1);
    ImGui::PushID(title);
    ImGui::BeginGroup();
    ImGui::Indent(M().card_pad);
    ImGui::ItemSize(ImVec2(0.0f, M().card_pad_y));
}

void EndCard()
{
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    ImGui::ItemSize(ImVec2(0.0f, g_last_was_row ? M().card_pad_y : M().block_gap + 4.0f));
    ImGui::Unindent(M().card_pad);
    ImGui::EndGroup();
    ImGui::PopID();

    const CardState s = g_cards.back();
    g_cards.pop_back();
    const ImRect r(s.start, ImVec2(s.start.x + s.width, ImGui::GetItemRectMax().y));
    ImDrawList* dl = w->DrawList;
    dl->ChannelsSetCurrent(0);
    // React Bits Glass Surface: refracts the field at its rim, frosted with backgroundOpacity,
    // with the component's inner rims in faint amber (veil.cpp).
    veil::Glass(dl, r.Min, r.Max, M().card_round, kCardFrost, kGlassBend);
    // Recessed panels behind the sub-blocks (settings that depend on the row above them).
    for (const ImVec2& sub : s.subs)
    {
        const ImVec2 a(r.Min.x + 8.0f, sub.x), b(r.Max.x - 8.0f, sub.y);
        dl->AddRectFilled(a, b, C(IM_COL32(0, 0, 0, 70)), 9.0f);
        dl->AddRect(a, b, C(IM_COL32(255, 225, 190, 14)), 9.0f);
    }
    const SpotState& spot = UpdateSpotlight(s.id, r);
    if (spot.presence > 0.002f)
        DrawSpotlight(dl, r, spot);
    dl->ChannelsMerge();

    ImGui::ItemSize(ImVec2(0.0f, M().card_gap));
}

void Spotlight(ImDrawList* dl, ImGuiID id, ImVec2 min, ImVec2 max)
{
    const ImRect r(min, max);
    const SpotState& spot = UpdateSpotlight(id, r);
    if (spot.presence > 0.002f)
        DrawSpotlight(dl, r, spot);
}

void BeginSub()
{
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    ImGui::PushOverrideID(g_last_row_id);
    g_subs.push_back({ w->DC.CursorPos.y + 3.0f, 0 });
    ImGui::ItemSize(ImVec2(0.0f, 6.0f));
    ImGui::Indent(M().sub_indent);
}

void EndSub()
{
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    const SubState s = g_subs.back();
    g_subs.pop_back();
    ImGui::Unindent(M().sub_indent);
    ImGui::PopID();
    if (s.items == 0)
    {
        // Nothing was put in it (a status line with nothing to say): no panel, and the room
        // BeginSub took goes back, so the rows close up as if the block weren't there.
        w->DC.CursorPos.y = s.start_y - 3.0f;
        w->DC.CursorMaxPos.y = ImMin(w->DC.CursorMaxPos.y, w->DC.CursorPos.y);
        return;
    }
    ImGui::ItemSize(ImVec2(0.0f, 6.0f));
    if (!g_cards.empty())
        g_cards.back().subs.push_back(ImVec2(s.start_y, w->DC.CursorPos.y - 3.0f));
    g_last_was_row = false;   // the panel closes the group: what follows starts a fresh block
}

void Spacing(float h)
{
    ImGui::ItemSize(ImVec2(0.0f, h));
}

void AlignRight(float width)
{
    BeforeBlock();   // the divider before the block starts at the row's left, before the shift
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    w->DC.CursorPos.x += ImMax(0.0f, RowWidth() - width);
}

void SetCompact(bool compact)
{
    g_metrics = compact ? &kCompact : &kRoomy;
}

float RowWidth()
{
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    return RightEdge(w) - w->DC.CursorPos.x;
}

// ---- rows ----------------------------------------------------------------------

bool Toggle(const char* label, bool* v)
{
    return ToggleImpl(label, v, nullptr, nullptr, nullptr, nullptr);
}

bool ToggleKey(const char* label, bool* v, int* key, int* key_mode)
{
    return ToggleImpl(label, v, key, key_mode, nullptr, nullptr);
}

bool ToggleColor(const char* label, bool* v, float* color, float* color2)
{
    return ToggleImpl(label, v, nullptr, nullptr, color, color2);
}

bool ToggleKeyColor(const char* label, bool* v, int* key, int* key_mode, float* color, float* color2)
{
    return ToggleImpl(label, v, key, key_mode, color, color2);
}

bool Slider(const char* label, float* v, float min, float max, const char* fmt)
{
    return SliderImpl(label, ImGuiDataType_Float, v, &min, &max, fmt);
}

bool SliderInt(const char* label, int* v, int min, int max, const char* fmt)
{
    return SliderImpl(label, ImGuiDataType_S32, v, &min, &max, fmt);
}

bool Combo(const char* label, int* v, std::initializer_list<const char*> items)
{
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    if (w->SkipItems)
        return false;

    const Row r = MakeRow(label, M().row);
    ImDrawList* dl = w->DrawList;
    const float cy = r.bb.GetCenter().y;
    const int count = (int)items.size();
    const char* const* list = items.begin();

    const float fw = ImMin(150.0f, floorf(r.bb.GetWidth() * 0.52f));
    const ImRect frame(ImVec2(r.bb.Max.x - fw, cy - 12.0f), ImVec2(r.bb.Max.x, cy + 12.0f));   // the key pill's height: one rail, one cap height
    ImGui::ItemAdd(frame, r.id);
    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior(frame, r.id, &hovered, &held);
    const ImGuiID popup = Sub(r.id, "##popup");
    bool open = ImGui::IsPopupOpen(popup, ImGuiPopupFlags_None);
    if (pressed && !open)
    {
        ImGui::OpenPopupEx(popup);
        open = true;
    }

    const float h = Anim(r.id, (hovered || open) ? 1.0f : 0.0f);
    dl->AddRectFilled(frame.Min, frame.Max, C(Mix(IM_COL32(255, 255, 255, 16), IM_COL32(255, 255, 255, 26), h)), 7.0f);
    dl->AddRect(frame.Min, frame.Max, C(open ? IM_COL32(255, 255, 255, 70) : Mix(IM_COL32(255, 255, 255, 24), IM_COL32(255, 255, 255, 40), h)), 7.0f);
    const char* preview = (*v >= 0 && *v < count) ? list[*v] : "-";
    DrawLabel(dl, g_regular, kSmallSize, ImVec2(frame.Min.x + 10.0f, cy - (kSmallSize) * 0.5f - 1.0f), frame.Max.x - 26.0f, col::text, preview);
    DrawIcon(dl, Icon::ChevronDown, ImVec2(frame.Max.x - 13.0f, cy + 0.5f), C(col::text_dim), 0.65f);
    DrawLabel(dl, g_regular, kLabelSize, ImVec2(r.bb.Min.x, LabelY(cy)), frame.Min.x - 10.0f, LabelColor(false), label);

    bool changed = false;
    if (PopupBegin(popup, fw, ImVec2(frame.Min.x, frame.Max.y + 4.0f), ImVec2(0.0f, 0.0f),
            (kPopupItemH + 2.0f) * 8.0f + 10.0f))
    {
        for (int i = 0; i < count; i++)
        {
            ImGui::PushID(i);
            if (PopupItem(list[i], i == *v))
            {
                *v = i;
                changed = true;
                ImGui::MarkItemEdited(r.id);
            }
            if (i == *v && ImGui::IsWindowAppearing())
                ImGui::SetScrollHereY();
            ImGui::PopID();
        }
        PopupEnd();
    }
    return changed;
}

bool Keybind(const char* label, int* key, int* key_mode)
{
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    if (w->SkipItems)
        return false;

    const Row r = MakeRow(label, M().row);
    const float cy = r.bb.GetCenter().y;
    ImGui::PushID(label);
    const ImGuiID kid = w->GetID("##key");
    const float kw = ImMax(64.0f, KeyWidth(kid, *key));
    const ImRect k(ImVec2(r.bb.Max.x - kw, cy - 12.0f), ImVec2(r.bb.Max.x, cy + 12.0f));
    const bool changed = KeyPill(kid, k, key, key_mode);
    ImGui::PopID();
    DrawLabel(w->DrawList, g_regular, kLabelSize, ImVec2(r.bb.Min.x, LabelY(cy)), k.Min.x - 10.0f, LabelColor(false), label);
    return changed;
}

bool ColorRow(const char* label, float* color)
{
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    if (w->SkipItems)
        return false;

    const Row r = MakeRow(label, M().row);
    const float cy = r.bb.GetCenter().y;
    ImGui::PushID(label);
    const ImRect s(ImVec2(r.bb.Max.x - kSwatch, cy - kSwatch * 0.5f), ImVec2(r.bb.Max.x, cy + kSwatch * 0.5f));
    const bool changed = Swatch("##c", s, color);
    ImGui::PopID();
    DrawLabel(w->DrawList, g_regular, kLabelSize, ImVec2(r.bb.Min.x, LabelY(cy)), s.Min.x - 10.0f, LabelColor(false), label);
    return changed;
}

bool InputText(const char* id, char* buf, size_t size, const char* hint, float width)
{
    BeforeBlock();
    // Padded to a Button's height, so a field and a button can share a row.
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, (M().button_h - ImGui::GetFontSize()) * 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, kCtrlFill);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, kCtrlHover);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, kCtrlHover);
    ImGui::PushStyleColor(ImGuiCol_Border, kCtrlBorder);
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, col::text_faint);
    ImGui::SetNextItemWidth(width > 0.0f ? width : RowWidth());
    const bool changed = ImGui::InputTextWithHint(id, hint, buf, size);
    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar(3);
    return changed;
}

void Text(const char* text, ImU32 color)
{
    BeforeBlock();
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::PushFont(g_regular, kSmallSize);
    ImGui::PushTextWrapPos(RightEdge(w) - w->Pos.x + w->Scroll.x);
    ImGui::ItemSize(ImVec2(0.0f, 3.0f));
    ImGui::TextUnformatted(text);
    ImGui::ItemSize(ImVec2(0.0f, 5.0f));
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
    ImGui::PopStyleColor();
}

void Value(const char* label, const char* value, ImU32 value_color)
{
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    if (w->SkipItems)
        return;
    const Row r = MakeRow(label, M().row - 10.0f);
    const float cy = r.bb.GetCenter().y;
    const ImVec2 vs = TextSize(g_regular, kLabelSize, value);
    DrawText(w->DrawList, g_regular, kLabelSize, ImVec2(r.bb.Max.x - vs.x, LabelY(cy)), value_color, value);
    DrawLabel(w->DrawList, g_regular, kLabelSize, ImVec2(r.bb.Min.x, LabelY(cy)), r.bb.Max.x - vs.x - 12.0f, col::text_dim, label);
}

void Legend(const char* text, ImU32 swatch)
{
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    if (w->SkipItems)
        return;
    const Row r = MakeRow(text, 28.0f);
    const float cy = r.bb.GetCenter().y;
    w->DrawList->AddCircleFilled(ImVec2(r.bb.Min.x + 5.0f, cy), 4.0f, C(swatch));
    DrawText(w->DrawList, g_regular, kSmallSize, ImVec2(r.bb.Min.x + 18.0f, cy - 8.0f), col::text_dim, text);
}

void Empty(const char* text)
{
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    if (w->SkipItems)
        return;
    const Row r = MakeRow(text, M().row + 10.0f);
    const ImVec2 ts = TextSize(g_regular, kSmallSize, text);
    DrawText(w->DrawList, g_regular, kSmallSize, r.bb.GetCenter() - ts * 0.5f - ImVec2(0.0f, 1.0f), col::text_faint, text);
}

bool Button(const char* label, Btn kind, float width)
{
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    if (w->SkipItems)
        return false;
    BeforeBlock();

    const ImGuiID id = w->GetID(label);
    const ImVec2 pos = w->DC.CursorPos;
    const ImRect bb(pos, ImVec2(pos.x + (width > 0.0f ? width : RowWidth()), pos.y + M().button_h));
    ImGui::ItemSize(bb.GetSize());
    if (!ImGui::ItemAdd(bb, id))
        return false;

    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    const float h = Anim(id, hovered ? 1.0f : 0.0f);

    // A faded white slab reads as grey; a disabled primary looks like a dimmed secondary instead.
    if (kind == Btn::Primary && (GImGui->CurrentItemFlags & ImGuiItemFlags_Disabled))
        kind = Btn::Secondary;

    ImU32 fill = 0, border = 0, text = 0, shine = 0;
    switch (kind)
    {
    case Btn::Primary:
        fill = Mix(col::accent, IM_COL32(240, 176, 104, 255), h);
        text = col::accent_ink;
        break;
    case Btn::Secondary:
        fill = Mix(IM_COL32(255, 255, 255, 17), IM_COL32(255, 255, 255, 28), h);
        border = Mix(IM_COL32(255, 255, 255, 24), IM_COL32(255, 255, 255, 44), h);
        shine = IM_COL32(255, 255, 255, 34);
        text = col::white;
        break;
    case Btn::Danger:
        fill = Mix(IM_COL32(255, 96, 96, 20), IM_COL32(255, 96, 96, 38), h);
        border = Mix(IM_COL32(255, 96, 96, 60), IM_COL32(255, 112, 112, 110), h);
        shine = IM_COL32(255, 140, 140, 36);
        text = col::danger;
        break;
    }

    ImDrawList* dl = w->DrawList;
    constexpr float kR = 9.0f;
    if (kind == Btn::Primary)
        dl->AddRectFilled(ImVec2(bb.Min.x, bb.Min.y + 1.0f), ImVec2(bb.Max.x, bb.Max.y + 1.0f), C(IM_COL32(0, 0, 0, 60)), kR);   // a hair of depth
    dl->AddRectFilled(bb.Min, bb.Max, C(fill), kR);
    if (border)
        dl->AddRect(bb.Min, bb.Max, C(border), kR);
    (void)shine;
    if (held)
        dl->AddRectFilled(bb.Min, bb.Max, C(IM_COL32(0, 0, 0, 38)), kR);   // pressed
    const char* end = ImGui::FindRenderedTextEnd(label);
    ImFont* const face = kind == Btn::Primary ? g_semibold : g_regular;   // only the main action is bold
    const ImVec2 ts = TextSize(face, kSmallSize, label, end);
    DrawText(dl, face, kSmallSize, bb.GetCenter() - ts * 0.5f - ImVec2(0.0f, held ? 0.0f : 1.0f), text, label, end);
    return pressed;
}

void DrawAvatar(ImDrawList* dl, ImVec2 c, float r, const AvatarSpec& av)
{
    if (av.tex)
        dl->AddImageRounded(av.tex, ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), ImVec2(0, 0), ImVec2(1, 1), C(IM_COL32_WHITE), r);
    else
    {
        dl->AddCircleFilled(c, r, C(IM_COL32(255, 255, 255, 24)), 0);
        const char letter[2] = { av.initial, '\0' };
        const float size = r * 0.95f;
        const ImVec2 ts = TextSize(g_semibold, size, letter);
        DrawText(dl, g_semibold, size, ImVec2(c.x - ts.x * 0.5f, c.y - size * 0.5f - 1.0f), col::text_dim, letter);
    }
    dl->AddCircle(c, r, C(IM_COL32(255, 255, 255, 28)), 0, 1.0f);   // a hairline edge keeps pale headshots crisp
    if (av.ring)
        dl->AddCircle(c, r + 2.5f, C(av.ring), 0, 1.75f);
}

void ProfileHeader(const AvatarSpec& av, const char* name, const char* handle, float health, const char* health_text,
    const char* chip, ImU32 chip_color)
{
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    if (w->SkipItems)
        return;
    BeforeBlock();

    constexpr float kPortraitW = 120.0f, kPortraitH = 120.0f, kGap = 16.0f, kRound = 12.0f;
    const ImVec2 pos = w->DC.CursorPos;
    const ImRect bb(pos, ImVec2(RightEdge(w), pos.y + kPortraitH));
    ImGui::ItemSize(bb.GetSize());
    ImDrawList* dl = w->DrawList;

    // ---- portrait: the full avatar on a flat backing ----
    const ImRect pr(bb.Min, ImVec2(bb.Min.x + kPortraitW, bb.Min.y + kPortraitH));
    dl->AddRectFilled(pr.Min, pr.Max, C(IM_COL32(24, 24, 29, 255)), kRound);   // solid: nothing shows through
    if (av.body)
    {
        // The render is a square with the figure in the middle: crop to the figure.
        dl->AddImageRounded(av.body, pr.Min, pr.Max, ImVec2(0.11f, 0.10f), ImVec2(0.89f, 0.88f), C(IM_COL32_WHITE), kRound);   // a square crop: no stretch
    }
    else
    {
        DrawAvatar(dl, pr.GetCenter(), 32.0f, AvatarSpec{ av.tex, 0, 0, av.initial });
    }
    dl->AddRect(pr.Min, pr.Max, C(av.ring ? Fade(av.ring, 0.85f) : IM_COL32(255, 255, 255, 22)), kRound, 0, av.ring ? 1.75f : 1.0f);

    // ---- name, handle, status ----
    const float tx = pr.Max.x + kGap;
    DrawLabel(dl, g_semibold, 17.0f, ImVec2(tx, bb.Min.y + 3.0f), bb.Max.x, col::white, name);
    DrawLabel(dl, g_regular, kSmallSize, ImVec2(tx, bb.Min.y + 26.0f), bb.Max.x, col::text_faint, handle);
    if (chip && *chip)
    {
        const ImVec2 cs = TextSize(g_semibold, 12.0f, chip);
        const ImRect pill(ImVec2(tx, bb.Min.y + 52.0f), ImVec2(tx + cs.x + 22.0f, bb.Min.y + 74.0f));
        dl->AddRectFilled(pill.Min, pill.Max, C(Fade(chip_color, 0.14f)), 11.0f);
        dl->AddRect(pill.Min, pill.Max, C(Fade(chip_color, 0.40f)), 11.0f);
        dl->AddCircleFilled(ImVec2(pill.Min.x + 11.0f, pill.GetCenter().y), 2.5f, C(chip_color), 12);
        DrawText(dl, g_semibold, 12.0f, ImVec2(pill.Min.x + 19.0f, pill.Min.y + 4.0f), chip_color, chip);
    }

    // ---- health: label and numbers, then a bar the full width of the column ----
    if (health >= 0.0f)
    {
        health = ImClamp(health, 0.0f, 1.0f);
        const ImVec4 red(1.0f, 0.34f, 0.36f, 1.0f), amber(1.0f, 0.76f, 0.28f, 1.0f), green(0.36f, 0.90f, 0.52f, 1.0f);
        const ImVec4 lo = health < 0.5f ? red : amber, hi = health < 0.5f ? amber : green;
        const float t = health < 0.5f ? health / 0.5f : (health - 0.5f) / 0.5f;
        const ImU32 fill = ImGui::ColorConvertFloat4ToU32(ImLerp(lo, hi, t));

        const float label_y = bb.Min.y + 64.0f;   // close under the names: one block, not two islands
        DrawText(dl, g_regular, 13.0f, ImVec2(tx, label_y), col::text_dim, "Health");
        if (health_text && *health_text)
        {
            const ImVec2 hs = TextSize(g_semibold, 13.0f, health_text);
            DrawText(dl, g_semibold, 13.0f, ImVec2(bb.Max.x - hs.x, label_y), col::white, health_text);
        }
        const ImRect bar(ImVec2(tx, bb.Min.y + 84.0f), ImVec2(bb.Max.x, bb.Min.y + 92.0f));
        dl->AddRectFilled(bar.Min, bar.Max, C(IM_COL32(0, 0, 0, 110)), 4.0f);
        dl->AddRect(bar.Min, bar.Max, C(IM_COL32(255, 255, 255, 18)), 4.0f);
        const float fw = ImMax(8.0f, (bar.GetWidth() - 2.0f) * health);
        if (health > 0.0f)
        {
            const ImVec2 f0(bar.Min.x + 1.0f, bar.Min.y + 1.0f), f1(bar.Min.x + 1.0f + fw, bar.Max.y - 1.0f);
            dl->AddRectFilled(ImVec2(f0.x - 1.0f, f0.y - 1.0f), ImVec2(f1.x + 1.0f, f1.y + 1.0f), C(Fade(fill, 0.22f)), 5.0f);   // glow
            dl->AddRectFilled(f0, f1, C(fill), 3.0f);
            dl->AddRectFilled(f0, ImVec2(f1.x, f0.y + (f1.y - f0.y) * 0.5f), C(IM_COL32(255, 255, 255, 48)), 3.0f, ImDrawFlags_RoundCornersTop);   // sheen
        }
    }
}


bool ListItem(const char* label, bool selected, const char* detail, ImU32 label_color, const AvatarSpec* avatar)
{
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    if (w->SkipItems)
        return false;
    BeforeBlock();

    const ImGuiID id = w->GetID(label);
    const ImVec2 pos = w->DC.CursorPos;
    const ImRect bb(pos, ImVec2(RightEdge(w), pos.y + M().list_h));
    ImGui::ItemSize(bb.GetSize());
    if (!ImGui::ItemAdd(bb, id))
        return false;

    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);
    const float ts = Anim(id, selected ? 1.0f : 0.0f);
    const float th = Anim(Sub(id, "hover"), hovered ? 1.0f : 0.0f);
    const float a = ImMax(9.0f * th, 17.0f * ts);
    ImDrawList* dl = w->DrawList;

    const float enter = ListEnter(id);
    const float alpha = GImGui->Style.Alpha;
    const int vtx0 = dl->VtxBuffer.Size;
    GImGui->Style.Alpha = alpha * enter;

    if (a > 0.0f)
        dl->AddRectFilled(bb.Min - ImVec2(7.0f, 0.0f), bb.Max + ImVec2(7.0f, 0.0f), C(IM_COL32(255, 255, 255, (int)a)), 7.0f);

    const float cy = bb.GetCenter().y;
    float max_x = bb.Max.x;
    if (detail && *detail)
    {
        const ImVec2 ds = TextSize(g_regular, kSmallSize, detail);
        DrawText(dl, g_regular, kSmallSize, ImVec2(bb.Max.x - ds.x, cy - kSmallSize * 0.5f - 1.0f), col::text_faint, detail);
        max_x = bb.Max.x - ds.x - 10.0f;
    }
    float label_x = bb.Min.x;
    if (avatar)
    {
        DrawAvatar(dl, ImVec2(bb.Min.x + 13.0f, cy), 12.0f, *avatar);
        label_x += 34.0f;
    }
    DrawLabel(dl, g_regular, kLabelSize, ImVec2(label_x, LabelY(cy)), max_x, selected ? col::white : label_color, label);

    GImGui->Style.Alpha = alpha;
    if (enter < 1.0f)
        ScaleVerts(dl, vtx0, bb.GetCenter(), 0.7f + 0.3f * enter);
    return pressed;
}

// ---- React Bits Click Spark ------------------------------------------------------
// Defaults: 8 white lines, size 14, radius 30, width 2, ease-out, randomness 0.2; stretched
// from 500 ms to 750 ms to match the menu's slower motion.

namespace
{
    struct Spark { float angle, reach, scale, life; };
    struct Burst { ImVec2 pos; double start; Spark sparks[8]; };

    float Rand01()
    {
        static unsigned int s = 0x9E3779B9u;
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return (float)(s & 0xFFFFFF) / (float)0x1000000;
    }
}

void ClickSparks()
{
    constexpr int   kCount = 8;
    constexpr float kSize = 14.0f, kRadius = 30.0f, kWidth = 2.0f, kDuration = 0.75f, kRandomness = 0.2f;
    static std::vector<Burst> bursts;

    const ImGuiIO& io = ImGui::GetIO();
    const double now = ImGui::GetTime();
    // AllowWhenBlockedByActiveItem: the control just clicked is already the active item this frame.
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)
        && ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
    {
        Burst b{ io.MousePos, now, {} };
        const float heading = -IM_PI * 0.5f, step = IM_PI * 2.0f / kCount;
        for (int i = 0; i < kCount; i++)
        {
            const float jitter = (Rand01() - 0.5f) * kRandomness;
            b.sparks[i] = { heading + step * i + jitter * step * 0.9f,
                            1.0f + (Rand01() - 0.5f) * 0.7f * kRandomness,
                            1.0f + (Rand01() - 0.5f) * 0.8f * kRandomness,
                            1.0f + (Rand01() - 0.5f) * 0.5f * kRandomness };
        }
        bursts.push_back(b);
        if (bursts.size() > 32)
            bursts.erase(bursts.begin());
    }

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    for (size_t i = 0; i < bursts.size();)
    {
        const Burst& b = bursts[i];
        const float elapsed = (float)(now - b.start);
        bool alive = false;
        for (const Spark& s : b.sparks)
        {
            const float p = elapsed / (kDuration * s.life);
            if (p >= 1.0f)
                continue;
            alive = true;
            const float eased = 1.0f - powf(1.0f - p, 3.0f);
            const float distance = kRadius * s.reach * eased;
            const float length = kSize * s.scale * powf(1.0f - p, 1.6f);
            const float t = ImClamp((p - 0.55f) / 0.45f, 0.0f, 1.0f);
            const float fade = 1.0f - t * t * (3.0f - 2.0f * t);
            const ImVec2 dir(cosf(s.angle), sinf(s.angle));
            dl->AddLine(b.pos + dir * distance, b.pos + dir * (distance + ImMax(0.01f, length)),
                IM_COL32(255, 255, 255, (int)(255.0f * fade)), kWidth * s.scale);
        }
        if (alive)
            i++;
        else
            bursts.erase(bursts.begin() + i);
    }
}
}
