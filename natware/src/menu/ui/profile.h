#pragma once

#include <d3d11.h>
#include <imgui/imgui.h>
#include <cstdint>
#include <string>

// The signed-in Roblox account shown at the bottom of the sidebar: its names and avatar
// headshot. In a game the local player is used; otherwise the account the Roblox app last
// signed in with (its LocalStorage\appStorage.json), so the sidebar knows you before you join.
namespace ui::profile
{
    struct Account
    {
        uint64_t user_id = 0;
        std::string display_name;
        std::string username;
    };

    // Starts the background worker that reads the account and downloads the headshot.
    void Init(ID3D11Device* device);

    // The in-game local player (user_id 0 when not in a game). Wins over the saved login.
    void SetInGame(uint64_t user_id, const std::string& display_name, const std::string& username);

    Account Current();

    // The headshot texture, or 0 until it has downloaded. Call on the render thread.
    ImTextureID Avatar();
}
