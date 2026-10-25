#include "rapidfire.h"

#include <game/game.h>
#include <sdk/sdk.h>
#include <memory/memory.h>
#include <Offsets/Offsets.hpp>
#include <render/render.h>
#include <settings.h>
#include <menu/keybind/keybind.h>

#include <windows.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>

// Two ways to fire faster, picked in the menu:
//  - Gun values: guns that keep their fire delay in values (ShootingCooldown and
//    ToleranceCooldown, as in Da Hood and its copies) get them set to almost zero. The gun
//    in your hand lives in your character, not the backpack, so both are patched.
//  - Auto-click: while you hold the left button in Roblox, natware clicks for you at the
//    chosen rate. That speeds up semi-auto guns in any game.
namespace
{
	std::atomic<int> g_patched{ 0 };
	std::atomic<bool> g_found_values{ false };
	std::atomic<bool> g_clicking{ false };

	// The left button as you press it. Clicks natware sends itself (auto-click, triggerbot)
	// arrive flagged as injected and are ignored here, so they never look like your hold.
	std::atomic<bool> g_left_held{ false };
	std::atomic<bool> g_hook_started{ false };

	LRESULT CALLBACK mouse_hook(int code, WPARAM wparam, LPARAM lparam)
	{
		if (code == HC_ACTION)
		{
			const auto* info = reinterpret_cast<const MSLLHOOKSTRUCT*>(lparam);
			if (!(info->flags & LLMHF_INJECTED))
			{
				if (wparam == WM_LBUTTONDOWN)
					g_left_held = true;
				else if (wparam == WM_LBUTTONUP)
					g_left_held = false;
			}
		}
		return CallNextHookEx(nullptr, code, wparam, lparam);
	}

	void hook_thread()
	{
		HHOOK hook = SetWindowsHookExW(WH_MOUSE_LL, mouse_hook, GetModuleHandleW(nullptr), 0);
		if (!hook)
			return;
		MSG msg;
		while (GetMessageW(&msg, nullptr, 0, 0) > 0)
		{
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
		UnhookWindowsHookEx(hook);
	}

	void send_left(bool down)
	{
		INPUT input{};
		input.type = INPUT_MOUSE;
		input.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
		SendInput(1, &input, sizeof(INPUT));
	}

	bool roblox_in_front()
	{
		HWND roblox = game::get_roblox_window();
		return roblox && GetForegroundWindow() == roblox && !render->running;
	}

	// Sets the cooldown values on every tool in `container`; returns how many it set.
	int patch_tools(rbx::instance_t container)
	{
		int patched = 0;
		if (!container.address)
			return 0;

		for (rbx::instance_t tool : container.get_children())
		{
			if (tool.get_class_name() != "Tool")
				continue;

			for (const char* name : { "ShootingCooldown", "ToleranceCooldown" })
			{
				rbx::instance_t value = tool.find_first_child(name);
				if (!value.address)
					continue;
				g_found_values = true;
				value.set_instance_double(0.0001);
				++patched;
			}
		}
		return patched;
	}

	void patch_gun_values()
	{
		if (!game::local_player.address)
			return;

		int patched = patch_tools(game::local_player.find_first_child("Backpack"));
		rbx::model_instance_t character = rbx::player_t(game::local_player.address).get_model_instance();
		patched += patch_tools(character);
		g_patched = patched;
	}
}

void rage::rapidfire::run()
{
	using clock = std::chrono::steady_clock;
	auto next_patch = clock::now();
	keybind::keybind_t key_state{};

	while (true)
	{
		const int mode = settings::rage::rapidfire_mode;   // 0 gun values, 1 auto-click, 2 both
		const bool on = keybind::feature_on(settings::rage::rapidfire, settings::rage::rapidfire_key, settings::rage::rapidfire_key_mode, key_state);
		const bool values = on && (mode == 0 || mode == 2);
		const bool clicks = on && (mode == 1 || mode == 2);

		if (!on)
		{
			g_patched = 0;
			g_found_values = false;
		}

		if (values && clock::now() >= next_patch)
		{
			try { patch_gun_values(); } catch (...) {}
			next_patch = clock::now() + std::chrono::milliseconds(50);
		}

		if (clicks && !g_hook_started.exchange(true))
			std::thread(hook_thread).detach();

		if (clicks && g_left_held && roblox_in_front())
		{
			g_clicking = true;
			const float cps = (std::max)(1.0f, (std::min)(settings::rage::rapidfire_cps, 40.0f));
			const auto interval = std::chrono::duration<float>(1.0f / cps);
			const auto started = clock::now();

			// One extra click: let go, then press again only if you're still holding.
			send_left(false);
			std::this_thread::sleep_for(std::chrono::milliseconds(8));
			if (g_left_held)
				send_left(true);

			const auto rest = std::chrono::duration_cast<std::chrono::microseconds>(interval - (clock::now() - started));
			if (rest.count() > 0)
				std::this_thread::sleep_for(rest);
			continue;
		}

		g_clicking = false;
		std::this_thread::sleep_for(std::chrono::milliseconds(values || clicks ? 5 : 50));
	}
}

std::string rage::rapidfire::status()
{
	if (!settings::rage::rapidfire)
		return "";
	const int mode = settings::rage::rapidfire_mode;
	std::string text;
	if (mode == 0 || mode == 2)
	{
		char buf[96];
		if (g_patched > 0)
			snprintf(buf, sizeof(buf), "Gun values: %d set", g_patched.load());
		else
			snprintf(buf, sizeof(buf), "Gun values: none in this game");
		text = buf;
	}
	if (mode == 1 || mode == 2)
	{
		if (!text.empty())
			text += "  \xC2\xB7  ";
		text += g_clicking ? "Auto-click: firing" : "Auto-click: hold left click";
	}
	return text;
}
