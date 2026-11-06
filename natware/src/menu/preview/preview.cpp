#include "preview.h"

#include <render/render.h>
#include <menu/menu.h>
#include <menu/ui/veil.h>
#include <menu/ui/ui.h>
#include <cache/cache.h>
#include <settings.h>
#include <game/game.h>
#include <render/textures/texture.h>
#include "../../../ext/dicons/warn.h"
#include <features/config/config.h>
#include <features/chams/body_chams.h>
#include <features/esp/esp.h>
#include <features/esp/rig.h>
#include <features/esp/esp_draw.h>
#include <updater/updater.h>
#include <json/json.hpp>

#include <imgui/imgui_internal.h>
#include <imgui/misc/imgui_freetype.h>

#include <wincodec.h>
#include <cfloat>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#pragma comment(lib, "windowscodecs.lib")

namespace
{
    constexpr UINT kWidth  = 1280;
    constexpr UINT kHeight = 760;

    LRESULT CALLBACK preview_wnd_proc(HWND h, UINT m, WPARAM w, LPARAM l)
    {
        return DefWindowProcA(h, m, w, l);
    }

    bool save_png(const std::wstring& path, const std::vector<BYTE>& bgra, UINT w, UINT h)
    {
        IWICImagingFactory* factory = nullptr;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
            return false;

        IWICStream* stream = nullptr;
        IWICBitmapEncoder* encoder = nullptr;
        IWICBitmapFrameEncode* frame = nullptr;
        WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;

        bool ok = SUCCEEDED(factory->CreateStream(&stream))
            && SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE))
            && SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder))
            && SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache))
            && SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr))
            && SUCCEEDED(frame->Initialize(nullptr))
            && SUCCEEDED(frame->SetSize(w, h))
            && SUCCEEDED(frame->SetPixelFormat(&fmt))
            && IsEqualGUID(fmt, GUID_WICPixelFormat32bppBGRA)
            && SUCCEEDED(frame->WritePixels(h, w * 4, (UINT)bgra.size(), const_cast<BYTE*>(bgra.data())))
            && SUCCEEDED(frame->Commit())
            && SUCCEEDED(encoder->Commit());

        if (frame) frame->Release();
        if (encoder) encoder->Release();
        if (stream) stream->Release();
        factory->Release();
        return ok;
    }

    std::string exe_dir()
    {
        char path[MAX_PATH]{};
        GetModuleFileNameA(nullptr, path, MAX_PATH);
        std::string s(path);
        return s.substr(0, s.find_last_of("\\/") + 1);
    }
}

