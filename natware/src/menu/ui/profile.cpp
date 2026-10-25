#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "profile.h"

#include <Windows.h>
#include <wininet.h>
#include <json/json.hpp>
#include <imgui/stb_image.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#pragma comment(lib, "wininet.lib")

namespace
{
    using ui::profile::Account;

    std::mutex     g_mutex;
    ID3D11Device*  g_device = nullptr;
    Account        g_in_game;   // set by the render thread
    Account        g_saved;     // the Roblox app's last sign-in

    // The worker leaves decoded pixels here; the render thread uploads them.
    uint64_t                   g_pixels_for = 0;
    std::vector<unsigned char> g_pixels;
    int                        g_pixels_w = 0, g_pixels_h = 0;

    uint64_t                   g_texture_for = 0;
    ID3D11ShaderResourceView*  g_texture = nullptr;

    // Cookies are never sent: the thumbnail API is public.
    std::string HttpGet(const std::string& url)
    {
        HINTERNET net = InternetOpenA("Mozilla/5.0", INTERNET_OPEN_TYPE_PRECONFIG, nullptr, nullptr, 0);
        if (!net)
            return {};
        std::string body;
        if (HINTERNET req = InternetOpenUrlA(net, url.c_str(), nullptr, 0,
                INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_SECURE | INTERNET_FLAG_NO_COOKIES, 0))
        {
            DWORD status = 0, size = sizeof(status);
            HttpQueryInfoA(req, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &size, nullptr);
            if (status == 200)
            {
                char buf[8192];
                DWORD read = 0;
                while (InternetReadFile(req, buf, sizeof(buf), &read) && read > 0)
                    body.append(buf, read);
            }
            InternetCloseHandle(req);
        }
        InternetCloseHandle(net);
        return body;
    }

    // Reads only UserId, Username and DisplayName. The file also carries the app's sign-in
    // data, which is never touched.
    Account ReadSavedLogin()
    {
        Account a;
        char* local = nullptr;
        size_t len = 0;
        if (_dupenv_s(&local, &len, "LOCALAPPDATA") != 0 || !local)
            return a;
        const std::string path = std::string(local) + "\\Roblox\\LocalStorage\\appStorage.json";
        free(local);

        std::ifstream file(path, std::ios::binary);
        if (!file)
            return a;
        std::stringstream text;
        text << file.rdbuf();
        const nlohmann::json j = nlohmann::json::parse(text.str(), nullptr, false);
        if (j.is_discarded() || !j.is_object())
            return a;

        auto field = [&](const char* key) -> std::string
        {
            const auto it = j.find(key);
            if (it == j.end()) return {};
            if (it->is_string()) return it->get<std::string>();
            if (it->is_number_unsigned()) return std::to_string(it->get<uint64_t>());
            return {};
        };
        a.user_id = strtoull(field("UserId").c_str(), nullptr, 10);
        a.username = field("Username");
        a.display_name = field("DisplayName");
        return a;
    }

