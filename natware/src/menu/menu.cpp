#include "menu.h"
#include <Offsets/Offsets.hpp>
#include <Offsets/OffsetLoader.hpp>

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>

#include <imgui/backends/imgui_impl_dx11.h>
#include <imgui/backends/imgui_impl_win32.h>

#include <imgui/misc/imgui_freetype.h>

#include "../../render/textures/texture.h"

#include "../../../ext/imgui/addons/imgui_addons.h"
#include "../../../ext/imgui/texteditor.hpp"

#include <settings.h>
#include <cache/cache.h>
#include <gamesupport/gamesupport.h>
#include <gamesupport/LumberTycoon2/lt2.h>
#include <menu/keybind/keybind.h>
#include <menu/ui/ui.h>
#include <menu/ui/veil.h>
#include <menu/ui/profile.h>
#include <features/chams/engine_chams.h>
#include <features/chams/body_chams.h>
#include <features/rage/rapidfire/rapidfire.h>
#include <features/rage/hbe/hitbox_expander.h>
#include <features/lighting/fog/fog.h>
#include <wallcheck/wallcheck.h>
#include <features/silentaim/silentaim.h>
#include <features/esp/esp_draw.h>
#include <features/esp/rig.h>
#include <features/explorer/explorer.h>
#include <features/explorer/globals.h>
#include <features/config/config.h>
#include <updater/updater.h>
#include <features/aimbot/aim_log.h>
#include <cache/custom_entities/custom_entities.h>
#include <memory/memory.h>
#include <game/game.h>
#include <features/aimbot/aimbot.h>
#include <features/avatarmanager/avatarmanager.h>
#include <render/render.h>
#include "../../render/3d/main_api.hpp"
#include "../../render/3d/texture/texture_cache.hpp"
#include "../../ext/font/config/font_config.h"
#include "../../ext/font/tahoma.h"
#include "../../ext/font/sp7.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <algorithm>
#include <cmath>
#include <cfloat>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

extern ImFont* esp_font;
extern ImFont* esp_font_tahoma;
extern ImFont* esp_font_sp7;
extern ImFont* esp_font_arial;

class AvatarManager;
extern std::unique_ptr<AvatarManager> g_avatar_manager;

namespace helper
{
    void draw_text_outlined(ImDrawList* draw, ImFont* font, float font_size, ImVec2 pos, ImU32 col, const char* text_begin, const char* text_end = nullptr);
    void corner_box(ImDrawList* draw, ImVec2 min, ImVec2 max, ImU32 col, float thickness = 1.f);
}

#define PROJECT_NAME    external_config::cheat_name.c_str()

namespace
{
    enum Page { PageAimbot, PageSilentAim, PageCombatTab, PageMovementTab, PageVisualsTab, PageWorldTab, PageSettingsTab, PageConfigs, PageCount };

    // Preview-only pseudo pages that open the tool windows.
    constexpr int kPreviewPlayerList = 100;
    constexpr int kPreviewExplorer   = 101;

    struct NavEntry { const char* label; ui::Icon icon; bool group_start; };
    // Groups: aiming, the rest of combat and movement, what you see, and the program itself.
    constexpr NavEntry kNav[PageCount] = {
        { "Aimbot",     ui::Icon::Crosshair, true  },
        { "Silent aim", ui::Icon::Target,    false },
        { "Combat",     ui::Icon::Zap,       true  },
        { "Movement",   ui::Icon::Move,      false },
        { "Visuals",    ui::Icon::Eye,       true  },
        { "World",      ui::Icon::Globe,     false },
        { "Settings",   ui::Icon::Sliders,   true  },
        { "Configs",    ui::Icon::Save,      false },
    };

    constexpr float kWindowW  = 820.0f;
    constexpr float kSidebarW = 200.0f;
    constexpr float kShellR   = 16.0f;
    constexpr float kPad      = 16.0f;   // sidebar
    constexpr float kNavH     = 34.0f;
    constexpr float kNavStep  = 36.0f;
    constexpr float kNavGap   = 16.0f;
    constexpr float kGroupGap = 18.0f;   // space between nav groups (no caption)
    constexpr float kFooterH  = 48.0f;
    constexpr float kContentPadX      = 32.0f;
    constexpr float kContentPadTop    = 76.0f;
    constexpr float kContentPadBottom = 18.0f;
    constexpr float kScrollGutter     = 12.0f;   // ui's page gutter; cards stop this far inside the scroll area

    int  s_page = PageAimbot;
    bool s_explorer_open = false;
    bool s_playerlist_open = false;

    const char* const kParts[]   = { "Closest", "Head", "HumanoidRootPart", "LeftArm", "RightArm", "LeftLeg", "RightLeg" };

    // Silent aim doesn't run in these games, so its page is hidden there.
    bool SilentAimAvailable()
    {
        return !game::is_lumber_tycoon_2 && !game::is_phantom_forces && !game::is_murder_mystery_2;
    }

    // Sized to the sidebar (all pages plus both tools) so the window has no dead space; a
    // fixed height also keeps it from jumping when the silent aim page comes and goes.
    float WindowHeight()
    {
        const int gaps = 4;   // between the four page groups, and before the tools
        const int items = PageCount + 2;
        return kPad + 48.0f + items * kNavStep + gaps * kGroupGap + 12.0f + kFooterH + kPad;
    }

    // ---- shared cards ----------------------------------------------------------

    // What the wall check last read of the map, so you can see it working.
    void WallCheckReadout()
    {
        const auto snap = wallcheck->snapshot();
        if (!snap)
        {
            ui::Text("Reading the map...");
            return;
        }
        const auto& st = snap->stats;
        const float age = std::chrono::duration<float>(std::chrono::steady_clock::now() - snap->built_at).count();
        char line[160];
        snprintf(line, sizeof(line), "%d walls from %d parts, read %.0f s ago", st.walls, st.parts, age);
        ui::Text(line);
        if (!st.transparency_trusted || !st.flags_trusted)
            ui::Text("Some part data looked wrong, so every part counts as a wall");
        if (st.budget_hit)
            ui::Text("This place is very large: only part of it was read");
    }

    void StatusLine(const std::string& text)
    {
        if (!text.empty())
            ui::Text(text.c_str());
    }

    struct TriggerbotRefs
    {
        bool* enabled; int* key; int* mode; int* fire_mode;
        float* cps; float* hold; float* reaction;
        bool* max_distance_on; float* max_distance; bool* wallcheck;
    };

    void TriggerbotCard(const TriggerbotRefs& t)
    {
        ui::BeginCard("Triggerbot");
        ui::ToggleKey("Enabled", t.enabled, t.key, t.mode);
        if (*t.enabled)
        {
            ui::BeginSub();
            ui::Combo("Fire mode", t.fire_mode, { "Click", "Hold" });
            if (*t.fire_mode == 0)
                ui::Slider("Clicks per second", t.cps, 1.0f, 30.0f, "%.1f");
            else
                ui::Slider("Hold duration", t.hold, 0.01f, 1.0f, "%.2f s");
            ui::Slider("Reaction time", t.reaction, 0.0f, 500.0f, "%.0f ms");
            ui::Toggle("Max distance", t.max_distance_on);
            if (*t.max_distance_on)
            {
                ui::BeginSub();
                ui::Slider("Distance", t.max_distance, 1.0f, 2000.0f, "%.0f studs");
                ui::EndSub();
            }
            ui::Toggle("Wall check", t.wallcheck);
            if (*t.wallcheck)
            {
                ui::BeginSub();
                WallCheckReadout();
                ui::EndSub();
            }
            ui::EndSub();
        }
        ui::EndCard();
    }

    void PartCombo(const char* label, int* v)
    {
        ui::Combo(label, v, { kParts[0], kParts[1], kParts[2], kParts[3], kParts[4], kParts[5], kParts[6] });
    }

    // ---- pages -------------------------------------------------------------------

    void PageAimbotMain()
    {
        namespace ab = settings::aimbot;

        ui::BeginPage("aimbot");
        ui::BeginCard("General");
        ui::ToggleKey("Enabled", &ab::enabled, &ab::keybind, &ab::activation_mode);
        ui::Toggle("Sticky aim", &ab::sticky_aim);
        ui::Toggle("Closest player", &ab::closest_player);
        ui::ToggleColor("Draw FOV", &ab::draw_fov, ab::fov_circle_colour, ab::fov_outline_colour);
        ui::Slider("FOV", &ab::fov, 0.0f, 1000.0f, "%.0f");
        // The choice is never changed for the user; Silent Aim simply runs as Camera outside PF.
        ui::Combo("Mode", &ab::mode, { "Mouse", "Camera", "Silent Aim (PF)" });
        if (ab::mode == 2)
        {
            ui::BeginSub();
            if (!game::is_phantom_forces)
                ui::Text("Silent Aim only works in Phantom Forces. Camera aim is used in other games.");
            ui::Slider("Head offset (Y)", &ab::ai_silent_y_offset, -1.0f, 2.0f, "%.2f studs");
            ui::EndSub();
        }
        if (ab::enabled)
        {
            ui::BeginSub();
            StatusLine(aimbot::status());
            ui::EndSub();
        }
        ui::EndCard();

        ui::BeginCard("Targeting");
        PartCombo("Target part", &ab::target_part);
        ui::Toggle("Different part in air", &ab::air_part_enabled);
        if (ab::air_part_enabled)
        {
            ui::BeginSub();
            PartCombo("Air part", &ab::air_part);
            ui::EndSub();
        }
        ui::Toggle("Use FOV", &ab::use_fov);
        ui::Toggle("Wall check", &ab::wallcheck);
        if (ab::wallcheck)
        {
            ui::BeginSub();
            WallCheckReadout();
            ui::EndSub();
        }
        ui::Toggle("Team check", &ab::teamcheck);
        ui::Toggle("Knock check", &ab::knock_check);
        ui::Toggle("Health check", &ab::health_check_enabled);
        if (ab::health_check_enabled)
        {
            ui::BeginSub();
            ui::Slider("Min health", &ab::min_health, 0.0f, 100.0f, "%.0f");
            ui::EndSub();
        }
        ui::EndCard();


        ui::BeginCard("Smoothing & prediction");
        ui::Toggle("Smoothing", &ab::smoothing);
        if (ab::smoothing)
        {
            ui::BeginSub();
            ui::Slider("Smoothing X", &ab::smoothingx, 1.0f, 100.0f, "%.1f");
            ui::Slider("Smoothing Y", &ab::smoothingy, 1.0f, 100.0f, "%.1f");
            ui::Combo("Easing", &ab::smoothing_style, { "None", "Linear", "EaseInQuad", "EaseOutQuad", "EaseInOutQuad",
                "EaseInCubic", "EaseOutCubic", "EaseInOutCubic", "EaseInSine", "EaseOutSine", "EaseInOutSine" });
            ui::EndSub();
        }
        ui::Toggle("Prediction", &ab::enable_prediction);
        if (ab::enable_prediction)
        {
            ui::BeginSub();
            ui::Slider("Prediction X", &ab::prediction_x, 0.0f, 20.0f, "%.1f");
            ui::Slider("Prediction Y", &ab::prediction_y, 0.0f, 20.0f, "%.1f");
            ui::EndSub();
        }
        ui::Toggle("Air prediction", &ab::air_prediction_enabled);
        if (ab::air_prediction_enabled)
        {
            ui::BeginSub();
            ui::Slider("Air prediction X", &ab::air_prediction_x, 0.0f, 20.0f, "%.1f");
            ui::Slider("Air prediction Y", &ab::air_prediction_y, 0.0f, 20.0f, "%.1f");
            ui::EndSub();
        }
        ui::Toggle("Offset", &ab::offset_enabled);
        if (ab::offset_enabled)
        {
            ui::BeginSub();
            ui::Slider("Offset X", &ab::offset_x, -100.0f, 100.0f, "%.1f");
            ui::Slider("Offset Y", &ab::offset_y, -100.0f, 100.0f, "%.1f");
            ui::EndSub();
        }
        ui::EndCard();

        TriggerbotCard({ &ab::triggerbot::enabled, &ab::triggerbot::keybind, &ab::triggerbot::activation_mode,
            &ab::triggerbot::fire_mode, &ab::triggerbot::clicks_per_second, &ab::triggerbot::hold_duration,
            &ab::triggerbot::reaction_ms, &ab::triggerbot::max_distance_enabled, &ab::triggerbot::max_distance,
            &ab::triggerbot::wallcheck });
        ui::EndPage();
    }

    void DrawSilentAimPage()
    {
        namespace sa = settings::silentaim;

        ui::BeginPage("silentaim");
        ui::BeginCard("General");
        ui::ToggleKey("Enabled", &sa::enabled, &sa::keybind, &sa::activation_mode);
        if (sa::enabled)
        {
            ui::BeginSub();
            StatusLine(silentaim::status());
            ui::EndSub();
        }
        ui::Toggle("Sticky aim", &sa::sticky_aim);
        ui::ToggleColor("Draw FOV", &sa::draw_fov, sa::fov_circle_colour, sa::fov_outline_colour);
        if (sa::draw_fov)
        {
            ui::BeginSub();
            ui::Toggle("Attach FOV to target", &sa::attach_fov_to_target);
            ui::EndSub();
        }
        ui::Slider("FOV", &sa::fov, 0.0f, 1000.0f, "%.0f");
        ui::Toggle("Use aimbot target", &sa::use_aimbot_target);
        ui::EndCard();

        ui::BeginCard("Targeting");
        ui::Combo("Target part", &sa::target_part, { "Nearest Point", kParts[0], kParts[1], kParts[2], kParts[3], kParts[4], kParts[5], kParts[6] });
        ui::Toggle("Prediction", &sa::enable_prediction);
        if (sa::enable_prediction)
        {
            ui::BeginSub();
            ui::Slider("Prediction X", &sa::prediction_x, 0.0f, 20.0f, "%.1f");
            ui::Slider("Prediction Y", &sa::prediction_y, 0.0f, 20.0f, "%.1f");
            ui::EndSub();
        }
        ui::Toggle("Hit chance", &sa::hitchance_enabled);
        if (sa::hitchance_enabled)
        {
            ui::BeginSub();
            ui::Slider("Chance", &sa::hitchance, 1.0f, 100.0f, "%.0f%%");
            ui::EndSub();
        }
        ui::EndCard();


        ui::BeginCard("Checks");
        ui::Toggle("Use FOV", &sa::use_fov);
        ui::Toggle("Wall check", &sa::wallcheck);
        if (sa::wallcheck)
        {
            ui::BeginSub();
            WallCheckReadout();
            ui::EndSub();
        }
        ui::Toggle("Team check", &sa::teamcheck);
        ui::Toggle("Gun check", &sa::guncheck);
        ui::Toggle("Knock check", &sa::knock_check);
        ui::Toggle("Health check", &sa::health_check_enabled);
        if (sa::health_check_enabled)
        {
            ui::BeginSub();
            ui::Slider("Min health", &sa::min_health, 0.0f, 100.0f, "%.0f");
            ui::EndSub();
        }
        ui::ToggleColor("Draw target dot", &sa::draw_target_dot, sa::target_dot_color);
        ui::EndCard();

        TriggerbotCard({ &sa::triggerbot::enabled, &sa::triggerbot::keybind, &sa::triggerbot::activation_mode,
            &sa::triggerbot::fire_mode, &sa::triggerbot::clicks_per_second, &sa::triggerbot::hold_duration,
            &sa::triggerbot::reaction_ms, &sa::triggerbot::max_distance_enabled, &sa::triggerbot::max_distance,
            &sa::triggerbot::wallcheck });
        ui::EndPage();
    }

    void DrawAimbotPage()
    {
        if (game::is_lumber_tycoon_2)
        {
            ui::BeginPage("aimbot_lt2");
            ui::BeginCard("Aimbot");
            ui::Empty("Not available in Lumber Tycoon 2");
            ui::EndCard();
                ui::EndPage();
            return;
        }
        PageAimbotMain();
    }

    // ---- visuals preview ----------------------------------------------------------------
    // A mannequin with your current ESP on it: the same chams renderer and drawing code the game
    // uses, turning slowly (drag to turn it yourself). It sits straight on the card's glass, so the
    // Dark Veil shows through like everywhere else in the menu.

    int alpha_of_col(ImU32 c) { return static_cast<int>((c >> IM_COL32_A_SHIFT) & 0xFF); }

    math::matrix3 PreviewRotY(float a)
    {
        math::matrix3 r = math::matrix3::identity();
        r.m[0][0] = cosf(a); r.m[0][2] = sinf(a); r.m[2][0] = -sinf(a); r.m[2][2] = cosf(a);
        return r;
    }