int menu_preview::run()
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    WNDCLASSEXA wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = preview_wnd_proc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "natware_menu_preview";
    RegisterClassExA(&wc);

    // Never shown: the Win32 backend only needs it for the client size.
    HWND hwnd = CreateWindowExA(0, wc.lpszClassName, "", WS_POPUP, 0, 0, kWidth, kHeight,
        nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) { printf("preview: CreateWindow failed\n"); return 1; }

    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &device, nullptr, &context))
        && FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &device, nullptr, &context)))
    {
        printf("preview: D3D11CreateDevice failed\n");
        return 1;
    }

    D3D11_TEXTURE2D_DESC td{};
    td.Width = kWidth;
    td.Height = kHeight;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET;

    ID3D11Texture2D* target = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    device->CreateTexture2D(&td, nullptr, &target);
    device->CreateRenderTargetView(target, nullptr, &rtv);

    td.Usage = D3D11_USAGE_STAGING;
    td.BindFlags = 0;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D* staging = nullptr;
    device->CreateTexture2D(&td, nullptr, &staging);

    render->detail->window = hwnd;
    render->detail->device = device;
    render->detail->device_context = context;
    render->detail->render_target_view = rtv;
    render->running = true;   // the menu fades toward "open"

    if (!render->create_imgui() || !Menu::Initialize(hwnd, device, context))
    {
        printf("preview: imgui init failed\n");
        return 1;
    }

    // This process exits after the shots, so turn on a spread of features to make
    // the dependent (indented) rows show up in the screenshots.
    settings::aimbot::enabled = true;
    settings::aimbot::keybind = VK_XBUTTON2;
    settings::aimbot::draw_fov = true;
    settings::aimbot::smoothing = true;
    settings::aimbot::health_check_enabled = true;
    settings::aimbot::triggerbot::enabled = true;
    settings::silentaim::enabled = true;
    settings::silentaim::draw_fov = true;
    settings::silentaim::hitchance_enabled = true;
    settings::visuals::enable_enemies = true;
    settings::visuals::box = true;
    settings::visuals::name = true;
    settings::visuals::healthbar = true;
    settings::visuals::chams = true;
    settings::visuals::max_distance_enabled = true;
    settings::movement::speedhack::enabled = true;
    settings::movement::flyhack::enabled = true;
    settings::lighting::fog::enabled = true;
    settings::rage::hitsounds = true;
    settings::rage::hitbox_expander::enabled = true;
    settings::movement::orbit::enabled = true;
    settings::menu::watermark = true;

    // Sample players so the player list shot has rows to judge (nothing reads their memory
    // unless one is selected, and this process exits after the shots).
    const char* const sample_names[] = { "Alpha", "Bravo", "Charlie", "Delta", "Echo", "Foxtrot" };
    for (int i = 0; i < 6; i++)
    {
        cache::entity_t e;
        e.instance.address = 0x10000 + i * 0x100;
        e.display_name = sample_names[i];
        e.name = std::string("sample_") + sample_names[i];
        e.health = 100.0f - i * 13.0f;
        e.max_health = 100.0f;
        cache::cached_players.push_back(e);
    }

    const std::string out_dir = exe_dir() + "preview\\";
    CreateDirectoryA(out_dir.c_str(), nullptr);

    // Renders ~45 frames (enough at 60 Hz for animations to settle) and saves the last one.
    auto capture = [&](const char* name, auto&& draw, float mouse_x, float mouse_y, int button, float drag_x, int click_frame, int frames)
    {
        for (int f = 0; f < frames; f++)
        {
            // The window is never focused, so the Win32 backend leaves the mouse alone
            // and these injected events are the only input ImGui sees.
            ImGuiIO& io = ImGui::GetIO();
            if (mouse_x >= 0.0f && drag_x >= 0.0f)
            {
                const float t = ImClamp((f - 12) / 18.0f, 0.0f, 1.0f);
                io.AddMousePosEvent(ImLerp(mouse_x, drag_x, t), mouse_y);
                if (f == 10) io.AddMouseButtonEvent(0, true);
            }
            else if (mouse_x >= 0.0f)
            {
                io.AddMousePosEvent(mouse_x, mouse_y);
                if (button >= 0 && f == click_frame) io.AddMouseButtonEvent(button, true);
                if (button >= 0 && f == click_frame + 2) io.AddMouseButtonEvent(button, false);
            }
            else
            {
                io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
            }

            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            if (f == 0)
                ImGui::ClosePopupsExceptModals();
            draw();
            ImGui::Render();

            // Mid-grey backdrop stands in for the game so the shell edges read.
            const float backdrop[4] = { 0.24f, 0.26f, 0.29f, 1.0f };
            context->OMSetRenderTargets(1, &rtv, nullptr);
            context->ClearRenderTargetView(rtv, backdrop);
            ui::veil::RenderDrawData(ImGui::GetDrawData(), rtv);
            Sleep(16);
        }

        context->CopyResource(staging, target);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)))
            return;

        std::vector<BYTE> bgra(kWidth * kHeight * 4);
        for (UINT y = 0; y < kHeight; y++)
        {
            const BYTE* src = static_cast<const BYTE*>(mapped.pData) + y * mapped.RowPitch;
            BYTE* dst = bgra.data() + y * kWidth * 4;
            for (UINT x = 0; x < kWidth; x++)
            {
                dst[x * 4 + 0] = src[x * 4 + 2];
                dst[x * 4 + 1] = src[x * 4 + 1];
                dst[x * 4 + 2] = src[x * 4 + 0];
                dst[x * 4 + 3] = 255;
            }
        }
        context->Unmap(staging, 0);

        const std::string file = out_dir + name + ".png";
        const std::wstring wfile(file.begin(), file.end());
        printf("%s %s\n", save_png(wfile, bgra, kWidth, kHeight) ? "wrote" : "FAILED", file.c_str());
    };

    // Results of the checks below go to preview\checks.txt (stdout is NUL in this exe).
    auto report = [&](const char* fmt, auto... args)
    {
        char line[512];
        _snprintf_s(line, sizeof(line), _TRUNCATE, fmt, args...);   // long messages are cut, not fatal
        std::ofstream log(out_dir + "checks.txt", std::ios::app);
        log << line;
    };
    // Config checks against the real config code (run from the exe folder):
    //   --config-roundtrip <from> <to>   load a config, save it under another name
    //   --configs-live                   drop a config into the folder and replace the loaded one,
    //                                    rendering the Configs page after each step
    //   --aim-mode                       the aim mode must survive switching games
    if (const char* rt = strstr(GetCommandLineA(), "--config-roundtrip ")) {
        char from[128]{}, to[128]{};
        sscanf_s(rt + strlen("--config-roundtrip "), "%127s %127s", from, (unsigned)sizeof(from), to, (unsigned)sizeof(to));
        const bool loaded = config::load_config(from);
        const bool saved = loaded && config::save_config(to);
        report("roundtrip %s -> %s: load %s, save %s\n", from, to, loaded ? "ok" : "FAILED", saved ? "ok" : "FAILED");
        ExitProcess(loaded && saved ? 0 : 1);
    }
    if (strstr(GetCommandLineA(), "--configs-live")) {
        Menu::PreviewSelect(Menu::PreviewPages()[7]);   // Configs
        render->running = true;
        std::error_code ec;
        std::filesystem::remove("configs\\dropped in.json", ec);
        report("load pf: %s\n", config::load_config("pf") ? "ok" : "FAILED");
        config::save_config("pf");   // now in the current format, so the edit below can parse it
        config::watch(true);
        capture("cfg_1_loaded", [] { Menu::DrawMenu(); }, -1, -1, -1, -1, 10, 45);
        std::filesystem::copy_file("configs\\pf.json", "configs\\dropped in.json", ec);
        capture("cfg_2_dropped_in", [] { Menu::DrawMenu(); }, -1, -1, -1, -1, 10, 45);
        bool listed = false;
        for (const auto& c : config::list())
            listed |= c.name == "dropped in";
        report("new file listed without restart: %s\n", listed ? "yes" : "NO");
        // Replace the loaded config's file the way an editor or a download would.
        {
            std::ifstream in("configs\\pf.json");
            std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            in.close();
            nlohmann::json j = nlohmann::json::parse(text);
            j["aimbot"]["fov"] = 222.0;
            std::ofstream out("configs\\pf.json", std::ios::trunc);
            out << j.dump(2);
        }
        capture("cfg_3_replaced_on_disk", [] { Menu::DrawMenu(); }, -1, -1, -1, -1, 10, 45);
        report("aimbot fov after the file was replaced: %.1f (expect 222.0), message: %s\n",
            settings::aimbot::fov, config::last_message().c_str());
        std::filesystem::remove("configs\\dropped in.json", ec);
        ExitProcess(0);
    }
    if (strstr(GetCommandLineA(), "--texture-check")) {
        // The ESP warning icon decodes without D3DX11 now.
        ID3D11ShaderResourceView* view = D3D11CreateTextureFromBytes(device, warn_icon_data, sizeof(warn_icon_data));
        report("texture from PNG bytes: %s\n", view ? "ok" : "FAILED");
        ExitProcess(view ? 0 : 1);
    }
    if (strstr(GetCommandLineA(), "--aim-mode")) {
        Menu::PreviewSelect(Menu::PreviewPages()[0]);   // Aimbot
        render->running = true;
        auto frames = [&](const char* step)
        {
            capture(step, [] { Menu::DrawMenu(); }, -1, -1, -1, -1, 10, 6);
        };
        settings::aimbot::mode = 0; game::is_phantom_forces = true;  frames("aim_1_pf_mouse");
        report("Mouse chosen, in PF: %d (expect 0)\n", settings::aimbot::mode);
        settings::aimbot::mode = 2;                                  frames("aim_2_pf_silent");
        game::is_phantom_forces = false;                             frames("aim_3_left_pf");
        report("Silent chosen, left PF and drew the page: %d (expect 2)\n", settings::aimbot::mode);
        config::save_config("aim mode test");
        settings::aimbot::mode = 0;
        config::load_config("aim mode test");
        report("saved and loaded again: %d (expect 2)\n", settings::aimbot::mode);
        config::delete_config("aim mode test");
        ExitProcess(0);
    }

    // Text rasterizer comparison: Arial through each FreeType setting, on the shell colour and on
    // a veil purple, at the menu's sizes.
    if (strstr(GetCommandLineA(), "--fonts")) {
        struct Variant { const char* name; unsigned flags; float multiply; bool snap; ImFont* font; };
        static Variant variants[] = {
            { "light x1.2 snap", ImGuiFreeTypeLoaderFlags_LightHinting, 1.2f, true, nullptr },
            { "light x1.0 snap", ImGuiFreeTypeLoaderFlags_LightHinting, 1.0f, true, nullptr },
            { "light x1.0", ImGuiFreeTypeLoaderFlags_LightHinting, 1.0f, false, nullptr },
            { "none x1.0", ImGuiFreeTypeLoaderFlags_NoHinting, 1.0f, false, nullptr },
            { "none x1.15", ImGuiFreeTypeLoaderFlags_NoHinting, 1.15f, false, nullptr },
            { "auto x1.0", ImGuiFreeTypeLoaderFlags_ForceAutoHint, 1.0f, false, nullptr },
            { "native x1.0", 0u, 1.0f, true, nullptr },
        };
        for (Variant& v : variants)
        {
            ImFontConfig cfg;
            cfg.FontLoaderFlags = v.flags;
            cfg.RasterizerMultiply = v.multiply;
            cfg.PixelSnapH = v.snap;
            v.font = ImGui::GetIO().Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\arial.ttf", 14.0f, &cfg);
        }
        capture("x_fonts", []
        {
            ImDrawList* dl = ImGui::GetForegroundDrawList();
            dl->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2((float)kWidth, (float)kHeight), IM_COL32(10, 10, 12, 255));
            dl->AddRectFilled(ImVec2(640.0f, 0.0f), ImVec2((float)kWidth, (float)kHeight), IM_COL32(52, 18, 104, 255));
            float y = 12.0f;
            for (const Variant& v : variants)
            {
                for (int side = 0; side < 2; side++)
                {
                    const float x = 12.0f + side * 640.0f;
                    dl->AddText(ui::Regular(), 12.0f, ImVec2(x, y), ui::col::text_faint, v.name);
                    float yy = y + 16.0f;
                    for (float size : { 13.0f, 14.0f, 15.0f, 16.0f })
                    {
                        dl->AddText(v.font, size, ImVec2(x, floorf(yy)), ui::col::text, "Aimbot  Silent aim  Movement  Draw FOV  Target part  0.75");
                        yy += size + 5.0f;
                    }
                }
                y += 104.0f;
            }
        }, -1.0f, -1.0f, -1, -1.0f, 10, 4);
        ExitProcess(0);
    }

    // Chams and skeletons on made-up players: an R15 and an R6 rig mid-stride, seen through a
    // camera built the way Roblox's view-projection matrix is (rows multiply column vectors).
    if (strstr(GetCommandLineA(), "--chams")) {
        auto rot_y = [](float a) { math::matrix3 r = math::matrix3::identity(); r.m[0][0] = cosf(a); r.m[0][2] = sinf(a); r.m[2][0] = -sinf(a); r.m[2][2] = cosf(a); return r; };
        auto rot_x = [](float a) { math::matrix3 r = math::matrix3::identity(); r.m[1][1] = cosf(a); r.m[1][2] = -sinf(a); r.m[2][1] = sinf(a); r.m[2][2] = cosf(a); return r; };
        auto mul = [](const math::matrix3& a, const math::matrix3& b) { math::matrix3 r{}; for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) { r.m[i][j] = 0; for (int k = 0; k < 3; k++) r.m[i][j] += a.m[i][k] * b.m[k][j]; } return r; };

        struct piece { const char* name; math::vector3 size; math::vector3 pos; };
        // Swings a limb (and everything listed after it in `names`) about a pivot.
        auto swing = [&](rig::body_t& b, std::initializer_list<const char*> names, math::vector3 pivot, const math::matrix3& r)
        {
            for (auto& p : b.parts)
                for (const char* n : names)
                    if (p.name == n)
                    {
                        p.pos = pivot + r * (p.pos - pivot);
                        p.rot = mul(r, p.rot);
                    }
        };
        auto make = [&](rig::kind_t kind, std::initializer_list<piece> pieces, math::vector3 at, float yaw)
        {
            rig::body_t b;
            b.kind = kind;
            for (const piece& pc : pieces)
            {
                rig::part_t p;
                p.name = pc.name;
                p.size = pc.size;
                p.pos = pc.pos;
                p.rot = math::matrix3::identity();
                b.parts.push_back(p);
            }
            return std::make_pair(b, std::make_pair(at, yaw));
        };
        auto place = [&](rig::body_t& b, math::vector3 at, float yaw)
        {
            const math::matrix3 r = rot_y(yaw);
            for (auto& p : b.parts)
            {
                p.pos = at + r * p.pos;
                p.rot = mul(r, p.rot);
            }
        };

        auto r15 = make(rig::kind_t::r15, {
            { "LeftFoot", { 1, 0.3f, 1 }, { -0.5f, 0.15f, 0 } }, { "RightFoot", { 1, 0.3f, 1 }, { 0.5f, 0.15f, 0 } },
            { "LeftLowerLeg", { 1, 1.1f, 1 }, { -0.5f, 0.85f, 0 } }, { "RightLowerLeg", { 1, 1.1f, 1 }, { 0.5f, 0.85f, 0 } },
            { "LeftUpperLeg", { 1, 1.2f, 1 }, { -0.5f, 2.0f, 0 } }, { "RightUpperLeg", { 1, 1.2f, 1 }, { 0.5f, 2.0f, 0 } },
            { "LowerTorso", { 2, 0.4f, 1 }, { 0, 2.8f, 0 } }, { "UpperTorso", { 2, 1.6f, 1 }, { 0, 3.8f, 0 } },
            { "Head", { 1.2f, 1.2f, 1.2f }, { 0, 5.2f, 0 } },
            { "LeftUpperArm", { 1, 1.2f, 1 }, { -1.5f, 4.0f, 0 } }, { "RightUpperArm", { 1, 1.2f, 1 }, { 1.5f, 4.0f, 0 } },
            { "LeftLowerArm", { 1, 1.1f, 1 }, { -1.5f, 2.85f, 0 } }, { "RightLowerArm", { 1, 1.1f, 1 }, { 1.5f, 2.85f, 0 } },
            { "LeftHand", { 1, 0.3f, 1 }, { -1.5f, 2.15f, 0 } }, { "RightHand", { 1, 0.3f, 1 }, { 1.5f, 2.15f, 0 } },
        }, { -3.2f, 0, 0 }, 0.5f);
        swing(r15.first, { "LeftUpperLeg", "LeftLowerLeg", "LeftFoot" }, { -0.5f, 2.6f, 0 }, rot_x(-0.45f));
        swing(r15.first, { "RightUpperLeg", "RightLowerLeg", "RightFoot" }, { 0.5f, 2.6f, 0 }, rot_x(0.4f));
        swing(r15.first, { "LeftUpperArm", "LeftLowerArm", "LeftHand" }, { -1.5f, 4.6f, 0 }, rot_x(0.5f));
        swing(r15.first, { "RightUpperArm", "RightLowerArm", "RightHand" }, { 1.5f, 4.6f, 0 }, rot_x(-1.2f));
        swing(r15.first, { "RightLowerArm", "RightHand" }, { 1.5f + 0.0f, 3.4f, 0 }, rot_x(-0.6f));
        place(r15.first, r15.second.first, r15.second.second);

        auto r6 = make(rig::kind_t::r6, {
            { "Left Leg", { 1, 2, 1 }, { -0.5f, 1, 0 } }, { "Right Leg", { 1, 2, 1 }, { 0.5f, 1, 0 } },
            { "Torso", { 2, 2, 1 }, { 0, 3, 0 } }, { "Head", { 2, 1, 1 }, { 0, 4.5f, 0 } },
            { "Left Arm", { 1, 2, 1 }, { -1.5f, 3, 0 } }, { "Right Arm", { 1, 2, 1 }, { 1.5f, 3, 0 } },
        }, { 3.2f, 0, 0 }, -0.6f);
        swing(r6.first, { "Left Leg" }, { -0.5f, 2, 0 }, rot_x(0.35f));
        swing(r6.first, { "Right Leg" }, { 0.5f, 2, 0 }, rot_x(-0.35f));
        swing(r6.first, { "Left Arm" }, { -1.5f, 3.5f, 0 }, rot_x(-0.4f));
        swing(r6.first, { "Right Arm" }, { 1.5f, 3.5f, 0 }, rot_x(-1.5f));
        place(r6.first, r6.second.first, r6.second.second);

        static rig::body_t bodies[2];
        bodies[0] = r15.first;
        bodies[1] = r6.first;

        // Camera 13 studs back, a little above, looking at the pair: view rows are its right,
        // up and back vectors; the projection is the usual 70-degree perspective.
        static math::matrix4 vp{};
        {
            const math::vector3 eye{ 0.0f, 5.0f, 13.0f }, target{ 0.0f, 2.6f, 0.0f };
            const math::vector3 back = (eye - target).normalized();
            const math::vector3 right = math::vector3{ 0, 1, 0 }.cross(back).normalized();
            const math::vector3 up = back.cross(right);
            const float f = 1.0f / tanf(70.0f * 3.14159265f / 360.0f), aspect = (float)kWidth / (float)kHeight;
            const math::vector3 rows[3] = { right, up, back };
            float V[4][4]{};
            for (int i = 0; i < 3; i++) { V[i][0] = rows[i].x; V[i][1] = rows[i].y; V[i][2] = rows[i].z; V[i][3] = -rows[i].dot(eye); }
            V[3][3] = 1.0f;
            float Pm[4][4]{};
            Pm[0][0] = f / aspect; Pm[1][1] = f; Pm[2][2] = -1.0f; Pm[2][3] = -0.2f; Pm[3][2] = -1.0f;
            for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) { float sum = 0; for (int k = 0; k < 4; k++) sum += Pm[i][k] * V[k][j]; vp.m[i][j] = sum; }
        }
        game::window_offset_x = game::window_offset_y = 0.0f;

        struct shot_t { const char* file; int effect; float fill[4]; float outline[4]; bool chams; bool skeleton; };
        static const shot_t shots[] = {
            { "chams_flat",     body_chams::effect_flat,     { 1.0f, 0.15f, 0.2f, 0.55f }, { 1, 1, 1, 1 }, true, false },
            { "chams_water",    body_chams::effect_water,    { 0.2f, 0.55f, 1.0f, 0.85f }, { 0.75f, 0.9f, 1, 1 }, true, false },
            { "chams_plasma",   body_chams::effect_plasma,   { 0.75f, 0.2f, 1.0f, 0.9f }, { 1, 0.8f, 1, 1 }, true, false },
            { "chams_hologram", body_chams::effect_hologram, { 0.2f, 0.95f, 1.0f, 0.9f }, { 0.6f, 1, 1, 1 }, true, false },
            { "chams_galaxy",   body_chams::effect_galaxy,   { 0.55f, 0.35f, 1.0f, 0.95f }, { 0.85f, 0.75f, 1, 1 }, true, false },
            { "skeleton",       body_chams::effect_flat,     { 0, 0, 0, 0 }, { 0, 0, 0, 0 }, false, true },
            { "skeleton_on_chams", body_chams::effect_flat,  { 1.0f, 0.15f, 0.2f, 0.35f }, { 1, 1, 1, 0.8f }, true, true },
        };
        static const shot_t* current = nullptr;
        for (const shot_t& shot : shots)
        {
            current = &shot;
            capture(shot.file, []
            {
                ImDrawList* dl = ImGui::GetBackgroundDrawList();
                // A dusk-coloured stand-in for the game behind the overlay.
                dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2((float)kWidth, (float)kHeight),
                    IM_COL32(58, 70, 92, 255), IM_COL32(58, 70, 92, 255), IM_COL32(28, 30, 36, 255), IM_COL32(28, 30, 36, 255));
                if (current->chams && body_chams::begin(dl))
                {
                    for (const rig::body_t& b : bodies)
                        body_chams::add(b, current->fill, current->outline);
                    body_chams::frame_t frame;
                    frame.view_projection = vp;
                    frame.dims = { (float)kWidth, (float)kHeight };
                    frame.offset = { 0.0f, 0.0f };
                    frame.effect = current->effect;
                    body_chams::end(frame);
                }
                if (current->skeleton)
                    for (const rig::body_t& b : bodies)
                        esp::draw_skeleton(dl, b, vp, { (float)kWidth, (float)kHeight }, IM_COL32(255, 255, 255, 255), 1.5f);
            }, -1, -1, -1, -1, 10, 40);
        }
        // The natware ESP look on both players: chams, skeleton, box, name pill, bars, info.
        capture("esp_natware", []
        {
            ImDrawList* dl = ImGui::GetBackgroundDrawList();
            dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2((float)kWidth, (float)kHeight),
                IM_COL32(58, 70, 92, 255), IM_COL32(58, 70, 92, 255), IM_COL32(28, 30, 36, 255), IM_COL32(28, 30, 36, 255));
            const float fill[4] = { 0.42f, 0.07f, 1.0f, 0.35f }, outline[4] = { 0.8f, 0.7f, 1.0f, 0.9f };
            if (body_chams::begin(dl))
            {
                for (const rig::body_t& b : bodies)
                    body_chams::add(b, fill, outline);
                body_chams::frame_t frame;
                frame.view_projection = vp;
                frame.dims = { (float)kWidth, (float)kHeight };
                body_chams::end(frame);
            }
            const char* names[2] = { "Builderman", "noob_slayer" };
            const char* infos[2] = { "24 studs  \xC2\xB7  Rifle", "31 studs" };
            const float health[2] = { 0.72f, 0.28f };
            for (int i = 0; i < 2; i++)
            {
                esp::draw_skeleton(dl, bodies[i], vp, { (float)kWidth, (float)kHeight }, IM_COL32(255, 255, 255, 230), 1.5f);
                ImVec2 mn(FLT_MAX, FLT_MAX), mx(-FLT_MAX, -FLT_MAX);
                for (const rig::part_t& p : bodies[i].parts)
                    for (int k = 0; k < 8; ++k)
                    {
                        const math::vector3 c{ (k & 1) ? 0.5f : -0.5f, (k & 2) ? 0.5f : -0.5f, (k & 4) ? 0.5f : -0.5f };
                        math::vector2 s2{};
                        if (game::visengine.world_to_screen(p.pos + p.rot * math::vector3{ c.x * p.size.x, c.y * p.size.y, c.z * p.size.z }, s2, { (float)kWidth, (float)kHeight }, vp))
                        {
                            mn = ImMin(mn, ImVec2(s2.x, s2.y));
                            mx = ImMax(mx, ImVec2(s2.x, s2.y));
                        }
                    }
                esp_draw::player_t look{};
                look.min = mn;
                look.max = mx;
                look.box = IM_COL32(255, 255, 255, 235);
                look.box_style = i == 0 ? 0 : 1;
                look.name = names[i];
                look.name_col = IM_COL32(255, 255, 255, 255);
                look.accent = i == 0 ? IM_COL32(107, 18, 255, 255) : IM_COL32(255, 70, 70, 255);
                look.health = health[i];
                look.health_text = true;
                look.health_text_col = IM_COL32(255, 255, 255, 220);
                look.armor = i == 0 ? 0.45f : -1.0f;
                look.armor_col = IM_COL32(70, 160, 255, 255);
                look.info = infos[i];
                look.info_col = IM_COL32(230, 230, 235, 230);
                look.flag = i == 0 ? "Running" : "Jumping";
                look.flag_col = IM_COL32(220, 220, 225, 200);
                esp_draw::player(dl, look);
            }
        }, -1, -1, -1, -1, 10, 40);
        report("chams layer: %s\n", body_chams::error().empty() ? "ok" : body_chams::error().c_str());
        ExitProcess(body_chams::error().empty() ? 0 : 1);
    }

    // Your last settings: written, wiped, read back; and old chams keys mapped onto the new styles.
    // The updater against a local stand-in for GitHub (data\settings.json's update_api):
    //   --update-test         check, the launcher's footer, download + SHA-256, swap the exe
    //   --update-test-later   check, then click "When I close natware" on the in-game prompt,
    //                         wait for the download, and put it in place the way quitting does
    if (strstr(GetCommandLineA(), "--update-test")) {
        std::string error;
        const bool checked = updater::check_now(error);
        updater::status_t st = updater::status();
        report("update check: %s, state %d (3 = available), latest %s, version %s, error '%s'\n",
            checked ? "ok" : "FAILED", (int)st.state, st.latest.c_str(), updater::version(), error.c_str());
        for (const updater::component_t& c : updater::components())
            report("  %-8s present %d, installed '%s', latest '%s', available %d, ticked %d, state %d '%s'\n",
                c.name.c_str(), c.present ? 1 : 0, c.installed.c_str(), c.latest.c_str(), c.available ? 1 : 0,
                c.selected ? 1 : 0, (int)c.state, c.message.c_str());
        capture("update_1_launcher", [] { Menu::DrawModePicker(); }, -1, -1, -1, -1, 10, 45);

        // An update left unticked (a local build) gets ticked by clicking its row in the dialog
        // (Menu::UpdateDialogPoint knows where the dialog was drawn).
        {
            const std::vector<updater::component_t> comps = updater::components();
            int row = 0;
            for (size_t i = 0; i < comps.size(); ++i)
            {
                if (!comps[i].available)
                    continue;
                if (!comps[i].selected)
                {
                    const ImVec2 pt = Menu::UpdateDialogPoint("row", row);
                    capture("update_1b_ticked", [] { Menu::DrawModePicker(); }, pt.x, pt.y, 0, -1, 10, 30);
                    for (const updater::component_t& c : updater::components())
                        if (c.name == comps[i].name)
                            report("clicked %s's row: ticked %d (1)\n", c.name.c_str(), c.selected ? 1 : 0);
                }
                row++;
            }
        }

        // --update-test-restart: click the dialog's Update now and expect natware to swap
        // itself, start the new exe and ask to quit (it isn't the editor's coordinator here).
        if (strstr(GetCommandLineA(), "--update-test-restart")) {
            const ImVec2 pt = Menu::UpdateDialogPoint("update");
            capture("update_6_restart_click", [] { Menu::DrawModePicker(); }, pt.x, pt.y, 0, -1, 10, 20);
            for (int i = 0; i < 400 && !settings::globals::quit_requested
                && updater::status().state != updater::state_t::failed; ++i)
                Sleep(25);
            st = updater::status();
            report("after Update: quit requested %d (1), state %d, message '%s', natware went in %d (1)\n",
                settings::globals::quit_requested ? 1 : 0, (int)st.state, st.message.c_str(), updater::updated("natware") ? 1 : 0);
            ExitProcess(settings::globals::quit_requested ? 0 : 1);
        }

        if (strstr(GetCommandLineA(), "--update-test-later")) {
            render->running = true;   // the menu is open, so the prompt takes clicks
            capture("update_2_prompt", [] { Menu::DrawUpdatePrompt(); }, -1, -1, -1, -1, 10, 30);
            const float cx = ImGui::GetIO().DisplaySize.x * 0.5f;
            // "When I close natware" sits left of "Update now" on the prompt's bottom row.
            capture("update_3_clicked_later", [] { Menu::DrawUpdatePrompt(); }, cx - 10.0f, 138.0f, 0, -1, 8, 14);
            for (int i = 0; i < 400 && updater::status().state != updater::state_t::staged
                && updater::status().state != updater::state_t::failed; ++i)
                Sleep(25);
            st = updater::status();
            report("after 'When I close natware': state %d (6 = staged), message '%s'\n", (int)st.state, st.message.c_str());
            capture("update_4_staged_note", [] { Menu::DrawUpdatePrompt(); }, -1, -1, -1, -1, 10, 20);
            const bool swapped = updater::finish_on_quit();
            report("put in place on quit: %s\n", swapped ? "yes" : "NO");
            ExitProcess(swapped ? 0 : 1);
        }

        const bool staged = checked && updater::stage_now(error);
        report("download + SHA-256: %s '%s'\n", staged ? "ok" : "REFUSED", error.c_str());
        const bool swapped = staged && updater::swap_now(error);
        report("swap: %s '%s'\n", swapped ? "ok" : "not done", error.c_str());
        for (const updater::component_t& c : updater::components())
            report("  %-8s went in %d, installed '%s'\n", c.name.c_str(), updater::updated(c.name) ? 1 : 0, c.installed.c_str());
        capture("update_5_after", [] { Menu::DrawModePicker(); }, -1, -1, -1, -1, 10, 20);
        ExitProcess(swapped ? 0 : 1);
    }
    if (strstr(GetCommandLineA(), "--last-settings")) {
        settings::visuals::skeleton = true;
        settings::visuals::chams_style = 1;
        settings::visuals::chams_effect = 2;
        settings::aimbot::fov = 123.0f;
        settings::rage::rapidfire_mode = 1;
        config::save_last_settings();
        config::reset_to_defaults();
        const bool restored = config::restore_last_settings();
        report("last settings restored: %s; skeleton %d (1), chams style %d (1), effect %d (2), fov %.0f (123), rapid fire method %d (1)\n",
            restored ? "yes" : "NO", settings::visuals::skeleton ? 1 : 0, settings::visuals::chams_style, settings::visuals::chams_effect,
            settings::aimbot::fov, settings::rage::rapidfire_mode);

        {
            std::ofstream("configs\\legacy engine.json") << R"({ "visuals": { "chams": true, "chams_type": 3, "chams_texture": 0 } })";
            std::ofstream("configs\\legacy texture.json") << R"({ "visuals": { "chams": true, "chams_type": 1, "chams_texture": 3 } })";
        }
        config::load_config("legacy engine");
        const int a_style = settings::visuals::chams_style;
        config::load_config("legacy texture");
        report("old chams configs: engine -> style %d (2); hologram texture -> style %d effect %d (1, 2)\n",
            a_style, settings::visuals::chams_style, settings::visuals::chams_effect);
        config::delete_config("legacy engine");
        config::delete_config("legacy texture");
        ExitProcess(restored ? 0 : 1);
    }

    // The Visuals preview card in three looks: no chams (the mannequin, skeleton on), flat
    // chams, and Engine chams (Plasma).
    if (strstr(GetCommandLineA(), "--visuals-preview")) {
        Menu::PreviewSelect(Menu::PreviewPages()[4]);
        render->running = true;
        settings::visuals::skeleton = true;
        settings::visuals::chams = false;
        capture("vprev_1_plain", [] { Menu::DrawMenu(); }, -1, -1, -1, -1, 10, 60);
        settings::visuals::skeleton = false;
        settings::visuals::chams = true;
        settings::visuals::chams_style = 0;
        capture("vprev_2_flat", [] { Menu::DrawMenu(); }, -1, -1, -1, -1, 10, 30);
        settings::visuals::chams_style = 1;
        settings::visuals::chams_effect = 1;
        capture("vprev_3_engine", [] { Menu::DrawMenu(); }, -1, -1, -1, -1, 10, 30);
        ExitProcess(0);
    }

    // The Visuals page scrolled down to the Body card, chams on Engine.
    if (strstr(GetCommandLineA(), "--visuals-body")) {
        Menu::PreviewSelect(Menu::PreviewPages()[4]);
        render->running = true;
        settings::visuals::skeleton = true;
        settings::visuals::chams = true;
        settings::visuals::chams_style = 1;
        settings::visuals::chams_effect = 2;
        settings::visuals::debug_wallcheck = true;
        static int frame = 0;
        for (const float wheel : { -5.0f, -9.0f })
        {
            frame = 0;
            static float step;
            step = wheel;
            capture(wheel > -6.0f ? "visuals_body" : "visuals_filters", []
            {
                if (frame++ < 12)
                    ImGui::GetIO().AddMouseWheelEvent(0.0f, step / 12.0f);
                Menu::DrawMenu();
            }, 740.0f, 420.0f, -1, -1, 10, 60);
        }
        ExitProcess(0);
    }

    if (strstr(GetCommandLineA(), "--picker")) {
        capture("launcher", [] { Menu::DrawModePicker(); }, -1, -1, -1, -1, 10, 75);
        // Mouse over the Editor card (launcher centred on the 1280x760 canvas).
        capture("launcher_hover", [] { Menu::DrawModePicker(); }, 509.0f, 420.0f, -1, -1, 10, 45);
        capture("launcher_close_hover", [] { Menu::DrawModePicker(); }, 890.0f, 276.0f, -1, -1, 10, 30);
        capture("waiting", [] { Menu::DrawWaiting("Waiting for Roblox to open"); }, -1, -1, -1, -1, 10, 60);
        // The update dialog over the launcher, with a made-up release.
        Menu::PreviewUpdateDialog(true);
        capture("launcher_update", [] { Menu::DrawModePicker(); }, -1, -1, -1, -1, 10, 60);
        Menu::PreviewUpdateDialog(false);
        // Clicking the picker's close button must ask natware to quit (after its fade).
        settings::globals::quit_requested = false;
        capture("launcher_closing", [] { Menu::DrawModePicker(); }, 890.0f, 276.0f, 0, -1, 4, 60);
        report("picker close button -> quit requested: %s\n", settings::globals::quit_requested ? "yes" : "NO");
        ExitProcess(0);
    }

    // The in-game ESP look on sample players (the real esp_draw code), over a stand-in scene.
    if (strstr(GetCommandLineA(), "--esp")) {
        capture("esp_samples", []
        {
            ImDrawList* dl = ImGui::GetBackgroundDrawList();
            // Dusk sky, a floor under it, and a figure for each box to sit on.
            const float horizon = 350.0f;
            dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2((float)kWidth, horizon),
                IM_COL32(38, 50, 80, 255), IM_COL32(38, 50, 80, 255), IM_COL32(112, 96, 118, 255), IM_COL32(112, 96, 118, 255));
            dl->AddRectFilledMultiColor(ImVec2(0, horizon), ImVec2((float)kWidth, (float)kHeight),
                IM_COL32(58, 52, 62, 255), IM_COL32(58, 52, 62, 255), IM_COL32(24, 22, 28, 255), IM_COL32(24, 22, 28, 255));
            auto figure = [dl](ImVec2 mn, ImVec2 mx)
            {
                const float W = mx.x - mn.x, H = mx.y - mn.y, cx = (mn.x + mx.x) * 0.5f;
                const ImU32 body = IM_COL32(34, 40, 54, 255), rim = IM_COL32(140, 160, 200, 110);
                for (int i = 4; i >= 1; --i)
                    dl->AddEllipseFilled(ImVec2(cx, mx.y), ImVec2(W * (0.55f + i * 0.10f), 5.0f + i * 2.0f), IM_COL32(0, 0, 0, 22));
                dl->AddRectFilled(ImVec2(cx - W * 0.30f, mn.y + H * 0.55f), ImVec2(cx - W * 0.02f, mx.y - 1.0f), body, 3.0f);
                dl->AddRectFilled(ImVec2(cx + W * 0.02f, mn.y + H * 0.55f), ImVec2(cx + W * 0.30f, mx.y - 1.0f), body, 3.0f);
                dl->AddRectFilled(ImVec2(cx - W * 0.30f, mn.y + H * 0.20f), ImVec2(cx + W * 0.30f, mn.y + H * 0.57f), body, 5.0f);
                dl->AddRectFilled(ImVec2(cx - W * 0.46f, mn.y + H * 0.21f), ImVec2(cx - W * 0.32f, mn.y + H * 0.52f), body, 4.0f);
                dl->AddRectFilled(ImVec2(cx + W * 0.32f, mn.y + H * 0.21f), ImVec2(cx + W * 0.46f, mn.y + H * 0.52f), body, 4.0f);
                dl->AddCircleFilled(ImVec2(cx, mn.y + H * 0.115f), W * 0.20f, body, 28);
                dl->AddLine(ImVec2(cx - W * 0.30f, mn.y + H * 0.24f), ImVec2(cx - W * 0.30f, mn.y + H * 0.54f), rim, 1.5f);
                dl->PathArcTo(ImVec2(cx, mn.y + H * 0.115f), W * 0.20f, 2.5f, 3.9f, 12);
                dl->PathStroke(rim, ImDrawFlags_None, 1.5f);
            };
            struct sample_t { float x, top, w, h; int style; bool fill; float health; bool armor; bool target; const char* info; const char* name; };
            const sample_t samples[] = {
                { 130.0f, 200.0f, 96.0f, 250.0f, 0, true,  0.72f, true,  false, "24 studs  \xC2\xB7  Rifle", "natware" },
                { 380.0f, 200.0f, 96.0f, 250.0f, 1, false, 0.28f, false, true,  "61 studs", "target_user" },
                { 630.0f, 200.0f, 96.0f, 250.0f, 2, false, 1.00f, false, false, "9 studs  \xC2\xB7  Knife", "glow_style" },
                { 880.0f, 270.0f, 46.0f, 120.0f, 0, true,  0.55f, false, false, "112 studs", "far_away" },
            };
            for (const sample_t& s : samples)
            {
                const ImVec2 mn(s.x, s.top), mx(s.x + s.w, s.top + s.h);
                figure(mn, mx);
                esp_draw::player_t look{};
                look.min = mn;
                look.max = mx;
                look.box = s.target ? IM_COL32(255, 70, 70, 255) : IM_COL32(255, 255, 255, 255);
                look.box_style = s.style;
                if (s.fill)
                    look.fill = IM_COL32(255, 255, 255, 40);
                look.name = s.name;
                look.name_col = IM_COL32(245, 245, 248, 255);
                look.accent = s.target ? IM_COL32(255, 70, 70, 255) : 0;   // only a target, a team or the whitelist colours a name
                look.health = s.health;
                look.bar_alpha = 1.0f;
                look.health_text = true;
                look.health_text_col = IM_COL32(245, 245, 248, 255);
                if (s.armor)
                {
                    look.armor = 0.45f;
                    look.armor_col = IM_COL32(90, 160, 255, 255);
                }
                look.info = s.info;
                look.info_col = IM_COL32(235, 235, 240, 255);
                look.flag = s.w > 60.0f ? "Running" : nullptr;
                look.flag_col = IM_COL32(200, 200, 210, 255);
                if (s.target)
                {
                    look.tag = "TARGET";
                    look.tag_col = IM_COL32(255, 70, 70, 255);
                }
                esp_draw::player(dl, look);
            }
        }, -1.0f, -1.0f, -1, -1.0f, 10, 3);
        ExitProcess(0);
    }

    if (strstr(GetCommandLineA(), "--external-ui")) {
        const Menu::PreviewPage& page = Menu::PreviewPages().front();
        Menu::PreviewSelect(page);
        render->running = true;
        capture("external-organized", [] { Menu::DrawMenu(); }, -1, -1, -1, -1, 10, 75);
        ExitProcess(0);
    }

    for (const Menu::PreviewPage& page : Menu::PreviewPages())
    {
        Menu::PreviewSelect(page);
        render->running = !page.closing;
        capture(page.name, [] { Menu::DrawWatermark(); Menu::DrawMenu(); }, page.mouse_x, page.mouse_y, page.button, page.drag_x, page.click_frame, page.frames);
        render->running = true;
        // Release any held button so the next page starts clean.
        ImGui::GetIO().AddMouseButtonEvent(0, false);
    }

    // Every icon at nav size and at 2x, for checking their shapes against Lucide.
    capture("x_icons", []
    {
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        dl->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2((float)kWidth, (float)kHeight), IM_COL32(14, 14, 18, 255));
        constexpr int kIcons = (int)ui::Icon::Move + 1;
        for (int i = 0; i < kIcons; i++)
        {
            ui::DrawIcon(dl, (ui::Icon)i, ImVec2(30.0f + i * 40.0f, 30.0f), ui::col::text, 0.95f);
            ui::DrawIcon(dl, (ui::Icon)i, ImVec2(30.0f + i * 40.0f, 80.0f), ui::col::text, 2.0f);
        }
    }, -1.0f, -1.0f, -1, -1.0f, 10, 3);

    // Skip static destructors: render_t would tear down a window/device it doesn't own.
    ExitProcess(0);
}
