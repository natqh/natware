#include <thread>
#include <windows.h>
#include <memory/memory.h>
#include <Offsets/Offsets.hpp>
#include <sdk/sdk.h>
#include <game/game.h>
#include <cache/cache.h>
#include <render/render.h>
#include <settings.h>
#include <features/explorer/explorer.h>
#include <features/lighting/fog/fog.h>
#include <features/lighting/clocktime/clocktime.h>
#include <features/aimbot/aimbot.h>
#include <features/silentaim/silentaim.h>
#include <features/movement/movement.h>
#include <features/movement/gravity/gravity.h>
#include <features/rage/hitsound/hitsounds.h>
#include <features/rage/hittracers/hittracers.h>
#include <features/rage/orbit/orbit.h>
#include <features/rage/rapidfire/rapidfire.h>
#include <features/rage/hbe/hitbox_expander.h>
#include <features/rage/noclip/noclip.h>
#include <features/rage/spin360/spin360.h>
#include <features/rage/hipheight/hipheight.h>
#include <features/exploits/headless/headless.h>
#include <features/exploits/korblox/korblox.h>
#include "../src/menu/menu.h"
#include <game/rescan/rescan.h>
#include <features/lighting/shadows/shadows.h>
#include <features/lighting/exposure/exposure.h>
#include <features/lighting/skybox/skybox.h>
#include <features/exploits/antiafk/antiafk.h>
#include <features/exploits/client/fpscaps.h>
#include <features/exploits/freezeplayer/freezeplayer.h>
#include <features/triggerbot/triggerbot.h>
#include <gamesupport/gamesupport.h>
#include <gamesupport/LumberTycoon2/lt2.h>
#include <sdk/math/math.h>
#include <iostream>
#include <string>
#include <chrono>
#include "json.hpp"

using json = nlohmann::json;

namespace {
    bool should_render_ui() {
        HWND hwnd = GetForegroundWindow();
        HWND roblox_window = game::get_roblox_window();
        HWND overlay_window = render->detail->window;

        if (roblox_window != nullptr && IsWindow(roblox_window)) {
            return (hwnd == roblox_window || hwnd == overlay_window);
        }
        return true;
    }
}