    math::matrix3 PreviewRotZ(float a)
    {
        math::matrix3 r = math::matrix3::identity();
        r.m[0][0] = cosf(a); r.m[0][1] = -sinf(a); r.m[1][0] = sinf(a); r.m[1][1] = cosf(a);
        return r;
    }

    math::matrix3 PreviewMul(const math::matrix3& a, const math::matrix3& b)
    {
        math::matrix3 r{};
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++)
                r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
        return r;
    }

    // An R15 mannequin standing at the origin, arms a little out, turned by `yaw`.
    rig::body_t PreviewBody(float yaw)
    {
        struct piece { const char* name; math::vector3 size, pos; float roll; math::vector3 pivot; };
        static const piece pieces[] = {
            { "LeftFoot", { 1, 0.3f, 1 }, { -0.5f, 0.15f, 0 }, 0, {} }, { "RightFoot", { 1, 0.3f, 1 }, { 0.5f, 0.15f, 0 }, 0, {} },
            { "LeftLowerLeg", { 1, 1.1f, 1 }, { -0.5f, 0.85f, 0 }, 0, {} }, { "RightLowerLeg", { 1, 1.1f, 1 }, { 0.5f, 0.85f, 0 }, 0, {} },
            { "LeftUpperLeg", { 1, 1.2f, 1 }, { -0.5f, 2.0f, 0 }, 0, {} }, { "RightUpperLeg", { 1, 1.2f, 1 }, { 0.5f, 2.0f, 0 }, 0, {} },
            { "LowerTorso", { 2, 0.4f, 1 }, { 0, 2.8f, 0 }, 0, {} }, { "UpperTorso", { 2, 1.6f, 1 }, { 0, 3.8f, 0 }, 0, {} },
            { "Head", { 1.2f, 1.2f, 1.2f }, { 0, 5.2f, 0 }, 0, {} },
            { "LeftUpperArm", { 1, 1.2f, 1 }, { -1.5f, 4.0f, 0 }, -0.14f, { -1.5f, 4.6f, 0 } },
            { "RightUpperArm", { 1, 1.2f, 1 }, { 1.5f, 4.0f, 0 }, 0.14f, { 1.5f, 4.6f, 0 } },
            { "LeftLowerArm", { 1, 1.1f, 1 }, { -1.5f, 2.85f, 0 }, -0.14f, { -1.5f, 4.6f, 0 } },
            { "RightLowerArm", { 1, 1.1f, 1 }, { 1.5f, 2.85f, 0 }, 0.14f, { 1.5f, 4.6f, 0 } },
            { "LeftHand", { 1, 0.3f, 1 }, { -1.5f, 2.15f, 0 }, -0.14f, { -1.5f, 4.6f, 0 } },
            { "RightHand", { 1, 0.3f, 1 }, { 1.5f, 2.15f, 0 }, 0.14f, { 1.5f, 4.6f, 0 } },
        };
        rig::body_t body;
        body.kind = rig::kind_t::r15;
        const math::matrix3 turn = PreviewRotY(yaw);
        for (const piece& pc : pieces)
        {
            rig::part_t p;
            p.name = pc.name;
            p.size = pc.size;
            p.rot = math::matrix3::identity();
            p.pos = pc.pos;
            if (pc.roll != 0.0f)
            {
                const math::matrix3 r = PreviewRotZ(pc.roll);
                p.pos = pc.pivot + r * (pc.pos - pc.pivot);
                p.rot = r;
            }
            p.pos = turn * p.pos;
            p.rot = PreviewMul(turn, p.rot);
            body.parts.push_back(p);
        }
        return body;
    }

    // A soft ellipse fading from `inner` at the centre to clear at the rim, as one mesh (stacked
    // circles showed as rings).
    void SoftEllipse(ImDrawList* dl, ImVec2 c, ImVec2 radius, ImU32 inner, int segments = 48)
    {
        const ImVec2 uv = dl->_Data->TexUvWhitePixel;
        const ImU32 outer = inner & ~IM_COL32_A_MASK;
        dl->PrimReserve(segments * 3, segments + 1);
        const ImDrawIdx base = static_cast<ImDrawIdx>(dl->_VtxCurrentIdx);
        dl->PrimWriteVtx(c, uv, inner);
        for (int i = 0; i < segments; ++i)
        {
            const float t = i * IM_PI * 2.0f / segments;
            dl->PrimWriteVtx(ImVec2(c.x + cosf(t) * radius.x, c.y + sinf(t) * radius.y), uv, outer);
        }
        for (int i = 0; i < segments; ++i)
        {
            dl->PrimWriteIdx(base);
            dl->PrimWriteIdx(static_cast<ImDrawIdx>(base + 1 + i));
            dl->PrimWriteIdx(static_cast<ImDrawIdx>(base + 1 + (i + 1) % segments));
        }
    }

    // The mannequin's visible faces, far to near, lit from the upper front left: a clay figure
    // in the menu's whites and greys. `shade_only` draws just the darkening, to lay the same
    // lighting over chams drawn on top of it. `inset` (studs) shrinks every part: chams round
    // their parts' corners, and a full-size box peeked out around them.
    template <typename Project>
    void DrawMannequin(ImDrawList* dl, const rig::body_t& body, const math::vector3& eye, Project&& project, bool shade_only, float inset = 0.0f)
    {
        struct face_t { ImVec2 pts[4]; float depth; float shade; bool head; };
        std::vector<face_t> faces;
        faces.reserve(body.parts.size() * 3);
        const math::vector3 light = math::vector3{ -0.35f, 0.6f, 0.72f }.normalized();
        for (const rig::part_t& p : body.parts)
        {
            const math::vector3 h{ (std::max)(0.05f, p.size.x * 0.5f - inset), (std::max)(0.05f, p.size.y * 0.5f - inset), (std::max)(0.05f, p.size.z * 0.5f - inset) };
            for (int k = 0; k < 3; ++k)
                for (int sign = -1; sign <= 1; sign += 2)
                {
                    math::vector3 local_n{}, local_c{};
                    local_n[k] = static_cast<float>(sign);
                    local_c[k] = sign * h[k];
                    const math::vector3 n = p.rot * local_n;
                    const math::vector3 centre = p.pos + p.rot * local_c;
                    if (n.dot(eye - centre) <= 0.0f)
                        continue;   // faces away from the camera
                    const int u = (k + 1) % 3, v = (k + 2) % 3;
                    static const float su[4] = { -1, 1, 1, -1 }, sv[4] = { -1, -1, 1, 1 };
                    face_t f{};
                    bool on_screen = true;
                    for (int i = 0; i < 4 && on_screen; ++i)
                    {
                        math::vector3 l = local_c;
                        l[u] = su[i] * h[u];
                        l[v] = sv[i] * h[v];
                        on_screen = project(p.pos + p.rot * l, f.pts[i]);
                    }
                    if (!on_screen)
                        continue;
                    f.depth = (eye - centre).length();
                    f.shade = 0.55f + 0.45f * (std::max)(0.0f, n.dot(light));
                    f.head = p.name == "Head";
                    faces.push_back(f);
                }
        }
        std::sort(faces.begin(), faces.end(), [](const face_t& a, const face_t& b) { return a.depth > b.depth; });

        // Faces meet edge to edge: anti-aliased fills left hairline gaps between them.
        const ImDrawListFlags flags = dl->Flags;
        dl->Flags &= ~ImDrawListFlags_AntiAliasedFill;
        for (const face_t& f : faces)
        {
            if (shade_only)
            {
                dl->AddQuadFilled(f.pts[0], f.pts[1], f.pts[2], f.pts[3], IM_COL32(0, 0, 0, static_cast<int>((1.0f - f.shade) * 190.0f)));
                continue;
            }
            const float base = f.head ? 238.0f : 224.0f;
            const int g = static_cast<int>(base * f.shade);
            dl->AddQuadFilled(f.pts[0], f.pts[1], f.pts[2], f.pts[3], IM_COL32(g, g, (std::min)(255, g + 5), 255));
        }
        dl->Flags = flags;
        if (!shade_only)
            for (const face_t& f : faces)
                dl->AddQuad(f.pts[0], f.pts[1], f.pts[2], f.pts[3], IM_COL32(0, 0, 0, 34), 1.0f);
    }

    void VisualsPreview()
    {
        namespace v = settings::visuals;
        ui::BeginCard("Preview");
        ImGuiWindow* w = ImGui::GetCurrentWindow();
        ImDrawList* dl = w->DrawList;
        const float width = ui::RowWidth();
        const float height = 320.0f;
        const ImVec2 a = w->DC.CursorPos;
        const ImRect r(a, ImVec2(a.x + width, a.y + height));
        ImGui::ItemSize(r.GetSize());
        const ImGuiID id = w->GetID("##visuals_preview");
        if (!ImGui::ItemAdd(r, id))
        {
            ui::EndCard();
            return;
        }

        // Sways around a three-quarter view; drag to turn it.
        static float drag = 0.0f;
        bool hovered = false, held = false;
        ImGui::ButtonBehavior(r, id, &hovered, &held);
        if (held)
            drag += ImGui::GetIO().MouseDelta.x * 0.012f;
        const float yaw = 0.55f * sinf(static_cast<float>(ImGui::GetTime()) * 0.6f) + 0.35f + drag;
        if (hovered || held)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

        // No backdrop of its own: the figure stands on the card's glass.
        dl->PushClipRect(r.Min, r.Max, true);

        // Camera: 13.5 studs out, so the 5.8-stud figure fills about 70% of the height and the
        // name above and the info below still fit.
        const float aspect = width / height;
        const math::vector3 eye{ 0.0f, 3.4f, 13.5f };
        math::matrix4 vp{};
        {
            const math::vector3 target{ 0.0f, 2.75f, 0.0f };
            const math::vector3 back = (eye - target).normalized();
            const math::vector3 right = math::vector3{ 0, 1, 0 }.cross(back).normalized();
            const math::vector3 up = back.cross(right);
            const float f = 1.0f / tanf(34.0f * 3.14159265f / 360.0f);
            const math::vector3 rows[3] = { right, up, back };
            float V[4][4]{};
            for (int i = 0; i < 3; i++) { V[i][0] = rows[i].x; V[i][1] = rows[i].y; V[i][2] = rows[i].z; V[i][3] = -rows[i].dot(eye); }
            V[3][3] = 1.0f;
            float P[4][4]{};
            P[0][0] = f / aspect; P[1][1] = f; P[2][2] = -1.0f; P[2][3] = -0.2f; P[3][2] = -1.0f;
            for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) { float sum = 0; for (int k = 0; k < 4; k++) sum += P[i][k] * V[k][j]; vp.m[i][j] = sum; }
        }
        auto project = [&](const math::vector3& p, ImVec2& out)
        {
            const math::vector4 q = vp.multiply({ p.x, p.y, p.z, 1.0f });
            if (q.w < 0.1f)
                return false;
            out = ImVec2(r.Min.x + (q.x / q.w * 0.5f + 0.5f) * width, r.Min.y + (0.5f - q.y / q.w * 0.5f) * height);
            return true;
        };

        const rig::body_t body = PreviewBody(yaw);

        // A soft contact shadow under its feet, then the figure.
        ImVec2 feet;
        if (project({ 0.0f, 0.0f, 0.0f }, feet))
            SoftEllipse(dl, feet, ImVec2(64.0f, 11.0f), IM_COL32(0, 0, 0, 150));
        // With chams on they are the body: only the figure's lighting goes over them.
        if (!v::chams)
            DrawMannequin(dl, body, eye, project, false);

        // Chams through the real renderer (Roblox highlight is drawn by the game, so a flat fill
        // stands in for it here), rendered at twice the size so its edges come out smooth, and
        // the figure's lighting laid back over it so it still reads as a body.
        if (v::chams)
        {
            body_chams::frame_t frame;
            frame.view_projection = vp;
            frame.dims = { width * 2.0f, height * 2.0f };
            frame.effect = v::chams_style == 1 ? body_chams::effect_water + v::chams_effect : body_chams::effect_flat;
            frame.speed = v::chams_texture_speed;
            frame.scale = v::chams_texture_scale;
            float fill[4] = { v::chams_fill_color[0], v::chams_fill_color[1], v::chams_fill_color[2], v::chams_fill_enabled ? v::chams_fill_color[3] : 0.0f };
            float outline[4] = { v::chams_outline_color[0], v::chams_outline_color[1], v::chams_outline_color[2], v::chams_outline_enabled ? v::chams_outline_color[3] : 0.0f };
            if (const ImTextureID tex = body_chams::render_preview(&body, 1, fill, outline, frame))
            {
                dl->AddImage(tex, r.Min, r.Max);
                DrawMannequin(dl, body, eye, project, true, 0.04f);
            }
        }

        if (v::skeleton)
        {
            std::vector<std::pair<math::vector3, math::vector3>> bones;
            std::vector<std::pair<ImVec2, ImVec2>> lines;
            rig::skeleton(body, bones);
            for (const auto& [p, q] : bones)
            {
                ImVec2 sp, sq;
                if (project(p, sp) && project(q, sq))
                    lines.emplace_back(sp, sq);
            }
            esp_draw::skeleton(dl, lines, ImGui::ColorConvertFloat4ToU32(ImVec4(v::skeleton_color[0], v::skeleton_color[1], v::skeleton_color[2], v::skeleton_color[3])),
                v::skeleton_thickness);
        }

        // The box around every part's corners, then the labels.
        ImVec2 mn(FLT_MAX, FLT_MAX), mx(-FLT_MAX, -FLT_MAX);
        for (const rig::part_t& p : body.parts)
            for (int i = 0; i < 8; ++i)
            {
                const math::vector3 corner{ (i & 1) ? 0.5f : -0.5f, (i & 2) ? 0.5f : -0.5f, (i & 4) ? 0.5f : -0.5f };
                ImVec2 s;
                if (project(p.pos + p.rot * math::vector3{ corner.x * p.size.x, corner.y * p.size.y, corner.z * p.size.z }, s))
                {
                    mn = ImMin(mn, s);
                    mx = ImMax(mx, s);
                }
            }

        auto col = [](const float* c) { return ImGui::ColorConvertFloat4ToU32(ImVec4(c[0], c[1], c[2], c[3])); };
        esp_draw::player_t look{};
        look.min = mn;
        look.max = mx;
        if (v::box)
        {
            look.box = col(v::box_color);
            look.box_style = v::box_type == 1 ? 1 : v::box_type == 4 ? 2 : 0;
        }
        if (v::box_fill)
            look.fill = col(v::box_fill_color);
        if (v::name)
        {
            look.name = v::name_display_type == 0 ? "natware" : "natware_user";
            look.name_col = col(v::blend ? v::name_color_blend_start : v::name_color);
            look.accent = IM_COL32(107, 18, 255, 255);
        }
        if (v::healthbar || v::health_percent)
        {
            look.health = 0.72f;
            look.bar_alpha = v::healthbar ? 1.0f : 0.0f;
            if (v::healthbar && !v::health_based_healthbar)
                look.health_col = col(v::healthbar_color);
            look.health_text = v::health_percent;
            look.health_text_col = col(v::health_percent_color);
        }
        if (v::armorbar)
        {
            look.armor = 0.45f;
            look.armor_col = col(v::armorbar_color);
        }
        char info[64] = {};
        if (v::distance)
            snprintf(info, sizeof(info), v::distance_measurement == 1 ? "7 m" : "24 studs");
        if (v::tool)
        {
            const size_t n = strlen(info);
            snprintf(info + n, sizeof(info) - n, "%sRifle", n ? "  \xC2\xB7  " : "");
        }
        if (info[0])
        {
            look.info = info;
            look.info_col = col(v::distance ? v::distance_color : v::tool_color);
        }
        if (v::flags)
        {
            look.flag = "Running";
            look.flag_col = col(v::flags_state_colour);
        }

        if (v::esp_style == 1)
        {
            // Classic: hard pixel box and outlined pixel text, like the old ESP.
            if (alpha_of_col(look.box))
            {
                dl->AddRect(look.min, look.max, IM_COL32(0, 0, 0, 255));
                dl->AddRect(ImVec2(look.min.x - 1, look.min.y - 1), ImVec2(look.max.x + 1, look.max.y + 1), look.box);
                dl->AddRect(ImVec2(look.min.x - 2, look.min.y - 2), ImVec2(look.max.x + 2, look.max.y + 2), IM_COL32(0, 0, 0, 255));
            }
            ImFont* font = v::esp_font == 0 ? esp_font_tahoma : v::esp_font == 1 ? esp_font_sp7 : esp_font_arial;
            const float size = v::esp_font == 0 ? font_config::tahoma::font_size : v::esp_font == 1 ? font_config::sp7::font_size : font_config::arial::font_size;
            if (font && look.name)
            {
                const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, look.name);
                const ImVec2 pos(roundf((look.min.x + look.max.x - ts.x) * 0.5f), roundf(look.min.y - size - 5.0f));
                for (int x = -1; x <= 1; x++) for (int y = -1; y <= 1; y++) if (x || y) dl->AddText(font, size, ImVec2(pos.x + x, pos.y + y), IM_COL32(0, 0, 0, 255), look.name);
                dl->AddText(font, size, pos, look.name_col, look.name);
            }
            if (look.health >= 0.0f && look.bar_alpha > 0.0f)
            {
                dl->AddRectFilled(ImVec2(look.min.x - 7, look.min.y - 2), ImVec2(look.min.x - 3, look.max.y + 2), IM_COL32(0, 0, 0, 255));
                const float fill_h = (look.max.y - look.min.y + 2.0f) * look.health;
                dl->AddRectFilled(ImVec2(look.min.x - 6, look.max.y + 1 - fill_h), ImVec2(look.min.x - 4, look.max.y + 1),
                    look.health_col ? look.health_col : IM_COL32(0, 255, 0, 255));
            }
            if (font && look.info)
            {
                const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, look.info);
                const ImVec2 pos(roundf((look.min.x + look.max.x - ts.x) * 0.5f), roundf(look.max.y + 5.0f));
                for (int x = -1; x <= 1; x++) for (int y = -1; y <= 1; y++) if (x || y) dl->AddText(font, size, ImVec2(pos.x + x, pos.y + y), IM_COL32(0, 0, 0, 255), look.info);
                dl->AddText(font, size, pos, look.info_col, look.info);
            }
        }
        else
        {
            esp_draw::player(dl, look);
        }

        dl->AddText(ui::Regular(), 12.0f, ImVec2(r.Min.x + 14.0f, r.Max.y - 24.0f), IM_COL32(255, 255, 255, 80), "Drag to turn");
        dl->PopClipRect();
        ui::EndCard();
    }

    // ESP on other players: what to draw, how it looks, and who it skips. Each option's own
    // settings sit right under it.
    void PageVisuals()
    {
        namespace v = settings::visuals;

        ui::BeginPage("visuals");
        if (game::is_murder_mystery_2)
        {
            ui::BeginCard("Murder Mystery 2");
            ui::Toggle("Show roles", &v::mm2_esp);
            if (v::mm2_esp)
            {
                ui::Legend("Murderer", IM_COL32(255, 38, 38, 255));
                ui::Legend("Sheriff", IM_COL32(38, 102, 255, 255));
                ui::Legend("Innocent", IM_COL32(153, 153, 153, 255));
            }
            ui::EndCard();
        }

        VisualsPreview();

        ui::BeginCard("Players");
        ui::ToggleKey("Enabled", &v::enable_enemies, &v::esp_key, &v::esp_key_mode);
        ui::Combo("Look", &v::esp_style, { "natware", "Classic" });
        ui::Toggle("Team check", &v::teamcheck);
        ui::Toggle("Team colours", &v::use_team_color);
        ui::Toggle("Target warning icon", &v::target_warning_icon);
        ui::EndCard();

        ui::BeginCard("Box");
        ui::ToggleColor("Box", &v::box, v::box_color);
        if (v::box)
        {
            ui::BeginSub();
            ui::Combo("Style", &v::box_type, { "Bounding", "Corner", "Rounded", "Cross", "Glow", "Diamond" });
            if (v::box_type == 4)
                ui::Slider("Glow strength", &v::box_glow_strength, 0.1f, 1.0f, "%.1f");
            ui::EndSub();
        }
        ui::ToggleColor("Fill", &v::box_fill, v::box_fill_color);
        ui::EndCard();

        ui::BeginCard("Body");
        ui::ToggleColor("Skeleton", &v::skeleton, v::skeleton_color);
        if (v::skeleton)
        {
            ui::BeginSub();
            ui::Slider("Thickness", &v::skeleton_thickness, 1.0f, 3.0f, "%.1f px");
            ui::EndSub();
        }
        ui::ToggleKeyColor("Chams", &v::chams, &v::chams_key, &v::chams_key_mode, v::chams_fill_color, v::chams_outline_color);
        if (v::chams)
        {
            ui::BeginSub();
            ui::Combo("Style", &v::chams_style, { "Flat", "Engine", "Roblox highlight" });
            if (v::chams_style == 1)
            {
                ui::Combo("Effect", &v::chams_effect, { "Water", "Plasma", "Hologram", "Galaxy" });
                ui::Slider("Speed", &v::chams_texture_speed, 0.1f, 5.0f, "%.1f");
                ui::Slider("Scale", &v::chams_texture_scale, 0.2f, 3.0f, "%.1f");
            }
            else if (v::chams_style == 2)
            {
                const auto status = engine_chams::get_status();
                char line[128];
                snprintf(line, sizeof(line), "Drawn by Roblox itself: %s (%zu players)",
                    engine_chams::describe_status(status), engine_chams::get_player_count());
                ui::Text(line, status == engine_chams::Status::Active ? ui::col::text : ui::col::text_dim);
            }
            ui::Toggle("Fill", &v::chams_fill_enabled);
            ui::Toggle("Outline", &v::chams_outline_enabled);
            if (v::chams_style != 2)
                StatusLine(body_chams::error());
            ui::EndSub();
        }
        ui::EndCard();

        ui::BeginCard("Information");
        ui::ToggleColor("Name", &v::name, v::name_color);
        if (v::name)
        {
            ui::BeginSub();
            ui::Combo("Shows", &v::name_display_type, { "Display Name", "Username" });
            ui::Toggle("Avatar", &v::avatar);
            ui::ToggleColor("Colour blend", &v::blend, v::name_color_blend_start, v::name_color_blend_end);
            ui::EndSub();
        }
        ui::ToggleColor("Health bar", &v::healthbar, v::healthbar_color);
        if (v::healthbar)
        {
            ui::BeginSub();
            ui::Toggle("Colour by health", &v::health_based_healthbar);
            ui::EndSub();
        }
        ui::ToggleColor("Health percent", &v::health_percent, v::health_percent_color);
        ui::ToggleColor("Armor bar", &v::armorbar, v::armorbar_color);
        ui::ToggleColor("Distance", &v::distance, v::distance_color);
        if (v::distance)
        {
            ui::BeginSub();
            ui::Combo("Unit", &v::distance_measurement, { "Studs", "Meters" });
            ui::EndSub();
        }
        ui::Toggle("Team name", &v::team_name);
        ui::ToggleColor("Tool", &v::tool, v::tool_color);
        ui::ToggleColor("Flags", &v::flags, v::flags_state_colour);
        if (v::esp_style == 1)
            ui::Combo("Font", &v::esp_font, { "Tahoma", "Smallest Pixel", "Arial" });
        ui::EndCard();

        ui::BeginCard("Filters");
        ui::Toggle("Knock check", &v::knock_check);
        ui::Toggle("Ignore whitelisted", &v::ignore_whitelisted);
        ui::Toggle("Max distance", &v::max_distance_enabled);
        if (v::max_distance_enabled)
        {
            ui::BeginSub();
            ui::Slider("Distance", &v::max_distance, 1.0f, 5000.0f, "%.0f studs");
            if (v::max_distance < 1.0f)
                v::max_distance = 1.0f;
            ui::EndSub();
        }
        ui::Toggle("Dim players behind walls", &v::debug_wallcheck);
        if (v::debug_wallcheck)
        {
            ui::BeginSub();
            WallCheckReadout();
            ui::EndSub();
        }
        ui::EndCard();

        ui::BeginCard("Fading");
        ui::Slider("Fade in speed", &v::fade_in_speed, 0.1f, 50.0f, "%.1f");
        ui::Slider("Fade out speed", &v::fade_out_speed, 0.1f, 50.0f, "%.1f");
        ui::EndCard();

        ui::BeginCard("Self");
        ui::Toggle("Enabled", &v::enable_client);
        ui::ToggleColor("Box", &v::client_box, v::client_box_color);
        ui::ToggleColor("Box fill", &v::client_box_fill, v::client_box_fill_color);
        ui::ToggleColor("Skeleton", &v::client_skeleton, v::client_skeleton_color);
        ui::ToggleColor("Chams", &v::client_chams, v::client_chams_fill_color, v::client_chams_outline_color);
        ui::Toggle("Headless", &v::client_headless);
        ui::Toggle("Korblox", &v::client_korblox);
        ui::EndCard();
        ui::EndPage();
    }

    // The game world: lighting, the radar, and ESP for custom entity containers.
    void PageWorld()
    {
        namespace lt = settings::lighting;
        namespace v = settings::visuals;
        namespace ce = settings::custom_entities;

        ui::BeginPage("world");
        ui::BeginCard("Lighting");
        float fog_color[4] = { lt::fog::fog_r, lt::fog::fog_g, lt::fog::fog_b, 1.0f };
        ui::ToggleColor("Fog", &lt::fog::enabled, fog_color);
        lt::fog::fog_r = fog_color[0];
        lt::fog::fog_g = fog_color[1];
        lt::fog::fog_b = fog_color[2];
        if (lt::fog::enabled)
        {
            ui::BeginSub();
            ui::Slider("Fog start", &lt::fog::fog_start, 0.0f, 5000.0f, "%.0f studs");
            ui::Slider("Fog end", &lt::fog::fog_end, 10.0f, 5000.0f, "%.0f studs");
            StatusLine(lighting::fog::status());
            ui::EndSub();
        }
        ui::Toggle("Disable shadows", &lt::shadows::disable);
        ui::Toggle("Exposure", &lt::exposure::enabled);
        if (lt::exposure::enabled)
        {
            ui::BeginSub();
            ui::Slider("Exposure", &lt::exposure::exposure, -10.0f, 10.0f, "%.1f");
            ui::EndSub();
        }
        ui::Toggle("Clock time", &lt::clocktime::enabled);
        if (lt::clocktime::enabled)
        {
            ui::BeginSub();
            ui::Slider("Time", &lt::clocktime::clock_time, 0.0f, 24.0f, "%.1f");
            ui::EndSub();
        }
        ui::EndCard();

        ui::BeginCard("Custom entities");
        ui::Toggle("Show custom entities", &ce::show_custom_entities);
        ui::Toggle("Auto refresh", &ce::auto_refresh);
        if (ce::auto_refresh)
        {
            ui::BeginSub();
            ui::Slider("Refresh rate", &ce::refresh_rate, 0.001f, 1.0f, "%.3f s");
            ui::EndSub();
        }
        ui::Spacing(10.0f);
        static char path_input[256];
        strncpy_s(path_input, ce::current_input.c_str(), sizeof(path_input));
        if (ui::InputText("##ce_path", path_input, sizeof(path_input), "Container path"))
            ce::current_input = path_input;
        ui::Spacing(10.0f);
        if (ui::Button("Set container"))
            custom_entities::set_container(ce::current_input);
        ui::Spacing(6.0f);

        std::vector<settings::custom_entities::custom_container_t> containers_snapshot;
        {
            std::lock_guard<std::mutex> lock(custom_entities::containers_mtx);
            containers_snapshot = ce::containers;
        }
        if (containers_snapshot.empty())
            ui::Text("No containers set", ui::col::text_faint);
        for (const auto& container : containers_snapshot)
            ui::Text(container.path.c_str());
        ui::EndCard();
        ui::EndPage();
    }

    // Moving your character: movement hacks, character tricks, and LT2 teleports.
    void PageMovement()
    {
        namespace mv = settings::movement;
        namespace rg = settings::rage;
        namespace orbit = settings::movement::orbit;

        ui::BeginPage("movement");
        ui::BeginCard("Movement");
        ui::ToggleKey("Speed", &mv::speedhack::enabled, &mv::speedhack::keybind, &mv::speedhack::activation_mode);
        if (mv::speedhack::enabled)
        {
            ui::BeginSub();
            ui::Combo("Mode", &mv::speedhack::mode, { "Velocity", "WalkSpeed" });
            ui::Slider("Speed", &mv::speedhack::speed, 1.0f, 1000.0f, "%.1f");
            ui::EndSub();
        }
        ui::ToggleKey("Jump", &mv::jumphack::enabled, &mv::jumphack::keybind, &mv::jumphack::activation_mode);
        if (mv::jumphack::enabled)
        {
            ui::BeginSub();
            ui::Slider("Jump power", &mv::jumphack::value, 50.0f, 300.0f, "%.1f");
            ui::EndSub();
        }
        ui::ToggleKey("Fly", &mv::flyhack::enabled, &mv::flyhack::keybind, &mv::flyhack::activation_mode);
        if (mv::flyhack::enabled)
        {
            ui::BeginSub();
            ui::Combo("Mode", &mv::flyhack::mode, { "Velocity", "Position", "CFrame" });
            ui::Slider("Fly speed", &mv::flyhack::speed, 1.0f, 1000.0f, "%.1f");
            ui::EndSub();
        }
        ui::Toggle("Tickrate", &mv::tickrate::enabled);
        if (mv::tickrate::enabled)
        {
            ui::BeginSub();
            ui::Slider("Tickrate", &mv::tickrate::value, 0.0f, 1000.0f, "%.1f");
            ui::EndSub();
        }
        ui::Toggle("Gravity", &mv::gravity::enabled);
        if (mv::gravity::enabled)
        {
            ui::BeginSub();
            ui::Slider("Gravity", &mv::gravity::value, 0.0f, 400.0f, "%.1f");
            ui::EndSub();
        }
        ui::ToggleKey("Noclip", &settings::rage::noclip, &settings::rage::noclip_key, &settings::rage::noclip_key_mode);
        ui::Toggle("No jump cooldown", &mv::nojumpcooldown::enabled);
        ui::EndCard();

        ui::BeginCard("Character");
        ui::ToggleKey("Freeze player", &settings::exploits::freezeplayer::enabled,
            &settings::exploits::freezeplayer::keybind, &settings::exploits::freezeplayer::activation_mode);
        ui::ToggleKey("Hip height", &rg::hipheight::enabled, &rg::hipheight::key, &rg::hipheight::key_mode);
        if (rg::hipheight::enabled)
        {
            ui::BeginSub();
            ui::Slider("Height", &rg::hipheight::height, 2.0f, 500.0f, "%.1f");
            ui::EndSub();
        }
        ui::ToggleKey("Spinbot", &rg::spin360::enabled, &rg::spin360::keybind, &rg::spin360::activation_mode);
        if (rg::spin360::enabled)
        {
            ui::BeginSub();
            ui::Slider("Spin speed", &rg::spin360::speed, 1.0f, 10.0f, "%.1f");
            ui::EndSub();
        }
        ui::ToggleKey("Orbit", &orbit::enabled, &orbit::key, &orbit::key_mode);
        if (orbit::enabled)
        {
            ui::BeginSub();
            ui::Combo("Orbit target", &orbit::orbit_type, { "Aimbot Target", "Silent Target" });
            ui::Slider("Speed", &orbit::speed, 1.0f, 100.0f, "%.1f");
            ui::Slider("Radius", &orbit::radius, 1.0f, 50.0f, "%.1f");
            ui::Slider("Height offset", &orbit::height_offset, 0.0f, 50.0f, "%.1f");
            ui::Toggle("Spectate target", &orbit::spectate_target);
            ui::Toggle("Randomize", &orbit::randomize);
            if (orbit::randomize)
            {
                ui::BeginSub();
                ui::Slider("Randomize X", &orbit::randomize_x, 0.0f, 50.0f, "%.1f");
                ui::Slider("Randomize Y", &orbit::randomize_y, 0.0f, 50.0f, "%.1f");
                ui::EndSub();
            }
            ui::EndSub();
        }
        ui::Toggle("Anti AFK", &settings::exploits::antiafk::enabled);
        ui::EndCard();

        if (game::is_lumber_tycoon_2)
        {
            ui::BeginCard("Teleport");
            const auto& locations = lt2::get_teleport_locations();
            for (size_t i = 0; i < locations.size(); i++)
            {
                ImGui::PushID((int)i);
                if (ui::Button(locations[i].name))
                    lt2::teleport_to(locations[i].position);
                ImGui::PopID();
                ui::Spacing(6.0f);
            }
            ui::EndCard();
        }
        ui::EndPage();
    }

    // Combat beyond aiming: weapon tweaks and hit feedback.
    void PageCombat()
    {
        namespace rg = settings::rage;

        ui::BeginPage("combat");
        ui::BeginCard("Weapon");
        ui::ToggleKey("Rapid fire", &rg::rapidfire, &rg::rapidfire_key, &rg::rapidfire_key_mode);
        if (rg::rapidfire)
        {
            ui::BeginSub();
            ui::Combo("Method", &rg::rapidfire_mode, { "Gun values", "Auto-click", "Both" });
            if (rg::rapidfire_mode != 0)
                ui::Slider("Clicks per second", &rg::rapidfire_cps, 5.0f, 30.0f, "%.0f");
            StatusLine(rage::rapidfire::status());
            ui::EndSub();
        }
        ui::ToggleKey("Hitbox expander", &rg::hitbox_expander::enabled, &rg::hitbox_expander::key, &rg::hitbox_expander::key_mode);
        if (rg::hitbox_expander::enabled)
        {
            ui::BeginSub();
            ui::Combo("Part", &rg::hitbox_expander::target_part, { "Head", "Root", "Torso" });
            ui::Slider("Size X", &rg::hitbox_expander::size_x, 0.1f, 30.0f, "%.1f");
            ui::Slider("Size Y", &rg::hitbox_expander::size_y, 0.1f, 30.0f, "%.1f");
            ui::Slider("Size Z", &rg::hitbox_expander::size_z, 0.1f, 30.0f, "%.1f");
            ui::Toggle("Knock check", &rg::hitbox_expander::knock_check);
            ui::ToggleColor("Show hitbox", &settings::visuals::view_hitbox, settings::visuals::view_hitbox_color);
            StatusLine(rage::hitbox_expander::status());
            ui::EndSub();
        }
        ui::EndCard();

        ui::BeginCard("Hit feedback");
        ui::Toggle("Hit sounds", &rg::hitsounds);
        if (rg::hitsounds)
        {
            ui::BeginSub();
            ui::Combo("Detect hits by", &rg::hitsound_method, { "Health", "Click", "Ammo" });
            ui::Combo("Sound", &rg::hitsound_type, { "Among Us", "Skeet", "Beep", "Bonk", "Bubble", "COD", "CSGO",
                "Fairy", "Fatality", "Osu", "Neverlose" });
            ui::EndSub();
        }
        ui::ToggleColor("Hit tracers", &rg::hit_tracers, rg::hit_tracers_color);
        if (rg::hit_tracers)
        {
            ui::BeginSub();
            ui::Combo("Detect hits by", &settings::visuals::hit_tracers_method, { "Health", "Click", "Ammo" });
            ui::Slider("Duration", &rg::hit_tracers_duration, 0.1f, 5.0f, "%.1f s");
            ui::EndSub();
        }
        ui::EndCard();
        ui::EndPage();
    }

    // The program itself: the menu and the watermark.
    void PageSettings()
    {
        namespace wm = settings::watermark;

        ui::BeginPage("misc_settings");
        ui::BeginCard("Menu");
        ui::Keybind("Menu key", &settings::menu::menu_keybind);
        ui::Toggle("Hide from screen capture", &settings::menu::streamproof);
        ui::Toggle("V-Sync", &settings::menu::vsync);
        ui::Toggle("Performance mode", &settings::menu::performance_mode);
        ui::Toggle("Stay loaded", &settings::menu::stay_loaded);
        ui::Toggle("Uncap client FPS", &settings::cilent::fpscaps::enabled);
        ui::Spacing(10.0f);
        if (ui::Button("Save diagnostics"))
        {
            // There is no console: the report goes to data\diagnostics.txt, opened for reading.
            FILE* f = nullptr;
            if (fopen_s(&f, "data\\diagnostics.txt", "w") == 0 && f)
            {
                fprintf(f, "natware diagnostics\n\n");
                fprintf(f, "Module base: 0x%llx\n", (unsigned long long)memory->get_module_address());
                fprintf(f, "DataModel: 0x%llx\n", (unsigned long long)game::datamodel.address);
                fprintf(f, "VisEngine: 0x%llx\n", (unsigned long long)game::visengine.address);
                fprintf(f, "Camera: 0x%llx\n", (unsigned long long)game::camera);
                fprintf(f, "FFlag source: %s\n", OffsetLoader::g_LoadedFFlagsFromWeb ? "web" : "built-in");
                fprintf(f, "FFlag NextGenReplicator offset: 0x%llx\n", (unsigned long long)FFlagOffsets::FFlags::NextGenReplicatorEnabledWrite4);
                fprintf(f, "Offsets: %s (%s)\n", Offsets::ClientVersion.c_str(),
                    OffsetLoader::WasLoadedFromWeb() ? "web" : OffsetLoader::WasLoadedFromJson() ? "json" : "built-in");
                fprintf(f, "%s\n", settings::globals::offset_validation_result.c_str());
                fclose(f);
                ShellExecuteA(nullptr, "open", "data\\diagnostics.txt", nullptr, nullptr, SW_SHOWNORMAL);
            }
        }
        ui::Spacing(6.0f);
        if (ui::Button("Unload", ui::Btn::Danger))
            settings::globals::quit_requested = true;   // the main loop exits cleanly next frame
        ui::EndCard();

        ui::BeginCard("Watermark");
        ui::Toggle("Enabled", &settings::menu::watermark);
        if (settings::menu::watermark)
        {
            ui::BeginSub();
            ui::Toggle("Cheat name", &wm::show_cheat_name);
            ui::Toggle("Display name", &wm::show_display_name);
            ui::Toggle("Username", &wm::show_username);
            ui::Toggle("FPS", &wm::show_fps);
            ui::Toggle("Server IP", &wm::show_server_ip);
            ui::Toggle("Rainbow", &wm::rainbow);
            if (wm::rainbow)
            {
                ui::BeginSub();
                ui::Slider("Speed", &wm::rainbow_speed, 0.1f, 5.0f, "%.1f");
                ui::EndSub();
            }
            else
            {
                ui::ColorRow("Text colour", wm::text_color);
            }
            ui::EndSub();
        }
        ui::EndCard();
        ui::EndPage();
    }

    // How long ago a config file was written, for the list.
    std::string Age(std::filesystem::file_time_type when)
    {
        using namespace std::chrono;
        const auto now = std::filesystem::file_time_type::clock::now();
        const long long s = duration_cast<seconds>(now - when).count();
        if (s < 60) return "Just now";
        if (s < 3600) return std::to_string(s / 60) + " min ago";
        if (s < 86400) return std::to_string(s / 3600) + " h ago";
        if (s < 86400 * 30) return std::to_string(s / 86400) + " d ago";
        return std::to_string(s / (86400 * 30)) + " mo ago";
    }

    // The list follows the configs folder live: files dropped in, renamed or deleted show up
    // here on their own, and the loaded config is reapplied when its file is replaced.
    void PageConfig()
    {
        static char config_name[64] = "";
        static std::string selected;

        const std::vector<config::config_info_t>& configs = config::list();
        const config::config_info_t* sel = nullptr;
        for (const auto& c : configs)
            if (c.name == selected)
                sel = &c;
        if (!sel)
            selected.clear();

        ui::BeginPage("config");
        ui::BeginCard("Save");
        if (ui::InputText("##config_name", config_name, sizeof(config_name), "Config name"))
            selected.clear();
        ui::Spacing(10.0f);
        const bool overwrite = config_name[0] != '\0' && config::config_exists(config_name);
        ImGui::BeginDisabled(config_name[0] == '\0');
        if (ui::Button(overwrite ? "Overwrite config" : "Save config", ui::Btn::Primary) && config::save_config(config_name))
        {
            config::watch(true);
            selected = config::loaded();
            memset(config_name, 0, sizeof(config_name));
        }
        ImGui::EndDisabled();
        ui::EndCard();

        ui::BeginCard("Configs");
        if (configs.empty())
            ui::Empty("No configs yet");
        for (const auto& c : configs)
        {
            const bool is_loaded = config::loaded() == c.name;
            const bool is_autoload = external_config::autoload_config == c.name;
            const std::string detail = is_loaded ? "Loaded" : is_autoload ? "On startup" : Age(c.modified);
            ImGui::PushID(c.name.c_str());
            if (ui::ListItem(c.name.c_str(), selected == c.name, detail.c_str()))
            {
                selected = c.name;
                strncpy_s(config_name, sizeof(config_name), c.name.c_str(), _TRUNCATE);
            }
            ImGui::PopID();
        }
        if (config::last_message_time() < 6.0)
            ui::Text(config::last_message().c_str());
        ui::EndCard();

        ui::BeginCard("Selected");
        ui::Value("Config", sel ? sel->name.c_str() : "None", sel ? ui::col::text : ui::col::text_faint);
        ui::Spacing(10.0f);
        ImGui::BeginDisabled(!sel);
        const float half = (ui::RowWidth() - 8.0f) * 0.5f;
        if (ui::Button("Load", ui::Btn::Primary, half) && sel)
            config::load_config(sel->name);
        ImGui::SameLine(0.0f, 8.0f);
        if (ui::Button("Delete", ui::Btn::Danger, half) && sel && config::delete_config(sel->name))
        {
            if (external_config::autoload_config == selected)
            {
                external_config::autoload_config.clear();
                external_config::save();
            }
            selected.clear();
            memset(config_name, 0, sizeof(config_name));
            config::watch(true);
        }
        ui::Spacing(10.0f);
        {
            const bool is_autoload = sel && external_config::autoload_config == sel->name;
            if (ui::Button(is_autoload ? "Don't load on startup" : "Load on startup") && sel)
            {
                external_config::autoload_config = is_autoload ? "" : sel->name;
                external_config::save();
            }
        }
        ImGui::EndDisabled();
        ui::EndCard();

        ui::BeginCard("Folder");
        ui::Value("On startup", external_config::autoload_config.empty() ? "None" : external_config::autoload_config.c_str(),
            external_config::autoload_config.empty() ? ui::col::text_faint : ui::col::text);
        ui::Text("New .json files in the configs folder show up here on their own.");
        ui::Spacing(6.0f);
        const float half2 = (ui::RowWidth() - 8.0f) * 0.5f;
        if (ui::Button("Open folder", ui::Btn::Secondary, half2))
            config::open_file_location();
        ImGui::SameLine(0.0f, 8.0f);
        if (ui::Button("Refresh", ui::Btn::Secondary, half2))
            config::watch(true);
        ui::Spacing(10.0f);
        if (ui::Button("Reset to defaults", ui::Btn::Danger))
            config::reset_to_defaults();
        ui::EndCard();
        ui::EndPage();
    }

    // ---- shell -------------------------------------------------------------------

    void DrawSidebar(ImDrawList* dl, const ImRect& sb)
    {
        dl->AddLine(ImVec2(sb.Max.x, sb.Min.y + kPad), ImVec2(sb.Max.x, sb.Max.y - kPad), ui::col::line);

        if (s_page == PageSilentAim && !SilentAimAvailable())
            s_page = PageAimbot;

        const float x0 = sb.Min.x + 10.0f, x1 = sb.Max.x - 10.0f;
        float item_y[PageCount] = {};
        dl->AddText(ui::Brand(), 18.0f, ImVec2(sb.Min.x + 22, sb.Min.y + 20), ui::col::white, "natware");
        float y = sb.Min.y + kPad + 48.0f;
        for (int i = 0; i < PageCount; i++)
        {
            item_y[i] = -1.0f;
            if (i == PageSilentAim && !SilentAimAvailable())
                continue;
            if (kNav[i].group_start && i != 0)
                y += kGroupGap;
            item_y[i] = y;
            y += kNavStep;
        }

        // One glass pane marks the page and glides to the new tab on the spring (offset from
        // the window, so dragging the menu doesn't make it trail).
        static float pane_y = -1.0f, pane_v = 0.0f;
        const float pane_target = item_y[s_page] - sb.Min.y;
        if (pane_y < 0.0f)
            pane_y = pane_target;
        pane_y = ui::Spring(pane_y, pane_v, pane_target, 0.28f);
        ui::veil::Glass(dl, ImVec2(x0, sb.Min.y + pane_y), ImVec2(x1, sb.Min.y + pane_y + kNavH), 8.0f);

        for (int i = 0; i < PageCount; i++)
        {
            if (item_y[i] < 0.0f)
                continue;
            if (ui::NavItem(kNav[i].label, kNav[i].icon, s_page == i, ImVec2(x0, item_y[i]), ImVec2(x1, item_y[i] + kNavH), false))
                s_page = i;
        }

        y += kGroupGap;
        if (ui::NavItem("Explorer", ui::Icon::Layers, s_explorer_open, ImVec2(x0, y), ImVec2(x1, y + kNavH)))
            s_explorer_open = !s_explorer_open;
        y += kNavStep;
        if (ui::NavItem("Player list", ui::Icon::List, s_playerlist_open, ImVec2(x0, y), ImVec2(x1, y + kNavH)))
            s_playerlist_open = !s_playerlist_open;

        // Account, pinned to the bottom: the in-game local player, or else the account the Roblox
        // app is signed in with, with its avatar headshot.
        std::string display, user;
        uint64_t player = 0;
        {
            std::lock_guard<std::mutex> lock(cache::mtx);
            if (cache::cached_local_player.instance.address != 0)
            {
                display = cache::cached_local_player.display_name;
                user = cache::cached_local_player.name;
                player = cache::cached_local_player.instance.address;
            }
        }
        static uint64_t read_for = 0, user_id = 0;
        if (player != read_for)
        {
            read_for = player;
            user_id = player ? memory->read<uint64_t>(player + Offsets::Player::UserId) : 0;
        }
        ui::profile::SetInGame(user_id, display, user);
        const ui::profile::Account account = ui::profile::Current();
        const bool known = !account.username.empty();

        const float cy = sb.Max.y - kPad - kFooterH * 0.5f;
        const ImVec2 avatar(x0 + 18.0f, cy);
        constexpr float kAvatarR = 15.0f;
        dl->AddCircleFilled(avatar, kAvatarR, IM_COL32(255, 255, 255, 26), 0);
        if (const ImTextureID photo = ui::profile::Avatar())
        {
            const int alpha = (int)(ui::Appear(ImHashStr("##avatar_photo")) * 255.0f + 0.5f);
            dl->AddImageRounded(photo, ImVec2(avatar.x - kAvatarR, avatar.y - kAvatarR), ImVec2(avatar.x + kAvatarR, avatar.y + kAvatarR),
                ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), IM_COL32(255, 255, 255, alpha), kAvatarR);
        }
        else if (known)
        {
            const std::string& name = account.display_name.empty() ? account.username : account.display_name;
            const char initial[2] = { (char)toupper((unsigned char)name[0]), '\0' };
            const ImVec2 is = ui::SemiBold()->CalcTextSizeA(14.0f, FLT_MAX, 0.0f, initial);
            dl->AddText(ui::SemiBold(), 14.0f, ImVec2(floorf(avatar.x - is.x * 0.5f + 0.5f), floorf(avatar.y - 8.0f + 0.5f)), ui::col::white, initial);
        }
        else
        {
            ui::DrawIcon(dl, ui::Icon::User, avatar, ui::col::text_dim, 1.0f);
        }

        const float tx = avatar.x + 25.0f;
        const std::string title = known ? (account.display_name.empty() ? account.username : account.display_name) : "Not signed in";
        const std::string sub = known ? "@" + account.username : "Waiting for Roblox";
        ImGui::PushClipRect(ImVec2(tx, cy - 24.0f), ImVec2(x1, cy + 24.0f), true);
        dl->AddText(ui::SemiBold(), 15.0f, ImVec2(tx, cy - 19.0f), ui::col::text, title.c_str());
        dl->AddText(ui::Regular(), 14.0f, ImVec2(tx, cy + 1.0f), ui::col::text_faint, sub.c_str());
        ImGui::PopClipRect();
    }

    // Centred on the display, on whole pixels: half-pixel window positions blur every 1px line
    // and soften all text by a subpixel.
    ImVec2 CenteredPos(ImVec2 size, ImVec2 offset = ImVec2(0.0f, 0.0f))
    {
        const ImGuiIO& io = ImGui::GetIO();
        return ImVec2(floorf((io.DisplaySize.x - size.x) * 0.5f + offset.x), floorf((io.DisplaySize.y - size.y) * 0.5f + offset.y));
    }

    // The text-top y that centres a reference glyph's ink on `cy`: 'n' centres a lowercase word
    // on its x-height, '0' a line of figures and capitals. Line metrics put very differently
    // designed fonts (Monocraft's tall ascender) visibly off-centre.
    float InkCentredTop(ImFont* font, float size, ImWchar ref, float cy)
    {
        if (ImFontBaked* baked = font->GetFontBaked(size))
            if (const ImFontGlyph* g = baked->FindGlyph(ref))
                return floorf(cy - (g->Y0 + g->Y1) * 0.5f + 0.5f);
        return floorf(cy - size * 0.5f);
    }

    // Dragging can also leave a window on a fraction of a pixel.
    void SnapWindowToPixels()
    {
        const ImVec2 pos = ImGui::GetWindowPos();
        const ImVec2 snapped(floorf(pos.x + 0.5f), floorf(pos.y + 0.5f));
        if (snapped.x != pos.x || snapped.y != pos.y)
            ImGui::SetWindowPos(snapped);
    }

    void DrawMainWindow()
    {
        const ImVec2 size(kWindowW, WindowHeight());
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::SetNextWindowPos(CenteredPos(size), ImGuiCond_Once);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        const bool visible = ImGui::Begin("##natware_menu", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse
            | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
            | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);
        ImGui::PopStyleVar(2);
        if (!visible)
        {
            ImGui::End();
            return;
        }
        SnapWindowToPixels();

        ImGuiWindow* window = ImGui::GetCurrentWindow();
        ImDrawList* dl = window->DrawList;
        const ImRect wbb = window->Rect();

        const ImTextureID veil = ui::veil::Render(wbb.Min, wbb.GetSize());
        if (veil)
            dl->AddImageRounded(veil, wbb.Min, wbb.Max, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), IM_COL32_WHITE, kShellR);
        else
            dl->AddRectFilled(wbb.Min, wbb.Max, ui::col::shell, kShellR);
        dl->AddRect(wbb.Min, wbb.Max, IM_COL32(255, 255, 255, 20), kShellR);

        const ImRect sidebar(wbb.Min, ImVec2(wbb.Min.x + kSidebarW, wbb.Max.y));
        DrawSidebar(dl, sidebar);

        // Page entrance (React Bits Animated Content): fade in while rising 20px into place, on
        // the house spring (eases at both ends, ~0.7 s) rather than a 0.5 s power3.out tween.
        static int shown_page = -1;
        static double shown_at = 0.0;
        if (s_page != shown_page)
        {
            shown_page = s_page;
            shown_at = ImGui::GetTime();
        }
        const float eased = ui::SpringIn((float)(ImGui::GetTime() - shown_at), 0.30f);

        // The scroll area runs kScrollGutter past the cards' right edge (that strip holds the
        // scroll indicator), so the cards themselves sit kContentPadX from the shell.
        dl->AddText(ui::SemiBold(), 24.0f, ImVec2(sidebar.Max.x + kContentPadX, wbb.Min.y + 22), ui::col::text, kNav[s_page].label);
        dl->AddLine(ImVec2(sidebar.Max.x + kContentPadX, wbb.Min.y + 63), ImVec2(wbb.Max.x - kContentPadX, wbb.Min.y + 63), ui::col::line);
        const float cx = sidebar.Max.x + kContentPadX;
        ImGui::SetCursorScreenPos(ImVec2(cx, wbb.Min.y + kContentPadTop + (1.0f - eased) * 20.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, eased);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::BeginChild("##content", ImVec2(wbb.Max.x - kContentPadX + kScrollGutter - cx, wbb.GetHeight() - kContentPadTop - kContentPadBottom),
            ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar();
        switch (s_page)
        {
        case PageAimbot:      DrawAimbotPage(); break;
        case PageSilentAim:   DrawSilentAimPage(); break;
        case PageCombatTab:   PageCombat(); break;
        case PageMovementTab: PageMovement(); break;
        case PageVisualsTab:  PageVisuals(); break;
        case PageWorldTab:    PageWorld(); break;
        case PageSettingsTab: PageSettings(); break;
        case PageConfigs:     PageConfig(); break;
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::End();
    }

    // ---- tool windows ------------------------------------------------------------

    // Shell + title bar with a close button. Returns false (window already ended) when hidden.
    bool BeginToolWindow(const char* id, const char* title, ui::Icon icon, bool* open, ImVec2 size, ImVec2 offset)
    {
        ImGui::SetNextWindowSize(size, ImGuiCond_Always);
        ImGui::SetNextWindowPos(CenteredPos(size, offset), ImGuiCond_Once);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        const bool visible = ImGui::Begin(id, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse
            | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
            | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);
        ImGui::PopStyleVar(2);
        if (!visible)
        {
            ImGui::End();
            return false;
        }
        SnapWindowToPixels();

        ImGuiWindow* window = ImGui::GetCurrentWindow();
        ImDrawList* dl = window->DrawList;
        const ImRect wbb = window->Rect();
        // The menu's own look: a Dark Veil of its own, opaque, under glass panels.
        if (const ImTextureID veil = ui::veil::Render(wbb.Min, wbb.GetSize()))
            dl->AddImageRounded(veil, wbb.Min, wbb.Max, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), IM_COL32_WHITE, kShellR);
        else
            dl->AddRectFilled(wbb.Min, wbb.Max, ui::col::shell, kShellR);
        dl->AddRect(wbb.Min, wbb.Max, IM_COL32(255, 255, 255, 20), kShellR);

        // A tighter frame than the menu's (the user found these windows too spacious): a 44px
        // title bar, 12px padding, and compact rows, buttons and gaps inside.
        constexpr float kBarH = 44.0f, kToolPad = 12.0f;
        const float bar_cy = wbb.Min.y + kBarH * 0.5f;
        ui::DrawIcon(dl, icon, ImVec2(wbb.Min.x + kToolPad + 9.0f, bar_cy), ui::col::text, 0.9f);
        dl->AddText(ui::SemiBold(), 15.0f, ImVec2(wbb.Min.x + kToolPad + 26.0f, InkCentredTop(ui::SemiBold(), 15.0f, '0', bar_cy)), ui::col::white, title);

        const ImRect close(ImVec2(wbb.Max.x - kToolPad - 26.0f, bar_cy - 13.0f), ImVec2(wbb.Max.x - kToolPad, bar_cy + 13.0f));
        const ImGuiID close_id = window->GetID("##close");
        ImGui::ItemAdd(close, close_id);
        bool hovered = false, held = false;
        if (ImGui::ButtonBehavior(close, close_id, &hovered, &held))
            *open = false;
        if (hovered)
            dl->AddRectFilled(close.Min, close.Max, IM_COL32(255, 255, 255, 16), 6.0f);
        ui::DrawIcon(dl, ui::Icon::Close, close.GetCenter(), hovered ? ui::col::white : ui::col::text_dim, 0.9f);

        dl->AddLine(ImVec2(wbb.Min.x + kToolPad, wbb.Min.y + kBarH), ImVec2(wbb.Max.x - kToolPad, wbb.Min.y + kBarH), ui::col::line);

        ImGui::SetCursorScreenPos(ImVec2(wbb.Min.x + kToolPad, wbb.Min.y + kBarH + 10.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::BeginChild("##body", ImVec2(wbb.GetWidth() - kToolPad - 6.0f, wbb.GetHeight() - kBarH - 10.0f - kToolPad), ImGuiChildFlags_None,
            ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar();
        ui::SetCompact(true);
        return true;
    }

    void EndToolWindow()
    {
        ui::SetCompact(false);
        ImGui::EndChild();
        ImGui::End();
    }

    // A glass scrolling panel for content drawn by ImGui's own widgets (the explorer tree).
    template <typename Fn>
    void ToolPanel(const char* id, ImVec2 size, Fn&& body)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ui::veil::Glass(dl, p, ImVec2(p.x + size.x, p.y + size.y), 10.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
        if (ImGui::BeginChild(id, size, ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoBackground))
            body();
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }

    void DrawExplorer()
    {
        if (!BeginToolWindow("##explorer", "Explorer", ui::Icon::Layers, &s_explorer_open, ImVec2(640.0f, 460.0f), ImVec2(0.0f, 0.0f)))
            return;

        if (explorer::explorer)
            explorer::explorer->render_settings();   // the Refresh button

        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float props_h = 112.0f;
        const float spacing = ImGui::GetStyle().ItemSpacing.y;

        ToolPanel("##tree", ImVec2(avail.x - 8.0f, avail.y - props_h - spacing), []
        {
            if (explorer::explorer && !explorer::explorer->is_refreshing.load())
            {
                if (explorer::explorer->root)
                {
                    try { explorer::explorer->render_node(explorer::explorer->root); }
                    catch (...) { ImGui::TextDisabled("Error rendering explorer tree"); }
                }
                else
                    ImGui::TextDisabled("No game instance found");
            }
            else if (explorer::explorer && explorer::explorer->is_refreshing.load())
                ImGui::TextDisabled("Refreshing...");
            else
                ImGui::TextDisabled("Explorer isn't running - join a game first");
        });

        ToolPanel("##props", ImVec2(avail.x - 8.0f, props_h), []
        {
            if (explorer::explorer && !explorer::explorer->is_refreshing.load())
            {
                try { explorer::explorer->render_properties(); }
                catch (...) { ImGui::TextDisabled("Error rendering explorer settings/properties"); }
            }
            else if (explorer::explorer && explorer::explorer->is_refreshing.load())
                ImGui::TextDisabled("Refreshing...");
            else
                ImGui::TextDisabled("Select an instance to see its properties");
        });

        EndToolWindow();
    }

    void DrawPlayerList()
    {
        namespace pl = settings::rage::playerlist;

        if (!s_playerlist_open)
        {
            pl::enabled = false;
            return;
        }
        pl::enabled = true;
        if (!BeginToolWindow("##playerlist", "Player list", ui::Icon::List, &s_playerlist_open, ImVec2(640.0f, 420.0f), ImVec2(50.0f, 0.0f)))
            return;

        std::vector<cache::entity_t> players_snapshot;
        cache::entity_t local_snapshot;
        {
            std::lock_guard<std::mutex> lock(cache::mtx);
            players_snapshot = cache::cached_players;
            local_snapshot = cache::cached_local_player;
        }

        if (!pl::selected_name.empty())
        {
            bool resolved = false;
            for (const auto& e : players_snapshot)
            {
                if (e.name == pl::selected_name)
                {
                    pl::selected_address = e.instance.address;
                    resolved = true;
                    break;
                }
            }
            if (!resolved)
                pl::selected_address = 0;
        }

        if (pl::is_spectating && !pl::spectate_target_name.empty() && game::camera != 0)
        {
            for (const auto& e : players_snapshot)
            {
                if (e.name == pl::spectate_target_name)
                {
                    auto hi = e.parts.find("Head");
                    if (hi == e.parts.end() || !hi->second.address)
                        hi = e.parts.find("HumanoidRootPart");
                    if (hi != e.parts.end() && hi->second.address)
                    {
                        try { memory->write<std::uint64_t>(game::camera + Offsets::Camera::CameraSubject, hi->second.address); } catch (...) {}
                    }
                    break;
                }
            }
        }

        ui::BeginColumns("playerlist");
        ui::BeginCard("Players");
        int shown = 0;
        for (int i = 0; i < static_cast<int>(players_snapshot.size()); i++)
        {
            const auto& entity = players_snapshot[i];
            if (entity.instance.address == local_snapshot.instance.address)
                continue;
            shown++;

            const bool wl = pl::whitelist.count(entity.name) > 0;
            const bool is_tgt = !pl::target_name.empty() && entity.name == pl::target_name;
            const bool is_selected = !pl::selected_name.empty() && entity.name == pl::selected_name;
            char detail[64];
            snprintf(detail, sizeof(detail), "%s%.0f HP", is_tgt ? "Target \xC2\xB7 " : (wl ? "Whitelisted \xC2\xB7 " : ""), entity.health);
            const std::string& label = entity.display_name.empty() ? entity.name : entity.display_name;

            ImGui::PushID(i);
            if (ui::ListItem(label.c_str(), is_selected, detail, is_tgt ? ui::col::danger : ui::col::text))
            {
                pl::selected_index = i;
                pl::selected_address = entity.instance.address;
                pl::selected_name = entity.name;
            }
            ImGui::PopID();
        }
        if (shown == 0)
            ui::Empty("No other players in this server");
        ui::EndCard();


        cache::entity_t sel{};
        bool found = false;
        if (!pl::selected_name.empty())
        {
            std::lock_guard<std::mutex> lock(cache::mtx);
            for (const auto& e : cache::cached_players)
            {
                if (e.name == pl::selected_name)
                {
                    sel = e;
                    found = true;
                    break;
                }
            }
        }

        ui::NextColumn();
        ui::BeginCard("Selected player");
        if (!found)
        {
            ui::Empty("Pick a player from the list");
        }
        else
        {
            math::vector3 tp{};
            bool has_tp = false;
            {
                auto rit = sel.parts.find("HumanoidRootPart");
                if (rit != sel.parts.end() && rit->second.address)
                {
                    try
                    {
                        rbx::primitive_t p = rit->second.get_primitive();
                        if (p.address) { tp = p.get_position(); has_tp = true; }
                    }
                    catch (...) {}
                }
            }

            const bool wl = pl::whitelist.count(sel.name) > 0;
            const bool is_targeted = !pl::target_name.empty() && sel.name == pl::target_name;

            char buf[96];
            ui::Value("Display name", sel.display_name.c_str());
            snprintf(buf, sizeof(buf), "@%s", sel.name.c_str());
            ui::Value("Username", buf);
            snprintf(buf, sizeof(buf), "%.0f / %.0f", sel.health, sel.max_health);
            ui::Value("Health", buf);
            if (has_tp)
            {
                snprintf(buf, sizeof(buf), "%.0f, %.0f, %.0f", tp.x, tp.y, tp.z);
                ui::Value("Position", buf);
            }
            ui::Value("Status", is_targeted ? "Targeted" : (wl ? "Whitelisted" : "-"),
                is_targeted ? ui::col::danger : ui::col::text);
            ui::Spacing(10.0f);

            if (ui::Button(is_targeted ? "Untarget" : "Target", ui::Btn::Primary))
            {
                if (is_targeted)
                {
                    pl::target_name.clear();
                    pl::target_address = 0;
                }
                else
                {
                    pl::target_name = sel.name;
                    pl::target_address = sel.instance.address;
                }
            }
            ui::Spacing(10.0f);

            if (ui::Button(wl ? "Remove from whitelist" : "Whitelist"))
            {
                if (wl) pl::whitelist.erase(sel.name);
                else pl::whitelist.insert(sel.name);
            }
            ui::Spacing(10.0f);

            const float half = (ui::RowWidth() - 8.0f) * 0.5f;
            ImGui::BeginDisabled(!has_tp);
            if (ui::Button("Teleport to", ui::Btn::Secondary, pl::has_saved_position ? half : 0.0f) && has_tp)
            {
                try
                {
                    std::lock_guard<std::mutex> lock(cache::mtx);
                    auto ri = cache::cached_local_player.parts.find("HumanoidRootPart");
                    if (ri != cache::cached_local_player.parts.end() && ri->second.address)
                    {
                        uintptr_t lp = memory->read<uintptr_t>(ri->second.address + Offsets::BasePart::Primitive);
                        if (lp)
                        {
                            pl::saved_position = memory->read<math::vector3>(lp + Offsets::Primitive::Position);
                            pl::has_saved_position = true;
                            math::vector3 t = tp;
                            t.x += 3.0f;
                            for (int w = 0; w < 10000; w++)
                            {
                                memory->write<math::vector3>(lp + Offsets::Primitive::Position, t);
                                memory->write<math::vector3>(lp + Offsets::Primitive::AssemblyLinearVelocity, math::vector3(0, 0, 0));
                            }
                        }
                    }
                }
                catch (...) {}
            }
            ImGui::EndDisabled();

            if (pl::has_saved_position)
            {
                ImGui::SameLine(0.0f, 8.0f);
                if (ui::Button("Teleport back", ui::Btn::Secondary, half))
                {
                    try
                    {
                        std::lock_guard<std::mutex> lock(cache::mtx);
                        auto ri = cache::cached_local_player.parts.find("HumanoidRootPart");
                        if (ri != cache::cached_local_player.parts.end() && ri->second.address)
                        {
                            uintptr_t lp = memory->read<uintptr_t>(ri->second.address + Offsets::BasePart::Primitive);
                            if (lp)
                            {
                                for (int w = 0; w < 10000; w++)
                                {
                                    memory->write<math::vector3>(lp + Offsets::Primitive::Position, pl::saved_position);
                                    memory->write<math::vector3>(lp + Offsets::Primitive::AssemblyLinearVelocity, math::vector3(0, 0, 0));
                                }
                                pl::has_saved_position = false;
                            }
                        }
                    }
                    catch (...) {}
                }
            }
            ui::Spacing(10.0f);

            const bool is_spec = pl::is_spectating;
            if (ui::Button(is_spec ? "Stop spectating" : "Spectate"))
            {
                if (is_spec)
                {
                    if (game::camera != 0 && pl::original_camera_subject != 0)
                        memory->write<std::uint64_t>(game::camera + Offsets::Camera::CameraSubject, pl::original_camera_subject);
                    pl::is_spectating = false;
                    pl::spectate_target_name.clear();
                    pl::original_camera_subject = 0;
                }
                else if (game::camera != 0)
                {
                    pl::original_camera_subject = memory->read<std::uint64_t>(game::camera + Offsets::Camera::CameraSubject);
                    pl::spectate_target_name = sel.name;
                    pl::is_spectating = true;
                }
            }
        }
        ui::EndCard();
        ui::EndColumns();

        EndToolWindow();
        if (!s_playerlist_open)
            pl::enabled = false;
    }

    void DrawTargetLeaveNotification()
    {
        static std::string notif_text;
        static float notif_timer = 0.0f;
        static bool was_target_present = false;
        constexpr float kDuration = 4.0f;

        if (!settings::rage::playerlist::target_name.empty())
        {
            bool target_found = false;
            {
                std::lock_guard<std::mutex> lock(cache::mtx);
                for (const auto& e : cache::cached_players)
                {
                    if (e.name == settings::rage::playerlist::target_name)
                    {
                        target_found = true;
                        break;
                    }
                }
            }

            if (!target_found && was_target_present)
            {
                notif_text = settings::rage::playerlist::target_name + " left the server";
                notif_timer = kDuration;
                settings::rage::playerlist::target_name.clear();
                settings::rage::playerlist::target_address = 0;
            }
            was_target_present = target_found;
        }
        else
        {
            was_target_present = false;
        }

        if (notif_timer <= 0.0f)
            return;

        const ImGuiIO& io = ImGui::GetIO();
        notif_timer -= io.DeltaTime;
        const float alpha = ImClamp(notif_timer, 0.0f, 1.0f);
        auto a = [&](ImU32 c) { return (c & ~IM_COL32_A_MASK) | ((ImU32)((float)((c >> IM_COL32_A_SHIFT) & 0xFF) * alpha) << IM_COL32_A_SHIFT); };

        ImFont* f = ui::SemiBold();
        const ImVec2 ts = f->CalcTextSizeA(13.5f, FLT_MAX, 0.0f, notif_text.c_str());
        const float w = ts.x + 52.0f, h = 42.0f;
        const ImVec2 p0(io.DisplaySize.x - w - 20.0f, 60.0f), p1(p0.x + w, p0.y + h);

        ImDrawList* dl = ImGui::GetForegroundDrawList();
        dl->AddRectFilled(p0, p1, a(ui::col::shell), 10.0f);
        dl->AddRect(p0, p1, a(IM_COL32(255, 255, 255, 24)), 10.0f);
        dl->AddCircleFilled(ImVec2(p0.x + 20.0f, p0.y + h * 0.5f), 4.0f, a(ui::col::danger));
        dl->AddText(f, 14.5f, ImVec2(p0.x + 34.0f, p0.y + h * 0.5f - 7.25f), a(ui::col::text), notif_text.c_str());
    }
}

const std::vector<Menu::PreviewPage>& Menu::PreviewPages()
{
    static const std::vector<PreviewPage> pages = {
        { "aimbot",   PageAimbot,      0 }, { "silent_aim", PageSilentAim,   0 }, { "combat", PageCombatTab, 0 },
        { "movement", PageMovementTab, 0 }, { "visuals",    PageVisualsTab,  0 },
        { "world",    PageWorldTab,    0 }, { "settings",   PageSettingsTab, 0 }, { "configs", PageConfigs, 0 },
        { "tool_playerlist", kPreviewPlayerList, 0 }, { "tool_explorer", kPreviewExplorer, 0 },
        // Interaction shots on the 1280x760 preview canvas (menu centred, top edge at y=94).
        { "x_slider_hover",   PageAimbot, 0, 700.0f, 379.0f, -1 },
        { "x_slider_stretch", PageAimbot, 0, 900.0f, 379.0f, 0, 1060.0f },
        { "x_combo_open",     PageAimbot, 0, 926.0f, 420.0f, 0 },
        { "x_key_mode_menu",  PageAimbot, 0, 925.0f, 222.0f, 1 },
        { "x_spotlight_row",  PageAimbot, 0, 600.0f, 266.0f, -1 },
        // From Aimbot straight to World, caught mid-glide.
        { "x_tab_glide",      PageWorldTab, 0, -1.0f, -1.0f, -1, -1.0f, 10, 9 },
        { "x_click_spark",    PageVisualsTab, 0, 640.0f, 266.0f, 0, -1.0f, 38 },
        // Closing, caught halfway through the fade.
        { "x_menu_closing",   PageAimbot, 0, -1.0f, -1.0f, -1, -1.0f, 10, 7, true },
    };
    return pages;
}

void Menu::PreviewSelect(const PreviewPage& page)
{
    s_explorer_open = page.tab == kPreviewExplorer;
    s_playerlist_open = page.tab == kPreviewPlayerList;
    if (page.tab >= 0 && page.tab < PageCount)
        s_page = page.tab;
}

bool Menu::Initialize(HWND hWnd, ID3D11Device* pDevice, ID3D11DeviceContext* pDeviceContext)
{
    IMGUI_CHECKVERSION();
    if (!ImGui::GetCurrentContext())
        ImGui::CreateContext();

    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ui::ApplyStyle();
    ui::LoadFonts(esp_font_arial);
    ui::veil::Init(pDevice, pDeviceContext);
    ui::profile::Init(pDevice);

    if (!ImGui_ImplWin32_Init(hWnd))
        return false;
    if (!ImGui_ImplDX11_Init(pDevice, pDeviceContext))
        return false;

    m_bInitialized = true;
    return true;
}


void Menu::DrawWatermark()
{
    if (!settings::menu::watermark)
        return;

    // Segments: the name, then icon + text pieces, split by hairlines.
    struct Segment { bool brand; ui::Icon icon; std::string text; };
    std::vector<Segment> segments;
    auto append = [&](ui::Icon icon, const std::string& s) {
        if (!s.empty())
            segments.push_back({ false, icon, s });
    };

    if (settings::watermark::show_cheat_name)
        segments.push_back({ true, ui::Icon::User, external_config::cheat_name });

    if (settings::watermark::show_display_name || settings::watermark::show_username)
    {
        std::string player_str;
        if (cache::cached_local_player.instance.address != 0)
        {
            if (settings::watermark::show_display_name && !cache::cached_local_player.display_name.empty())
                player_str = cache::cached_local_player.display_name;
            if (settings::watermark::show_username && !cache::cached_local_player.name.empty())
            {
                if (!player_str.empty())
                    player_str += " (@" + cache::cached_local_player.name + ")";
                else
                    player_str = "@" + cache::cached_local_player.name;
            }
        }
        append(ui::Icon::User, player_str);
    }

    if (settings::watermark::show_fps)
        append(ui::Icon::Activity, std::to_string(static_cast<int>(ImGui::GetIO().Framerate)) + " fps");

    if (settings::watermark::show_server_ip)
    {
        try
        {
            if (game::datamodel.address)
            {
                std::string ip = memory->read_string(game::datamodel.address + Offsets::DataModel::ServerIP);
                if (!ip.empty() && ip != "Unknown")
                {
                    for (auto& c : ip)
                        if (c == '|') c = ':';
                    append(ui::Icon::Server, ip);
                }
            }
        }
        catch (...) {}
    }

    if (segments.empty())
        segments.push_back({ true, ui::Icon::User, external_config::cheat_name });

    ImU32 text_col;
    if (settings::watermark::rainbow)
    {
        float t = static_cast<float>(ImGui::GetTime()) * settings::watermark::rainbow_speed;
        float r = sinf(t) * 0.5f + 0.5f;
        float g = sinf(t + 2.094f) * 0.5f + 0.5f;
        float b = sinf(t + 4.189f) * 0.5f + 0.5f;
        text_col = IM_COL32(static_cast<int>(r * 255), static_cast<int>(g * 255), static_cast<int>(b * 255), 255);
    }
    else
    {
        text_col = ImGui::ColorConvertFloat4ToU32({
            settings::watermark::text_color[0],
            settings::watermark::text_color[1],
            settings::watermark::text_color[2],
            settings::watermark::text_color[3]
        });
    }

    // The menu's material in miniature: a glass pill over its own Dark Veil. The name is the
    // Monocraft wordmark; the rest is Arial with Lucide icons.
    constexpr float kFontSize = 14.0f;
    constexpr float kHeight   = 36.0f;
    constexpr float kPadX     = 16.0f;
    constexpr float kIconW    = 13.0f;
    constexpr float kIconGap  = 7.0f;
    constexpr float kSplit    = 12.0f;   // space either side of a hairline
    auto font_of = [](const Segment& s) { return s.brand ? ui::Brand() : ui::Regular(); };
    auto size_of = [](const Segment& s) { return s.brand ? ui::kBrandSize : kFontSize; };
    auto width_of = [&](const Segment& s) {
        const float text = font_of(s)->CalcTextSizeA(size_of(s), FLT_MAX, 0.0f, s.text.c_str()).x;
        return s.brand ? text : kIconW + kIconGap + text;
    };
    float wm_width = kPadX * 2.0f + (segments.size() - 1) * (kSplit * 2.0f + 1.0f);
    for (const Segment& s : segments)
        wm_width += width_of(s);
    wm_width = floorf(wm_width + 0.5f);
    const float wm_height = kHeight;

    ImGuiWindowFlags wm_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav;
    if (!m_bMenuVisible)
        wm_flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs;

    ImGui::SetNextWindowSize(ImVec2(wm_width, wm_height), ImGuiCond_Always);
    if (!settings::watermark::pos_initialized)
    {
        ImGui::SetNextWindowPos(ImVec2(settings::watermark::pos_x, settings::watermark::pos_y), ImGuiCond_Always);
        settings::watermark::pos_initialized = true;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    bool wm_open = ImGui::Begin("##Watermark", (bool*)0, wm_flags);
    ImGui::PopStyleVar(2);

    if (wm_open)
    {
        SnapWindowToPixels();
        ImVec2 pos = ImGui::GetWindowPos();
        settings::watermark::pos_x = pos.x;
        settings::watermark::pos_y = pos.y;

        const ImRect bb(ImGui::GetCurrentWindow()->Rect());
        ImDrawList* dl = ImGui::GetCurrentWindow()->DrawList;

        // Its veil is a cut-out of a menu-sized one, so the pattern keeps the menu's scale; the
        // margin covers how far the glass reaches past its edge (distortion / 2).
        constexpr float kReach = 96.0f;
        ui::veil::RenderRegion(ImVec2(bb.Min.x - kReach, bb.Min.y - kReach), ImVec2(bb.GetWidth() + kReach * 2.0f, bb.GetHeight() + kReach * 2.0f),
            ImVec2(820.0f, 530.0f), ImVec2(150.0f, 200.0f));
        ui::veil::Glass(dl, bb.Min, bb.Max, kHeight * 0.5f, 0.15f);

        // The name takes the text colour (or the rainbow) in full; the other pieces are quieter.
        const ImU32 quiet = (text_col & ~IM_COL32_A_MASK) | ((ImU32)(((text_col >> IM_COL32_A_SHIFT) & 0xFF) * 0.8f) << IM_COL32_A_SHIFT);
        const ImU32 icon_col = (text_col & ~IM_COL32_A_MASK) | ((ImU32)(((text_col >> IM_COL32_A_SHIFT) & 0xFF) * 0.6f) << IM_COL32_A_SHIFT);
        const float cy = bb.GetCenter().y;
        const float ty = InkCentredTop(ui::Regular(), kFontSize, '0', cy);
        const float brand_y = InkCentredTop(ui::Brand(), ui::kBrandSize, 'n', cy);
        float x = bb.Min.x + kPadX;
        for (size_t i = 0; i < segments.size(); i++)
        {
            const Segment& s = segments[i];
            if (i > 0)
            {
                x += kSplit;
                dl->AddLine(ImVec2(floorf(x) + 0.5f, cy - 7.0f), ImVec2(floorf(x) + 0.5f, cy + 7.0f), IM_COL32(255, 255, 255, 30));
                x += 1.0f + kSplit;
            }
            if (!s.brand)
            {
                ui::DrawIcon(dl, s.icon, ImVec2(x + kIconW * 0.5f, cy), icon_col, 0.85f);
                x += kIconW + kIconGap;
            }
            dl->AddText(font_of(s), size_of(s), ImVec2(floorf(x + 0.5f), s.brand ? brand_y : ty), s.brand ? text_col : quiet, s.text.c_str());
            x += width_of(s) - (s.brand ? 0.0f : kIconW + kIconGap);
        }
    }
    ImGui::End();
}

namespace
{
    float s_menu_fade = 0.0f, s_menu_fade_v = 0.0f;

    // Fades every window the menu drew this frame (and popups opened from them) as one flattened
    // layer (ui::veil::RenderDrawData), so the panes inside fade with the shell instead of
    // showing through each other. Tool windows also fade in when they open.
    void FadeMenuWindows(float menu_alpha)
    {
        ImGuiWindow* const menu = ImGui::FindWindowByName("##natware_menu");
        ImGuiWindow* const tools[] = { ImGui::FindWindowByName("##explorer"), ImGui::FindWindowByName("##playerlist") };
        for (ImGuiWindow* w : GImGui->Windows)
        {
            if (!w->Active || !w->DrawList)
                continue;
            // RootWindowPopupTree crosses popups to the window that opened them (but stops at a
            // child window); RootWindow then climbs from a child to its top-level window.
            ImGuiWindow* const root = w->RootWindowPopupTree->RootWindow;
            float alpha = menu_alpha;
            if (root == tools[0] || root == tools[1])
                alpha *= ui::Appear(root->ID, 0.0f, 0.14f);
            else if (root != menu)
                continue;
            if (alpha < 0.999f)
                ui::veil::FadeList(w->DrawList, alpha);
        }
    }
}

bool Menu::IsClosing()
{
    return !render->running && s_menu_fade > 0.0f;
}

void Menu::DrawMenu()
{
    m_bMenuVisible = true;

    // Opening fades the menu in and closing fades it out (main.cpp keeps drawing it until the
    // fade ends); nothing is drawn while it's fully closed, so each open starts from zero.
    s_menu_fade = ui::Spring(s_menu_fade, s_menu_fade_v, render->running ? 1.0f : 0.0f, 0.25f);

    config::watch();
    DrawMainWindow();
    if (s_explorer_open)
        DrawExplorer();
    DrawPlayerList();
    DrawTargetLeaveNotification();
    FadeMenuWindows(s_menu_fade);
    if (render->running)
        ui::ClickSparks();
}

void Menu::Shutdown()
{
    if (!m_bInitialized) return;

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    m_bInitialized = false;
}

// The shells' close button (same as the tool windows'): 28px, a faint fill on hover.
static bool ShellCloseButton(ImDrawList* dl, const char* id, ImVec2 min)
{
    const ImRect r(min, ImVec2(min.x + 28.0f, min.y + 28.0f));
    ImGui::SetCursorScreenPos(r.Min);
    const bool pressed = ImGui::InvisibleButton(id, r.GetSize());
    const bool hovered = ImGui::IsItemHovered();
    if (hovered)
    {
        dl->AddRectFilled(r.Min, r.Max, IM_COL32(255, 255, 255, 16), 7.0f);
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    ui::DrawIcon(dl, ui::Icon::Close, r.GetCenter(), hovered ? ui::col::white : ui::col::text_dim, 0.85f);
    return pressed;
}

// Where the launcher and the waiting card sit relative to screen centre, so they can be dragged
// out of the way (by any part that isn't a button) and stay there from one to the other.
static ImVec2 s_card_drag(0.0f, 0.0f);
static bool s_card_dragging = false;

// The overlay covers the whole screen, so while a card shows it only takes the mouse over the
// card (or mid-drag) and is click-through everywhere else: your other apps stay usable.
static void CardInput(const ImRect& card)
{
    settings::globals::card_showing = true;
    if (!render || !render->detail || !render->detail->window)
        return;   // the preview tests draw without an overlay window
    HWND const hwnd = render->detail->window;
    POINT p{};
    GetCursorPos(&p);
    ScreenToClient(hwnd, &p);
    const bool over = s_card_dragging || card.Contains(ImVec2((float)p.x, (float)p.y));
    const LONG_PTR style = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    const LONG_PTR want = over ? (style & ~(LONG_PTR)WS_EX_TRANSPARENT) : (style | WS_EX_TRANSPARENT);
    if (want != style)
        SetWindowLongPtr(hwnd, GWL_EXSTYLE, want);
}

// Call inside the card's window, after its buttons are submitted.
static void DragCard(ImVec2 size)
{
    const ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered())
        s_card_dragging = true;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        s_card_dragging = false;
    if (s_card_dragging)
        s_card_drag = ImVec2(s_card_drag.x + io.MouseDelta.x, s_card_drag.y + io.MouseDelta.y);
    const float rx = (std::max)(0.0f, (io.DisplaySize.x - size.x) * 0.5f);
    const float ry = (std::max)(0.0f, (io.DisplaySize.y - size.y) * 0.5f);
    s_card_drag.x = (std::min)((std::max)(s_card_drag.x, -rx), rx);
    s_card_drag.y = (std::min)((std::max)(s_card_drag.y, -ry), ry);
}

// While natware waits on Roblox after External was picked: there's no console any more, so this
// card says what it's waiting for and can close natware.
void Menu::DrawWaiting(const char* status)
{
    constexpr float kW = 400.0f, kH = 112.0f, kPadX = 24.0f;
    static double opened = ImGui::GetTime();
    const double now = ImGui::GetTime();
    const float enter = ui::SpringIn((float)(now - opened), 0.30f);

    const ImVec2 size(kW, kH);
    ImGui::SetNextWindowSize(size);
    ImGui::SetNextWindowPos(CenteredPos(size, ImVec2(s_card_drag.x, s_card_drag.y + (1.0f - enter) * 18.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##waiting", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
    ImGui::PopStyleVar(2);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImRect bb = ImGui::GetCurrentWindow()->Rect();
    if (enter < 0.999f)
        ui::veil::FadeList(dl, enter);

    if (const ImTextureID veil = ui::veil::Render(bb.Min, bb.GetSize()))
        dl->AddImageRounded(veil, bb.Min, bb.Max, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, kShellR);
    else
        dl->AddRectFilled(bb.Min, bb.Max, ui::col::shell, kShellR);
    dl->AddRect(bb.Min, bb.Max, IM_COL32(255, 255, 255, 20), kShellR);
    dl->AddText(ui::Brand(), ui::kBrandSize, ImVec2(bb.Min.x + kPadX, bb.Min.y + 22.0f), ui::col::white, "natware");

    // A dot that breathes, then the status with a slow ellipsis.
    const float pulse = 0.5f + 0.5f * sinf((float)now * 3.0f);
    const ImVec2 dot(bb.Min.x + kPadX + 4.0f, bb.Min.y + 78.0f);
    dl->AddCircleFilled(dot, 4.0f, IM_COL32(255, 255, 255, (int)(90 + 140 * pulse)), 16);
    char text[96];
    const int dots = (int)(now * 2.0) % 4;
    snprintf(text, sizeof(text), "%s%.*s", status, dots, "...");
    dl->AddText(ui::Regular(), 15.0f, ImVec2(dot.x + 14.0f, dot.y - 9.0f), ui::col::text, text);

    if (ShellCloseButton(dl, "##waiting_close", ImVec2(bb.Max.x - 16.0f - 28.0f, bb.Min.y + 16.0f))
        || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        settings::globals::quit_requested = true;
    DragCard(size);
    CardInput(bb);
    ImGui::End();
}

// A pill button drawn by hand (white for the main choice, glass for the other), for the launcher
// and the update prompt, which aren't laid out like menu pages.
static bool PillButton(ImDrawList* dl, const char* id, const char* label, ImVec2 min, float width, bool primary, bool enabled = true)
{
    constexpr float kH = 28.0f;
    const ImRect r(min, ImVec2(min.x + width, min.y + kH));
    ImGui::SetCursorScreenPos(r.Min);
    const bool pressed = ImGui::InvisibleButton(id, r.GetSize()) && enabled;
    const bool hovered = ImGui::IsItemHovered() && enabled;
    if (hovered)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    const int a = enabled ? 255 : 110;
    if (primary)
        dl->AddRectFilled(r.Min, r.Max, hovered ? IM_COL32(255, 255, 255, a) : IM_COL32(236, 236, 240, a), kH * 0.5f);
    else
    {
        dl->AddRectFilled(r.Min, r.Max, IM_COL32(255, 255, 255, hovered ? 26 : 14), kH * 0.5f);
        dl->AddRect(r.Min, r.Max, IM_COL32(255, 255, 255, 30), kH * 0.5f);
    }
    ImFont* font = ui::Regular();
    const ImVec2 ts = font->CalcTextSizeA(14.0f, FLT_MAX, 0.0f, label);
    dl->AddText(font, 14.0f, ImVec2(floorf(r.GetCenter().x - ts.x * 0.5f), floorf(r.GetCenter().y - ts.y * 0.5f)),
        primary ? IM_COL32(12, 12, 14, a) : (enabled ? ui::col::text : ui::col::text_faint), label);
    return pressed;
}

// The launcher's update rows: one per program natware keeps up to date (natware, the editor and
// its backend), with what's installed and what's out. A row with an update gets a tick box;
// ticked rows go in with the Update button below. Rows divide by hairlines, like a card's.
static constexpr float kUpdateRowH = 30.0f;

static void UpdateRows(ImDrawList* dl, const ImRect& bb, float pad_x, float y, bool busy)
{
    const std::vector<updater::component_t> comps = updater::components();
    const bool checking = updater::status().state == updater::state_t::checking;
    for (size_t i = 0; i < comps.size(); ++i)
    {
        const updater::component_t& c = comps[i];
        const ImVec2 a(bb.Min.x + pad_x, y), b(bb.Max.x - pad_x, y + kUpdateRowH);
        dl->AddLine(ImVec2(a.x, a.y), ImVec2(b.x, a.y), ui::col::line);

        const bool tickable = c.available && c.state != updater::state_t::staged && !busy;
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(c.name.c_str());
        const bool pressed = ImGui::InvisibleButton("##update_row", ImVec2(b.x - a.x, kUpdateRowH)) && tickable;
        const bool hovered = ImGui::IsItemHovered() && tickable;
        const ImGuiID id = ImGui::GetItemID();
        ImGui::PopID();
        if (pressed)
            updater::select(c.name, !c.selected);
        if (hovered)
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

        // The tick fills and empties on the spring.
        ImGuiStorage* storage = ImGui::GetStateStorage();
        const ImGuiID vid = ImHashStr("velocity", 0, id);
        float t = storage->GetFloat(id, c.selected ? 1.0f : 0.0f), tv = storage->GetFloat(vid, 0.0f);
        t = ui::Spring(t, tv, c.selected ? 1.0f : 0.0f, 0.12f);
        storage->SetFloat(id, t);
        storage->SetFloat(vid, tv);

        const float cy = floorf(y + kUpdateRowH * 0.5f);
        float text_x = a.x;
        if (c.available)
        {
            const ImVec2 k0(a.x, cy - 8.0f), k1(a.x + 16.0f, cy + 8.0f);
            const int rim = tickable ? (hovered ? 90 : 60) : 30;
            dl->AddRect(k0, k1, IM_COL32(255, 255, 255, (int)(rim * (1.0f - t))), 5.0f);
            if (t > 0.01f)
            {
                dl->AddRectFilled(k0, k1, IM_COL32(248, 248, 250, (int)(255 * t * (tickable ? 1.0f : 0.5f))), 5.0f);
                ui::DrawIcon(dl, ui::Icon::Check, ImVec2(a.x + 8.0f, (float)cy), IM_COL32(12, 12, 14, (int)(255 * t)), 0.72f);
            }
            text_x = a.x + 28.0f;
        }
        dl->AddText(ui::Regular(), 15.0f, ImVec2(text_x, cy - 8.0f), hovered ? ui::col::white : ui::col::text, c.name.c_str());

        // What it is and what's out, right-aligned.
        const std::string have = c.installed.empty() ? (c.present ? "a local build" : "not installed") : c.installed;
        char detail[160];
        ImU32 color = ui::col::text_dim;
        switch (c.state)
        {
        case updater::state_t::available:
            if (c.present)
                snprintf(detail, sizeof(detail), "%s is out  \xC2\xB7  you have %s", c.latest.c_str(), have.c_str());
            else
                snprintf(detail, sizeof(detail), "%s is out  \xC2\xB7  not installed", c.latest.c_str());
            color = c.selected ? ui::col::text : ui::col::text_dim;
            break;
        case updater::state_t::downloading:
            snprintf(detail, sizeof(detail), "Downloading %s  \xC2\xB7  %d%%", c.latest.c_str(), (int)(c.progress * 100.0f + 0.5f));
            color = ui::col::text;
            break;
        case updater::state_t::staged:
            snprintf(detail, sizeof(detail), "%s goes in when natware closes", c.latest.c_str());
            break;
        case updater::state_t::done:
            snprintf(detail, sizeof(detail), "%s installed", c.installed.c_str());
            color = ui::col::text;
            break;
        case updater::state_t::failed:
            snprintf(detail, sizeof(detail), "%s  \xC2\xB7  %s", have.c_str(), c.message.c_str());
            color = ui::col::text_faint;
            break;
        case updater::state_t::off:
            snprintf(detail, sizeof(detail), "%s", have.c_str());
            if (checking)
                snprintf(detail, sizeof(detail), "%s  \xC2\xB7  checking", have.c_str());
            break;
        default:
            snprintf(detail, sizeof(detail), "%s  \xC2\xB7  %s", have.c_str(), c.message.empty() ? "up to date" : c.message.c_str());
            break;
        }
        ImFont* font = ui::Regular();
        const ImVec2 ts = font->CalcTextSizeA(14.0f, FLT_MAX, 0.0f, detail);
        dl->AddText(font, 14.0f, ImVec2(floorf(b.x - ts.x), cy - 7.0f), color, detail);
        y += kUpdateRowH;
    }
    dl->AddLine(ImVec2(bb.Min.x + pad_x, y), ImVec2(bb.Max.x - pad_x, y), ui::col::line);
}

// The launcher's last line: natware's version and where the updates stand.
static void UpdateFooter(ImDrawList* dl, const ImRect& bb, float pad_x, float y)
{
    const updater::status_t st = updater::status();
    const int pct = (int)(st.progress * 100.0f + 0.5f);
    char line[192];
    switch (st.state)
    {
    case updater::state_t::checking:    snprintf(line, sizeof(line), "natware %s  \xC2\xB7  checking for updates", updater::version()); break;
    case updater::state_t::current:
        if (st.optional)
            snprintf(line, sizeof(line), "natware %s  \xC2\xB7  tick a local build to replace it", updater::version());
        else
            snprintf(line, sizeof(line), "natware %s  \xC2\xB7  everything is up to date", updater::version());
        break;
    case updater::state_t::available:
        if (st.count == 1) snprintf(line, sizeof(line), "1 update is out");
        else               snprintf(line, sizeof(line), "%d updates are out", st.count);
        break;
    case updater::state_t::downloading:
        if (st.count == 1) snprintf(line, sizeof(line), "Downloading %s  \xC2\xB7  %d%%", st.latest.c_str(), pct);
        else               snprintf(line, sizeof(line), "Downloading %d updates  \xC2\xB7  %d%%", st.count, pct);
        break;
    case updater::state_t::installing:
        if (st.count == 1) snprintf(line, sizeof(line), "Installing %s", st.latest.c_str());
        else               snprintf(line, sizeof(line), "Installing %d updates", st.count);
        break;
    case updater::state_t::staged:      snprintf(line, sizeof(line), "%s goes in when natware closes", st.latest.c_str()); break;
    case updater::state_t::done:
        if (st.count == 1) snprintf(line, sizeof(line), "%s is installed", st.latest.c_str());
        else               snprintf(line, sizeof(line), "%d updates are installed", st.count);
        break;
    case updater::state_t::failed:      snprintf(line, sizeof(line), "natware %s  \xC2\xB7  update: %s", updater::version(), st.message.c_str()); break;
    default:                            snprintf(line, sizeof(line), "natware %s", updater::version()); break;
    }
    const ImU32 color = st.state == updater::state_t::available || st.state == updater::state_t::done ? ui::col::text
        : st.state == updater::state_t::failed ? ui::col::text_faint : ui::col::text_dim;
    dl->AddText(ui::Regular(), 14.0f, ImVec2(bb.Min.x + pad_x, y + 6.0f), color, line);

    if (st.state == updater::state_t::available)
    {
        // natware restarts into its new version when it's among them; the others just go in.
        if (PillButton(dl, "##update_now", "Update", ImVec2(bb.Max.x - pad_x - 86.0f, y), 86.0f, true))
            updater::install_async(true);
    }
    else if (st.state == updater::state_t::downloading)
    {
        const ImVec2 a(bb.Max.x - pad_x - 120.0f, y + 12.0f), b(bb.Max.x - pad_x, y + 16.0f);
        dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 22), 2.0f);
        dl->AddRectFilled(a, ImVec2(a.x + (b.x - a.x) * st.progress, b.y), ui::col::white, 2.0f);
    }
}

void Menu::DrawModePicker()
{
    // The launcher wears the menu's shell: its own Dark Veil, the quiet rim, the Monocraft wordmark
    // and two Glass Surface cards with the cards' spotlight. It fades in rising into place, and a
    // choice fades it out (group opacity, like the menu) before the mode switches.
    constexpr float kW = 560.0f, kPadX = 24.0f, kCardY = 104.0f, kCardH = 140.0f, kGap = 12.0f;
    // Below the cards: a row per component when updates are on, then the status line.
    const bool update_rows = updater::status().state != updater::state_t::off;
    const float rows_h = update_rows ? updater::components().size() * kUpdateRowH + 12.0f : 0.0f;
    const float kH = 304.0f + rows_h;
    static double opened = ImGui::GetTime();
    static int chosen = -1;
    static double chosen_at = 0.0;
    const double now = ImGui::GetTime();
    const float enter = ui::SpringIn((float)(now - opened), 0.30f);
    const float leave = chosen >= 0 ? ui::SpringIn((float)(now - chosen_at), 0.12f) : 0.0f;
    if (chosen >= 0 && leave >= 1.0f)
    {
        if (chosen == 2)
            settings::globals::quit_requested = true;
        else
            settings::app_mode = chosen == 0 ? settings::MODE_EXECUTOR : settings::MODE_EXTERNAL;
        return;
    }

    const ImVec2 size(kW, kH);
    ImGui::SetNextWindowSize(size);
    ImGui::SetNextWindowPos(CenteredPos(size, ImVec2(s_card_drag.x, s_card_drag.y + (1.0f - enter) * 18.0f - leave * 8.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##mode_picker", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
    ImGui::PopStyleVar(2);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImRect bb = ImGui::GetCurrentWindow()->Rect();
    const float alpha = enter * (1.0f - leave);
    if (alpha < 0.999f)
        ui::veil::FadeList(dl, alpha);

    if (const ImTextureID veil = ui::veil::Render(bb.Min, bb.GetSize()))
        dl->AddImageRounded(veil, bb.Min, bb.Max, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, kShellR);
    else
        dl->AddRectFilled(bb.Min, bb.Max, ui::col::shell, kShellR);
    dl->AddRect(bb.Min, bb.Max, IM_COL32(255, 255, 255, 20), kShellR);
    dl->AddText(ui::Brand(), ui::kBrandSize, ImVec2(bb.Min.x + kPadX, bb.Min.y + 22.0f), ui::col::white, "natware");
    dl->AddText(ui::Regular(), 22.0f, ImVec2(bb.Min.x + kPadX, bb.Min.y + 58.0f), ui::col::text, "Choose a mode");

    // Close natware from here (or Esc): it fades out like a choice, then the program exits.
    if (chosen < 0 && (ShellCloseButton(dl, "##picker_close", ImVec2(bb.Max.x - 16.0f - 28.0f, bb.Min.y + 16.0f))
            || ImGui::IsKeyPressed(ImGuiKey_Escape, false)))
    {
        chosen = 2;
        chosen_at = now;
    }

    auto mix = [](ImU32 a, ImU32 b, float t)
    {
        return ImGui::ColorConvertFloat4ToU32(ImLerp(ImGui::ColorConvertU32ToFloat4(a), ImGui::ColorConvertU32ToFloat4(b), t));
    };
    struct Mode { const char* label; const char* detail; ui::Icon icon; };
    const Mode modes[2] = {
        { "Editor",   "Write and save scripts",   ui::Icon::Layers },
        { "External", "The in-game overlay menu", ui::Icon::Sliders },
    };
    const float card_w = floorf((kW - kPadX * 2.0f - kGap) * 0.5f);
    // While an update downloads and installs, the modes wait: natware restarts into the new one.
    const updater::state_t update_state = updater::status().state;
    const bool updating = update_state == updater::state_t::downloading || update_state == updater::state_t::installing;
    for (int i = 0; i < 2; i++)
    {
        const Mode& m = modes[i];
        const ImVec2 p(bb.Min.x + kPadX + i * (card_w + kGap), bb.Min.y + kCardY);
        ImGui::SetCursorScreenPos(p);
        ImGui::PushID(i);
        const bool pressed = ImGui::InvisibleButton("##mode", ImVec2(card_w, kCardH));
        const bool hovered = ImGui::IsItemHovered() && chosen < 0 && !updating;
        const ImGuiID id = ImGui::GetItemID();
        ImGui::PopID();
        if (pressed && chosen < 0 && !updating)
        {
            chosen = i;
            chosen_at = now;
        }
        if (hovered)
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

        // Hover lifts the card 3px on the spring, brightens its text and nudges the chevron.
        ImGuiStorage* storage = ImGui::GetStateStorage();
        const ImGuiID vid = ImHashStr("velocity", 0, id);
        float h = storage->GetFloat(id, 0.0f), hv = storage->GetFloat(vid, 0.0f);
        h = ui::Spring(h, hv, (hovered || chosen == i) ? 1.0f : 0.0f, 0.18f);
        storage->SetFloat(id, h);
        storage->SetFloat(vid, hv);

        const ImVec2 q(p.x, p.y - floorf(h * 3.0f + 0.5f));
        const ImVec2 q1(q.x + card_w, q.y + kCardH);
        ui::veil::Glass(dl, q, q1, 12.0f, 0.08f);
        ui::Spotlight(dl, id, q, q1);

        ui::DrawIcon(dl, m.icon, ImVec2(q.x + 30.0f, q.y + 32.0f), mix(ui::col::text_dim, ui::col::white, h), 1.1f);
        // Lucide chevron-right on the icon grid (0.62 px per unit, like ui::DrawIcon).
        const ImVec2 c(q1.x - 28.0f + h * 3.0f, q.y + 32.0f);
        const float u = 0.62f * 0.9f;
        dl->PathLineTo(ImVec2(c.x - 3.0f * u, c.y - 6.0f * u));
        dl->PathLineTo(ImVec2(c.x + 3.0f * u, c.y));
        dl->PathLineTo(ImVec2(c.x - 3.0f * u, c.y + 6.0f * u));
        dl->PathStroke(mix(ui::col::text_faint, ui::col::text, h), ImDrawFlags_None, 1.4f);

        dl->AddText(ui::Regular(), 17.0f, ImVec2(q.x + 20.0f, q.y + kCardH - 58.0f), mix(ui::col::text, ui::col::white, h), m.label);
        dl->AddText(ui::Regular(), 14.0f, ImVec2(q.x + 20.0f, q.y + kCardH - 33.0f), ui::col::text_dim, m.detail);
    }
    if (update_rows)
        UpdateRows(dl, bb, kPadX, bb.Min.y + kCardY + kCardH + 16.0f, updating || chosen >= 0);
    UpdateFooter(dl, bb, kPadX, bb.Min.y + kCardY + kCardH + 16.0f + rows_h);
    if (chosen < 0)
        DragCard(size);
    CardInput(bb);
    ImGui::End();
}

void Menu::DrawUpdatePrompt()
{
    // A release that came out while natware was open. With the menu open you choose: update now
    // (natware restarts into it) or when you close natware (it downloads now and goes in on
    // exit). With the menu closed the overlay can't take clicks, so a quiet note says where to
    // choose. What happens next shows the same way for a few seconds.
    const updater::status_t st = updater::status();
    const double now = ImGui::GetTime();
    static updater::state_t last_state = updater::state_t::off;
    static double changed_at = 0.0;
    static bool asked = false;   // you picked an answer this run
    if (st.state != last_state)
    {
        last_state = st.state;
        changed_at = now;
    }

    auto note = [&](const char* text)
    {
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        ImFont* font = ui::Regular();
        const ImVec2 ts = font->CalcTextSizeA(14.0f, FLT_MAX, 0.0f, text);
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        const ImVec2 a(floorf(display.x * 0.5f - ts.x * 0.5f - 16.0f), 18.0f);
        const ImVec2 b(a.x + ts.x + 32.0f, a.y + 32.0f);
        fg->AddRectFilled(a, b, IM_COL32(10, 10, 10, 230), 16.0f);
        fg->AddRect(a, b, IM_COL32(255, 255, 255, 24), 16.0f);
        fg->AddText(font, 14.0f, ImVec2(a.x + 16.0f, a.y + 16.0f - ts.y * 0.5f), ui::col::text, text);
    };

    // "natware 1.1.0" for one update, "2 updates" for more (the launcher lists them by name).
    char what[128];
    if (st.count == 1)
        snprintf(what, sizeof(what), "%s", st.latest.c_str());
    else
        snprintf(what, sizeof(what), "%d updates", st.count);
    const char* is = st.count == 1 ? "is" : "are";

    char text[192];
    switch (st.state)
    {
    case updater::state_t::available:
        if (asked)
            return;
        break;
    case updater::state_t::downloading:
        if (asked)
        {
            snprintf(text, sizeof(text), "Downloading %s  \xC2\xB7  %d%%", what, (int)(st.progress * 100.0f + 0.5f));
            note(text);
        }
        return;
    case updater::state_t::installing:
        if (st.restarts)
            snprintf(text, sizeof(text), "Installing %s: natware restarts in a moment", what);
        else
            snprintf(text, sizeof(text), "Installing %s", what);
        note(text);
        return;
    case updater::state_t::staged:
        if (now - changed_at < 6.0)
        {
            snprintf(text, sizeof(text), "%s is downloaded and goes in when you close natware", st.latest.c_str());
            note(text);
        }
        return;
    case updater::state_t::done:
        if (asked && now - changed_at < 6.0)
        {
            snprintf(text, sizeof(text), "%s %s installed", what, is);
            note(text);
        }
        return;
    case updater::state_t::failed:
        if (asked && now - changed_at < 8.0)
        {
            snprintf(text, sizeof(text), "The update didn't go in: %s", st.message.c_str());
            note(text);
        }
        return;
    default:
        return;
    }

    if (!render->running)
    {
        snprintf(text, sizeof(text), "%s %s out  \xC2\xB7  open the menu to update", what, is);
        note(text);
        return;
    }

    constexpr float kW = 440.0f, kH = 150.0f, kPadX = 22.0f;
    const ImVec2 size(kW, kH);
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float enter = ui::SpringIn((float)(now - changed_at), 0.25f);
    ImGui::SetNextWindowSize(size);
    ImGui::SetNextWindowPos(ImVec2(floorf(display.x * 0.5f - kW * 0.5f), floorf(24.0f - (1.0f - enter) * 12.0f)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##update_prompt", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::PopStyleVar(2);
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());   // over the menu, without taking its keyboard focus
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImRect bb = ImGui::GetCurrentWindow()->Rect();
    if (enter < 0.999f)
        ui::veil::FadeList(dl, enter);
    dl->AddRectFilled(bb.Min, bb.Max, IM_COL32(10, 10, 10, 245), kShellR);
    dl->AddRect(bb.Min, bb.Max, IM_COL32(255, 255, 255, 24), kShellR);

    snprintf(text, sizeof(text), "%s %s out", what, is);
    dl->AddText(ui::Regular(), 18.0f, ImVec2(bb.Min.x + kPadX, bb.Min.y + 20.0f), ui::col::white, text);
    // First line: what you have (one update) or what's in it (several).
    if (st.count == 1)
        snprintf(text, sizeof(text), "You have %s.", st.have.empty() ? "none installed" : st.have.c_str());
    else
        snprintf(text, sizeof(text), "%s.", st.latest.c_str());
    dl->AddText(ui::Regular(), 14.0f, ImVec2(bb.Min.x + kPadX, bb.Min.y + 50.0f), ui::col::text_dim, text);

    const float y = bb.Max.y - 22.0f - 28.0f;
    const float later_w = 176.0f, now_w = 112.0f;
    if (st.restarts)
    {
        // natware is among them: now (it restarts) or when you close it.
        dl->AddText(ui::Regular(), 14.0f, ImVec2(bb.Min.x + kPadX, bb.Min.y + 69.0f), ui::col::text_dim,
            "Update now (natware restarts) or when you close it.");
        if (PillButton(dl, "##update_later", "When I close natware", ImVec2(bb.Max.x - kPadX - now_w - 8.0f - later_w, y), later_w, false))
        {
            asked = true;
            updater::install_async(false);
        }
        if (PillButton(dl, "##update_restart", "Update now", ImVec2(bb.Max.x - kPadX - now_w, y), now_w, true))
        {
            asked = true;
            config::save_last_settings();   // natware restarts: your settings come back as they are
            updater::install_async(true);
        }
    }
    else
    {
        // Only the editor or nexec: they go in now and natware keeps running.
        dl->AddText(ui::Regular(), 14.0f, ImVec2(bb.Min.x + kPadX, bb.Min.y + 69.0f), ui::col::text_dim,
            "Nothing restarts: it's used the next time it opens.");
        if (PillButton(dl, "##update_restart", "Update", ImVec2(bb.Max.x - kPadX - now_w, y), now_w, true))
        {
            asked = true;
            updater::install_async(true);
        }
    }
    ImGui::End();
}

void Menu::DrawExecutorUI()
{
    static TextEditor editor;
    static bool editor_init = false;
    if (!editor_init)
    {
        editor.SetLanguageDefinition(TextEditor::LanguageDefinition::Lua());

        auto palette = TextEditor::GetDarkPalette();
        palette[(int)TextEditor::PaletteIndex::Default]     = 0xFFD0D0D4;
        palette[(int)TextEditor::PaletteIndex::Background]  = 0x00000000; // transparent â€” veil shows through
        palette[(int)TextEditor::PaletteIndex::Cursor]       = 0xFFF0F0F2;
        palette[(int)TextEditor::PaletteIndex::Selection]    = 0x20FFFFFF;
        palette[(int)TextEditor::PaletteIndex::LineNumber]   = 0x30FFFFFF;
        palette[(int)TextEditor::PaletteIndex::CurrentLineFill] = 0x06FFFFFF;
        palette[(int)TextEditor::PaletteIndex::CurrentLineFillInactive] = 0x03FFFFFF;
        palette[(int)TextEditor::PaletteIndex::CurrentLineEdge] = 0x00000000;
        palette[(int)TextEditor::PaletteIndex::Keyword]      = 0xFFB888E0;
        palette[(int)TextEditor::PaletteIndex::Number]       = 0xFF7ABCD0;
        palette[(int)TextEditor::PaletteIndex::String]       = 0xFF80C88A;
        palette[(int)TextEditor::PaletteIndex::CharLiteral]  = 0xFF80C88A;
        palette[(int)TextEditor::PaletteIndex::Punctuation]  = 0xFF808088;
        palette[(int)TextEditor::PaletteIndex::Comment]      = 0xFF85858E;
        palette[(int)TextEditor::PaletteIndex::MultiLineComment] = 0xFF85858E;
        palette[(int)TextEditor::PaletteIndex::KnownIdentifier] = 0xFF6AACC4;
        palette[(int)TextEditor::PaletteIndex::PreprocIdentifier] = 0xFFA878B0;
        palette[(int)TextEditor::PaletteIndex::Preprocessor] = 0xFFA878B0;
        editor.SetPalette(palette);

        editor.SetText("print(\"hello from natware\")");
        editor_init = true;
    }

    const ImGuiIO& io = ImGui::GetIO();
    const ImVec2 size(ImMin(820.0f, ImMax(560.0f, io.DisplaySize.x - 32.0f)),
        ImMin(560.0f, ImMax(360.0f, io.DisplaySize.y - 32.0f)));
    constexpr float radius = 16.0f;
    constexpr float sidebar_w = 156.0f;
    constexpr float pad = 20.0f;
    static int page = 0;
    static bool show_output = true;

    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(floorf((io.DisplaySize.x - size.x) * 0.5f),
        floorf((io.DisplaySize.y - size.y) * 0.5f)), ImGuiCond_Once);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    const bool visible = ImGui::Begin("##executor_ui", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBackground
        | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
        | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar(2);
    if (!visible) { ImGui::End(); return; }
    SnapWindowToPixels();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImRect bb = ImGui::GetCurrentWindow()->Rect();
    if (const ImTextureID veil = ui::veil::Render(bb.Min, bb.GetSize()))
        dl->AddImageRounded(veil, bb.Min, bb.Max, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, radius);
    else
        dl->AddRectFilled(bb.Min, bb.Max, ui::col::shell, radius);
    dl->AddRect(bb.Min, bb.Max, IM_COL32(255, 255, 255, 20), radius);
    dl->AddLine(ImVec2(bb.Min.x + sidebar_w, bb.Min.y + 16),
        ImVec2(bb.Min.x + sidebar_w, bb.Max.y - 16), ui::col::line);

    dl->AddText(ui::Brand(), ui::kBrandSize, ImVec2(bb.Min.x + 20, bb.Min.y + 23), ui::col::text, "natware");
    dl->AddText(ImVec2(bb.Min.x + 20, bb.Min.y + 51), ui::col::text_faint, "WORKSPACE");
    const float nav_x = bb.Min.x + 10;
    if (ui::NavItem("Editor", ui::Icon::Layers, page == 0,
        ImVec2(nav_x, bb.Min.y + 82), ImVec2(bb.Min.x + sidebar_w - 10, bb.Min.y + 114))) page = 0;
    if (ui::NavItem("Console", ui::Icon::Activity, page == 1,
        ImVec2(nav_x, bb.Min.y + 118), ImVec2(bb.Min.x + sidebar_w - 10, bb.Min.y + 150))) page = 1;
    dl->AddText(ImVec2(bb.Min.x + 20, bb.Max.y - 45), ui::col::text_dim, "Lua workspace");
    dl->AddText(ImVec2(bb.Min.x + 20, bb.Max.y - 26), ui::col::text_faint, "natware");

    const float left = bb.Min.x + sidebar_w + pad;
    const float width = bb.Max.x - pad - left;
    const bool ready = false;
    const std::string status = "Editor only";
    dl->AddText(ui::SemiBold(), 17.0f, ImVec2(left, bb.Min.y + 23), ui::col::text,
        page == 0 ? "Script editor" : "Console");
    dl->AddText(ImVec2(left, bb.Min.y + 48), ui::col::text_dim,
        page == 0 ? "Write and edit Lua scripts." : "Editor activity.");

    // Keep arbitrary backend status strings inside a fixed, clipped status area.
    const float status_y = bb.Max.y - 22;
    dl->AddCircleFilled(ImVec2(left + 3, status_y + 6), 3,
        ready ? IM_COL32(100, 210, 120, 255) : ui::col::text_faint);
    dl->PushClipRect(ImVec2(left + 14, status_y - 2), ImVec2(bb.Max.x - pad, bb.Max.y - 4), true);
    dl->AddText(ImVec2(left + 14, status_y), ui::col::text_dim, status.c_str());
    dl->PopClipRect();

    const float top = bb.Min.y + 82;
    const float bottom = bb.Max.y - 38;
    float console_top = top;
    if (page == 0)
    {
        const float output_h = show_output ? 104.0f : 0.0f;
        const float editor_bottom = bottom - 48.0f - output_h;
        dl->AddRectFilled(ImVec2(left, top), ImVec2(left + width, editor_bottom), IM_COL32(255, 255, 255, 5), 10);
        dl->AddRect(ImVec2(left, top), ImVec2(left + width, editor_bottom), ui::col::line, 10);
        dl->AddText(ImVec2(left + 12, top + 10), ui::col::text_dim, "Script.lua");
        const char* language = "LUA";
        dl->AddText(ImVec2(left + width - 12 - ImGui::CalcTextSize(language).x, top + 10), ui::col::text_faint, language);
        dl->AddLine(ImVec2(left, top + 32), ImVec2(left + width, top + 32), ui::col::line);
        ImGui::SetCursorScreenPos(ImVec2(left + 8, top + 40));
        editor.Render("##lua_editor", ImVec2(width - 16, editor_bottom - top - 48), false);

        ImGui::SetCursorScreenPos(ImVec2(left, editor_bottom + 8));
        ui::SetCompact(true);
        ImGui::BeginDisabled(!ready);
        ui::Button("Execute", ui::Btn::Primary, 84);
        ImGui::EndDisabled();
        ImGui::SameLine(0, 8);
        ImGui::BeginDisabled();
        ui::Button("Reconnect", ui::Btn::Secondary, 92);
        ImGui::EndDisabled();
        ImGui::SameLine(0, 8);
        if (ui::Button(show_output ? "Hide output" : "Show output", ui::Btn::Secondary, 100)) show_output = !show_output;
        ui::SetCompact(false);
        console_top = editor_bottom + 48;
    }

    if (page == 1 || show_output)
    {
        dl->AddRectFilled(ImVec2(left, console_top), ImVec2(left + width, bottom), IM_COL32(255, 255, 255, 5), 10);
        dl->AddRect(ImVec2(left, console_top), ImVec2(left + width, bottom), ui::col::line, 10);
        dl->AddText(ImVec2(left + 12, console_top + 10), ui::col::text_dim, "Output");
        ImGui::SetCursorScreenPos(ImVec2(left + width - 67, console_top + 3));
        ui::SetCompact(true);
        ui::Button("Clear", ui::Btn::Secondary, 60);
        ui::SetCompact(false);
        ImGui::SetCursorScreenPos(ImVec2(left + 12, console_top + 36));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("##exec_log", ImVec2(width - 24, bottom - console_top - 44),
            ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(ui::col::text_faint), "Editor only. Script execution is unavailable.");
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }
    ImGui::End();
}
