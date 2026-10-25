#include "game.h"
#include <memory/memory.h>
#include <Offsets/Offsets.hpp>

math::vector2 game::aim_origin(const math::vector2& dims)
{
	CURSORINFO info{};
	info.cbSize = sizeof(info);
	const bool hidden = GetCursorInfo(&info) && !(info.flags & CURSOR_SHOWING);
	if ((game::is_phantom_forces || hidden) && dims.x > 0.f && dims.y > 0.f)
		return { game::window_offset_x + dims.x * 0.5f, game::window_offset_y + dims.y * 0.5f };

	POINT cursor{};
	GetCursorPos(&cursor);
	return { static_cast<float>(cursor.x), static_cast<float>(cursor.y) };
}

HWND game::get_roblox_window()
{
	if (game::roblox_window && IsWindow(game::roblox_window))
	{
		return game::roblox_window;
	}

	HWND hwnd = nullptr;
	EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL
		{
			DWORD process_id = 0;
			GetWindowThreadProcessId(hwnd, &process_id);
			if (process_id == memory->get_process_id())
			{
				if (IsWindowVisible(hwnd)) {
					*reinterpret_cast<HWND*>(lParam) = hwnd;
					return FALSE;
				}
			}
			return TRUE;
		}, reinterpret_cast<LPARAM>(&hwnd));

	if (hwnd)
	{
		game::roblox_window = hwnd;
	}

	return hwnd;
}

void game::update_window_offset()
{
	HWND roblox_window = game::get_roblox_window();
	if (roblox_window)
	{
		RECT client_rect{};
		POINT client_pos{};
		if (GetClientRect(roblox_window, &client_rect))
		{
			client_pos.x = client_rect.left;
			client_pos.y = client_rect.top;
			ClientToScreen(roblox_window, &client_pos);
			game::window_offset_x = (float)client_pos.x;
			game::window_offset_y = (float)client_pos.y;
		}
	}
	else
	{
		game::window_offset_x = 0.0f;
		game::window_offset_y = 0.0f;
	}
}