#pragma once

#include <D3D11.h>
#include <vector>

class Menu
{
public:
	static bool Initialize(HWND hWnd, ID3D11Device* pDevice, ID3D11DeviceContext* pDeviceContext);
	static void Shutdown();

public:
	static void	DrawMenu();
	static void DrawWatermark();
	static void DrawModePicker();
	static void DrawWaiting(const char* status);   // the card shown while waiting on Roblox
static void DrawUpdatePrompt();   // every frame in External: offers a release that came out while open
	static void DrawExecutorUI();
	// True while the menu is closed but still fading out, so it should keep being drawn.
	static bool IsClosing();

	// Offscreen screenshot support (menu_preview::run).
	// mouse_x/y: canvas point to hover (negative = no mouse); button: click there (-1 = hover only);
	// drag_x: if set, press there and sweep the cursor to this x without releasing;
	// click_frame: which captured frame the click lands on; frames: how many frames to run
	// before the shot (45 lets animations settle; fewer catches one mid-flight).
	// closing: the menu is told to close at the start, to catch its fade-out.
	struct PreviewPage { const char* name; int tab; int sub; float mouse_x = -1.0f; float mouse_y = -1.0f; int button = -1; float drag_x = -1.0f; int click_frame = 10; int frames = 45; bool closing = false; };
	static const std::vector<PreviewPage>& PreviewPages();
	static void PreviewSelect(const PreviewPage& page);

public:
	inline static bool m_bMenuVisible = false;

private:
	inline static bool m_bInitialized = false;
};