    bool DownloadHeadshot(uint64_t user_id, std::vector<unsigned char>& rgba, int& w, int& h)
    {
        const std::string meta = HttpGet("https://thumbnails.roblox.com/v1/users/avatar-headshot?userIds="
            + std::to_string(user_id) + "&size=150x150&format=Png&isCircular=false");
        const nlohmann::json j = nlohmann::json::parse(meta, nullptr, false);
        if (j.is_discarded() || !j.is_object() || !j.contains("data") || !j["data"].is_array() || j["data"].empty())
            return false;
        const nlohmann::json& entry = j["data"][0];
        if (!entry.is_object() || entry.value("state", std::string()) != "Completed"
            || !entry.contains("imageUrl") || !entry["imageUrl"].is_string())
            return false;

        const std::string png = HttpGet(entry["imageUrl"].get<std::string>());
        if (png.empty())
            return false;
        int channels = 0;
        unsigned char* px = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(png.data()), (int)png.size(), &w, &h, &channels, 4);
        if (!px)
            return false;
        rgba.assign(px, px + (size_t)w * h * 4);
        stbi_image_free(px);
        return true;
    }

    uint64_t WantedId()
    {
        return g_in_game.user_id != 0 ? g_in_game.user_id : g_saved.user_id;
    }

    void Worker()
    {
        uint64_t fetched_for = 0;
        int failures = 0;
        auto next_read = std::chrono::steady_clock::time_point{};
        for (;;)
        {
            const auto now = std::chrono::steady_clock::now();
            if (now >= next_read)
            {
                Account saved = ReadSavedLogin();
                std::lock_guard<std::mutex> lock(g_mutex);
                g_saved = std::move(saved);
                next_read = now + std::chrono::seconds(30);
            }

            uint64_t want = 0;
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                want = WantedId();
            }
            if (want != 0 && want != fetched_for)
            {
                std::vector<unsigned char> rgba;
                int w = 0, h = 0;
                if (DownloadHeadshot(want, rgba, w, h))
                {
                    std::lock_guard<std::mutex> lock(g_mutex);
                    g_pixels = std::move(rgba);
                    g_pixels_w = w;
                    g_pixels_h = h;
                    g_pixels_for = want;
                    fetched_for = want;
                    failures = 0;
                }
                else if (++failures >= 6)
                {
                    // A fresh avatar can sit in "Pending" for a while; stop after ~12 s.
                    fetched_for = want;
                    failures = 0;
                }
            }
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    }

    // A box-filtered mip chain (in premultiplied alpha, so the transparent backdrop doesn't
    // bleed a dark fringe), so the 150px headshot stays smooth drawn at ~30px.
    ID3D11ShaderResourceView* Upload(const std::vector<unsigned char>& rgba, int w, int h)
    {
        std::vector<std::vector<unsigned char>> levels;
        std::vector<D3D11_SUBRESOURCE_DATA> data;
        std::vector<float> cur((size_t)w * h * 4);
        for (size_t i = 0; i < (size_t)w * h; i++)
        {
            const float a = rgba[i * 4 + 3] / 255.0f;
            for (int c = 0; c < 3; c++)
                cur[i * 4 + c] = rgba[i * 4 + c] / 255.0f * a;
            cur[i * 4 + 3] = a;
        }

        int lw = w, lh = h;
        for (;;)
        {
            std::vector<unsigned char> out((size_t)lw * lh * 4);
            for (size_t i = 0; i < (size_t)lw * lh; i++)
            {
                const float a = cur[i * 4 + 3];
                for (int c = 0; c < 3; c++)
                    out[i * 4 + c] = (unsigned char)(a > 0.0f ? std::min(1.0f, cur[i * 4 + c] / a) * 255.0f + 0.5f : 0.0f);
                out[i * 4 + 3] = (unsigned char)(a * 255.0f + 0.5f);
            }
            levels.push_back(std::move(out));
            if (lw == 1 && lh == 1)
                break;

            const int nw = std::max(1, lw / 2), nh = std::max(1, lh / 2);
            std::vector<float> next((size_t)nw * nh * 4);
            for (int y = 0; y < nh; y++)
                for (int x = 0; x < nw; x++)
                    for (int c = 0; c < 4; c++)
                    {
                        float sum = 0.0f;
                        for (int dy = 0; dy < 2; dy++)
                            for (int dx = 0; dx < 2; dx++)
                            {
                                const int sx = std::min(lw - 1, x * 2 + dx), sy = std::min(lh - 1, y * 2 + dy);
                                sum += cur[((size_t)sy * lw + sx) * 4 + c];
                            }
                        next[((size_t)y * nw + x) * 4 + c] = sum * 0.25f;
                    }
            cur = std::move(next);
            lw = nw;
            lh = nh;
        }

        lw = w;
        for (const auto& level : levels)
        {
            data.push_back({ level.data(), (UINT)lw * 4, 0 });
            lw = std::max(1, lw / 2);
        }

        D3D11_TEXTURE2D_DESC td{};
        td.Width = w;
        td.Height = h;
        td.MipLevels = (UINT)levels.size();
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_IMMUTABLE;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        ID3D11Texture2D* tex = nullptr;
        ID3D11ShaderResourceView* srv = nullptr;
        if (SUCCEEDED(g_device->CreateTexture2D(&td, data.data(), &tex)))
        {
            g_device->CreateShaderResourceView(tex, nullptr, &srv);
            tex->Release();
        }
        return srv;
    }
}

void ui::profile::Init(ID3D11Device* device)
{
    static bool started = false;
    g_device = device;
    if (!started)
    {
        started = true;
        std::thread(Worker).detach();
    }
}

void ui::profile::SetInGame(uint64_t user_id, const std::string& display_name, const std::string& username)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_in_game.user_id = user_id;
    g_in_game.display_name = display_name;
    g_in_game.username = username;
}

ui::profile::Account ui::profile::Current()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return !g_in_game.username.empty() ? g_in_game : g_saved;
}

ImTextureID ui::profile::Avatar()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_device && g_pixels_for != 0 && g_pixels_for != g_texture_for && !g_pixels.empty())
    {
        if (ID3D11ShaderResourceView* srv = Upload(g_pixels, g_pixels_w, g_pixels_h))
        {
            if (g_texture)
                g_texture->Release();
            g_texture = srv;
            g_texture_for = g_pixels_for;
        }
        g_pixels.clear();
    }
    return (g_texture && g_texture_for == WantedId()) ? (ImTextureID)(intptr_t)g_texture : (ImTextureID)0;
}
