#include "physical_mouse.h"

#include <windows.h>
#include <atomic>
#include <thread>

namespace
{
	std::atomic<std::uint32_t> g_moves{ 0 };
	std::atomic<bool> g_started{ false };

	LRESULT CALLBACK hook(int code, WPARAM wparam, LPARAM lparam)
	{
		if (code == HC_ACTION && wparam == WM_MOUSEMOVE)
		{
			const auto* info = reinterpret_cast<const MSLLHOOKSTRUCT*>(lparam);
			if (!(info->flags & LLMHF_INJECTED))   // natware's own SendInput moves are injected
				g_moves.fetch_add(1, std::memory_order_relaxed);
		}
		return CallNextHookEx(nullptr, code, wparam, lparam);
	}
}

void physical_mouse::start()
{
	if (g_started.exchange(true))
		return;
	std::thread([]
	{
		HHOOK h = SetWindowsHookExW(WH_MOUSE_LL, hook, GetModuleHandleW(nullptr), 0);
		if (!h)
			return;
		MSG msg;
		while (GetMessageW(&msg, nullptr, 0, 0) > 0)
		{
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
		UnhookWindowsHookEx(h);
	}).detach();
}

std::uint32_t physical_mouse::moves()
{
	return g_moves.load(std::memory_order_relaxed);
}
