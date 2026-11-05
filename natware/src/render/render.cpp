#define IMGUI_DEFINE_MATH_OPERATORS
#include <render/render.h>

#include <dwmapi.h>
#include <cstdio>
#include <chrono>
#include <thread>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <cfloat>
#include <random>

#include <settings.h>
#include <menu/ui/veil.h>
#include <features/esp/esp.h>
#include <features/silentaim/silentaim.h>
#include <features/triggerbot/triggerbot.h>
#include <cache/cache.h>
#include <cache/custom_entities/custom_entities.h>
#include <cache/bodyparts/bodyparts.h>
#include <features/esp/parser/meshparser.h>
#include <features/explorer/explorer.h>
#include <memory/memory.h>
#include <game/game.h>
#include <Offsets/Offsets.hpp>
#include <menu/keybind/keybind.h>
#include "../src/menu/menu.h"
#include <features/config/config.h>
#include <cstring>
#include <cstdlib>

namespace helper
{
	void draw_text_outlined(ImDrawList* draw, ImFont* font, float font_size, ImVec2 pos, ImU32 col, const char* text_begin, const char* text_end = nullptr);
	void box(ImVec2& c1, ImVec2& c2, ImU32 color);
	void corner_box(ImDrawList* draw, ImVec2 min, ImVec2 max, ImU32 col, float thickness = 1.f);
}
#include "../../ext/font/tahoma.h"
#include "../../ext/font/sp7.h"
#include "../../ext/font/arial.h"
#include "../../ext/font/config/font_config.h"
#include "../../ext/imgui/imgui.h"
#include "../../ext/imgui/imgui_internal.h"
#include "../../ext/imgui/misc/imgui_freetype.h"
#include "../../ext/imgui/addons/imgui_addons.h"
#include "textures/texture.h"
#include <features/aimbot/aimbot.h>
#include "../../ext/dicons/warn.h"
#include "3d/main_api.hpp"
#include "3d/parsers/obj_parser.hpp"
#include "3d/texture/texture_cache.hpp"
#include <features/avatarmanager/avatarmanager.h>
#include <wallcheck/wallcheck.h>
#include <menu/rardar/radar.h>
#include <features/rage/hittracers/hittracers.h>
#include <gamesupport/gamesupport.h>
#include <gamesupport/LumberTycoon2/lt2.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
	{
		return true;
	}

	switch (msg)
	{
	case WM_SYSCOMMAND:
		if ((wParam & 0xfff0) == SC_KEYMENU)
		{
			return 0;
		}
		break;

	case WM_SYSKEYDOWN:
		if (wParam == VK_F4) {
			settings::globals::quit_requested = true;
			return 0;
		}
		break;

	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	case WM_CLOSE:
		settings::globals::quit_requested = true;
		return 0;
	}

	return DefWindowProcA(hwnd, msg, wParam, lParam);
}

render_t::render_t()
{
	detail = std::make_unique<detail_t>();
}

render_t::~render_t()
{
	destroy_imgui();
	destroy_window();
	destroy_device();
}

bool render_t::create_window()
{
	static char rnd_class[12] = {};
	if (!rnd_class[0]) {
		std::mt19937 rng(static_cast<unsigned>(GetTickCount64()));
		std::uniform_int_distribution<int> dist('a', 'z');
		for (int i = 0; i < 8; i++) rnd_class[i] = static_cast<char>(dist(rng));
	}

	detail->window_class.cbSize = sizeof(detail->window_class);
	detail->window_class.style = CS_CLASSDC;
	detail->window_class.lpszClassName = rnd_class;
	detail->window_class.hInstance = GetModuleHandleA(0);
	detail->window_class.lpfnWndProc = wnd_proc;

	RegisterClassExA(&detail->window_class);

	detail->window = CreateWindowExA(
		WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_TOOLWINDOW,
		detail->window_class.lpszClassName,
		rnd_class,
		WS_POPUP,
		0,
		0,
		GetSystemMetrics(SM_CXSCREEN),
		GetSystemMetrics(SM_CYSCREEN),
		0,
		0,
		detail->window_class.hInstance,
		0
	);

	if (!detail->window)
	{
		return false;
	}

	SetLayeredWindowAttributes(detail->window, RGB(0, 0, 0), BYTE(255), LWA_ALPHA);

	RECT client_area{};
	RECT window_area{};

	GetClientRect(detail->window, &client_area);
	GetWindowRect(detail->window, &window_area);

	POINT diff{};
	ClientToScreen(detail->window, &diff);

	MARGINS margins
	{
		window_area.left + (diff.x - window_area.left),
		window_area.top + (diff.y - window_area.top),
		window_area.right,
		window_area.bottom,
	};

	DwmExtendFrameIntoClientArea(detail->window, &margins);

	ShowWindow(detail->window, SW_SHOW);
	UpdateWindow(detail->window);

	return true;
}

bool render_t::create_device()
{
	DXGI_SWAP_CHAIN_DESC swap_chain_desc{};

	swap_chain_desc.BufferCount = 1;

	swap_chain_desc.BufferDesc.Width = 0;
	swap_chain_desc.BufferDesc.Height = 0;
	swap_chain_desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

	swap_chain_desc.OutputWindow = detail->window;

	swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	swap_chain_desc.Windowed = 1;

	swap_chain_desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	swap_chain_desc.SampleDesc.Count = 2;
	swap_chain_desc.SampleDesc.Quality = 0;

	swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;

	D3D_FEATURE_LEVEL feature_level;
	D3D_FEATURE_LEVEL feature_level_list[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };

	HRESULT result = D3D11CreateDeviceAndSwapChain(
		nullptr,
		D3D_DRIVER_TYPE_HARDWARE,
		nullptr,
		0,
		feature_level_list,
		2,
		D3D11_SDK_VERSION,
		&swap_chain_desc,
		&detail->swap_chain,
		&detail->device,
		&feature_level,
		&detail->device_context
	);

	if (result == DXGI_ERROR_UNSUPPORTED)
	{
		result = D3D11CreateDeviceAndSwapChain(
			nullptr,
			D3D_DRIVER_TYPE_WARP,
			nullptr,
			0,
			feature_level_list,
			2,
			D3D11_SDK_VERSION,
			&swap_chain_desc,
			&detail->swap_chain,
			&detail->device,
			&feature_level,
			&detail->device_context
		);
	}

	if (result != S_OK)
	{
		MessageBoxA(nullptr, "This software can not run on your computer.", "Critical Problem", MB_ICONERROR | MB_OK);
	}

	ID3D11Texture2D* back_buffer{ nullptr };
	detail->swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer));

	if (back_buffer)
	{
		detail->device->CreateRenderTargetView(back_buffer, nullptr, &detail->render_target_view);
		back_buffer->Release();

		return true;
	}

	return false;
}

bool imgui_initialized = false;

bool render_t::create_imgui()
{
	using namespace ImGui;
	CreateContext();

	float main_scale = ImGui_ImplWin32_GetDpiScaleForMonitor(::MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));

	ImGuiStyle& style = ImGui::GetStyle();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.IniFilename = nullptr;

	ImGui::StyleColorsDark();

	style.WindowRounding = 0;
	style.ChildRounding = 0;
	style.FrameRounding = 0;
	style.ScrollbarRounding = 0;
	style.GrabRounding = 0;
	style.PopupRounding = 0;
	style.TabRounding = 0;

	style.WindowBorderSize = 1;
	style.FrameBorderSize = 1;
	style.PopupBorderSize = 1;

	style.WindowPadding = ImVec2(8, 8);
	style.ChildPadding = ImVec2(6, 6);
	style.FramePadding = ImVec2(2, 2);
	style.ItemSpacing = ImVec2(8, 6);
	style.ItemInnerSpacing = ImVec2(3, 3);
	style.CellPadding = ImVec2(2, 1);

	style.WindowMinSize = ImVec2(0, 0);
	style.GrabMinSize = 10;
	style.ScrollbarSize = 1;

	style.Colors[ImGuiCol_WindowBg] = ImColor(16, 16, 16);
	style.Colors[ImGuiCol_ChildBg] = ImColor(17, 17, 17);
	style.Colors[ImGuiCol_PopupBg] = ImColor(16, 16, 16);
	style.Colors[ImGuiCol_MenuBarBg] = ImColor(20, 20, 20);

	style.Colors[ImGuiCol_SliderGrab] = ImColor(118, 125, 216);
	style.Colors[ImGuiCol_SliderGrabActive] = style.Colors[ImGuiCol_SliderGrab];
	style.Colors[ImGuiCol_SliderGrabActive].w = 0.5f;
	style.Colors[ImGuiCol_TextSelectedBg] = style.Colors[ImGuiCol_SliderGrabActive];

	style.Colors[ImGuiCol_ScrollbarBg] = ImVec4();
	style.Colors[ImGuiCol_ScrollbarGrab] = style.Colors[ImGuiCol_SliderGrabActive];
	style.Colors[ImGuiCol_ScrollbarGrabHovered] = style.Colors[ImGuiCol_ScrollbarGrab];
	style.Colors[ImGuiCol_ScrollbarGrabActive] = style.Colors[ImGuiCol_ScrollbarGrab];

	style.Colors[ImGuiCol_Border] = ImColor(82, 82, 82, 150);
	style.Colors[ImGuiCol_BorderShadow] = ImColor(0, 0, 0);
	style.Colors[ImGuiCol_Separator] = ImColor(58, 58, 58);

	style.Colors[ImGuiCol_Text] = ImColor(255, 255, 255);
	style.Colors[ImGuiCol_TextDisabled] = ImColor(146, 146, 146);

	style.Colors[ImGuiCol_FrameBg] = ImColor(13, 13, 13);
	style.Colors[ImGuiCol_FrameBgHovered] = ImColor(16, 16, 16);
	style.Colors[ImGuiCol_FrameBgActive] = ImColor(11, 11, 11);
	style.Colors[ImGuiCol_FrameBgShadow] = ImColor(0, 0, 0, 140);

	style.Colors[ImGuiCol_Button] = ImColor(23, 23, 23);
	style.Colors[ImGuiCol_ButtonHovered] = ImColor(27, 27, 27);
	style.Colors[ImGuiCol_ButtonActive] = ImColor(20, 20, 20);
	style.Colors[ImGuiCol_ButtonShadow] = ImColor(0, 0, 0, 20);

	style.ScaleAllSizes(main_scale);
	style.FontScaleDpi = main_scale;

	io.Fonts->AddFontDefault();

	float dpi_scale = main_scale > 0.0f ? main_scale : 1.0f;

	// The ESP fonts are static arrays: ImGui must not free them when the context goes (it did,
	// and every clean exit ended in heap corruption).
	ImFontConfig font_cfg_tahoma;
	font_cfg_tahoma.FontDataOwnedByAtlas = false;
	font_cfg_tahoma.FontLoaderFlags = ImGuiFreeTypeLoaderFlags_MonoHinting | ImGuiFreeTypeLoaderFlags_Monochrome;
	font_cfg_tahoma.PixelSnapH = true;
	font_cfg_tahoma.OversampleH = 2;
	font_cfg_tahoma.OversampleV = 1;
	font_cfg_tahoma.RasterizerMultiply = 1.05f;
	font_cfg_tahoma.GlyphOffset = ImVec2(0, 1);
	esp_font_tahoma = io.Fonts->AddFontFromMemoryTTF(const_cast<void*>(static_cast<const void*>(rawData)), sizeof(rawData), font_config::tahoma::font_size / dpi_scale, &font_cfg_tahoma, nullptr);

	ImFontConfig font_cfg_sp7;
	font_cfg_sp7.FontDataOwnedByAtlas = false;
	font_cfg_sp7.FontLoaderFlags = ImGuiFreeTypeLoaderFlags_ForceAutoHint;
	font_cfg_sp7.GlyphOffset = ImVec2(0, 1);
	esp_font_sp7 = io.Fonts->AddFontFromMemoryTTF(const_cast<void*>(static_cast<const void*>(font_sp7)), font_sp7_size, font_config::sp7::font_size / dpi_scale, &font_cfg_sp7, nullptr);

	ImFontConfig font_cfg_arial;
	font_cfg_arial.FontDataOwnedByAtlas = false;
	font_cfg_arial.FontLoaderFlags = ImGuiFreeTypeLoaderFlags_ForceAutoHint;
	font_cfg_arial.GlyphOffset = ImVec2(0, 1);
	esp_font_arial = io.Fonts->AddFontFromMemoryTTF(const_cast<void*>(static_cast<const void*>(font_arial)), font_arial_size, font_config::arial::font_size / dpi_scale, &font_cfg_arial, nullptr);

	if (settings::visuals::esp_font == 0)
		esp_font = esp_font_tahoma;
	else if (settings::visuals::esp_font == 1)
		esp_font = esp_font_sp7;
	else
		esp_font = esp_font_arial;

	if (!ImGui_ImplWin32_Init(detail->window))
	{
		return false;
	}

	if (!detail->device || !detail->device_context)
	{
		return false;
	}

	if (!ImGui_ImplDX11_Init(detail->device, detail->device_context))
	{
		return false;
	}

	imgui_initialized = true;

	if (detail->device && detail->device_context)
	{
		g_avatar_manager = std::make_unique<AvatarManager>(detail->device, detail->device_context);
	}

	return true;
}

void render_t::destroy_device()
{
	if (detail->render_target_view) detail->render_target_view->Release();
	if (detail->swap_chain) detail->swap_chain->Release();
	if (detail->device_context) detail->device_context->Release();
	if (detail->device) detail->device->Release();
}

void render_t::destroy_window()
{
	DestroyWindow(detail->window);
	UnregisterClassA(detail->window_class.lpszClassName, detail->window_class.hInstance);
}

void render_t::destroy_imgui()
{
	// Menu::Shutdown tears down the same context first on most exits.
	if (!imgui_initialized || !ImGui::GetCurrentContext()) return;
	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
	imgui_initialized = false;
}

void render_t::start_render()
{
	MSG msg;
	while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	// The launcher and waiting card are always hidden from screen capture; the overlay (menu and
	// ESP) follows the setting. Only when it changes: it's a call into the window manager.
	static int applied_streamproof = -1;
	const bool hide = settings::menu::streamproof || settings::globals::card_showing;
	settings::globals::card_showing = false;   // the cards set it again as they draw
	if (applied_streamproof != (hide ? 1 : 0))
	{
		applied_streamproof = hide ? 1 : 0;
		SetWindowDisplayAffinity(detail->window, hide ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE);
	}

	ImGui_ImplDX11_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	esp_font = (settings::visuals::esp_font == 0) ? esp_font_tahoma : esp_font_sp7;

	if (GetAsyncKeyState(settings::menu::menu_keybind) & 1)
	{
		running = !running;

		if (running)
		{
			SetWindowLong(detail->window, GWL_EXSTYLE, WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT);
		}
		else
		{
			SetWindowLong(detail->window, GWL_EXSTYLE, WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_LAYERED);
		}
	}
}

void render_t::end_render()
{
	ImGui::Render();

	float clear_color[4]{ 0, 0, 0, 0 };
	detail->device_context->OMSetRenderTargets(1, &detail->render_target_view, nullptr);
	detail->device_context->ClearRenderTargetView(detail->render_target_view, clear_color);

	ui::veil::RenderDrawData(ImGui::GetDrawData(), detail->render_target_view);   // fades the menu as one layer

	detail->swap_chain->Present(settings::menu::vsync ? 1 : 0, 0);
}

void render_t::render_menu()
{
	Menu::DrawMenu();
}

static void draw_fov_circle(float fov_radius, float circle_colour[4], float outline_colour[4], bool attach_to_target = false)
{
	if (game::visengine.address == 0)
		return;

	if (fov_radius <= 0.0f || std::isnan(fov_radius) || std::isinf(fov_radius))
		return;

	ImDrawList* draw = ImGui::GetBackgroundDrawList();
	if (!draw)
		return;

	ImVec2 center{};

	try
	{
		if (attach_to_target && settings::silentaim::enabled && silentaim::state.data_ready && silentaim::state.target.instance.address != 0)
		{
			const math::matrix4 view = game::visengine.get_viewmatrix();
			const math::vector2 dims = game::visengine.get_dimensions();

			if (dims.x < 1.0f || dims.y < 1.0f)
			{
				POINT cursor{};
				if (!GetCursorPos(&cursor)) return;
				center = ImVec2(static_cast<float>(cursor.x), static_cast<float>(cursor.y));
			}
			else
			{
				math::vector2 target_screen_pos;
				if (game::visengine.world_to_screen(silentaim::state.target_world_pos, target_screen_pos, dims, view)
					&& !std::isnan(target_screen_pos.x) && !std::isnan(target_screen_pos.y))
				{
					center = ImVec2(target_screen_pos.x, target_screen_pos.y);
				}
				else
				{
					POINT cursor{};
					if (!GetCursorPos(&cursor)) return;
					center = ImVec2(static_cast<float>(cursor.x), static_cast<float>(cursor.y));
				}
			}
		}
		else
		{
			// Around the crosshair when the game locks the mouse, like the aimbot measures it.
			const math::vector2 origin = game::aim_origin(game::visengine.get_dimensions());
			center = ImVec2(origin.x, origin.y);
		}
	}
	catch (...)
	{
		return;
	}

	ImU32 outline_col = IM_COL32(
		static_cast<int>(outline_colour[0] * 255.f),
		static_cast<int>(outline_colour[1] * 255.f),
		static_cast<int>(outline_colour[2] * 255.f),
		static_cast<int>(outline_colour[3] * 255.f)
	);

	ImU32 circle_col = IM_COL32(
		static_cast<int>(circle_colour[0] * 255.f),
		static_cast<int>(circle_colour[1] * 255.f),
		static_cast<int>(circle_colour[2] * 255.f),
		static_cast<int>(circle_colour[3] * 255.f)
	);

	draw->AddCircle(center, fov_radius, outline_col, 0, 3.f);
	draw->AddCircle(center, fov_radius, circle_col, 0, 1.f);
}

static void draw_custom_cursor()
{
	if (!settings::silentaim::enabled || !silentaim::state.aim_indicator.address)
	{
		return;
	}

	bool is_visible = false;
	is_visible = memory->read<bool>(silentaim::state.aim_indicator.address + Offsets::GuiObject::Visible);

	if (!is_visible)
	{
		return;
	}

	POINT pt;
	if (!GetCursorPos(&pt))
	{
		return;
	}

	bool right_click_held = GetAsyncKeyState(VK_RBUTTON);
	float gap = right_click_held ? 4.0f : 10.0f;

	ImDrawList* draw = ImGui::GetBackgroundDrawList();
	ImU32 col = IM_COL32(255, 255, 255, 255);

	float dot_size = 4.0f;
	float line_width = 2.0f;
	float line_length = 10.0f;

	ImVec2 center = { (float)pt.x, (float)pt.y };

	ImVec2 dot_min(center.x - dot_size * 0.5f, center.y - dot_size * 0.5f);
	ImVec2 dot_max(center.x + dot_size * 0.5f, center.y + dot_size * 0.5f);
	draw->AddRectFilled(dot_min, dot_max, col, 0.0f);

	ImVec2 top_min(center.x - line_width * 0.5f, center.y - gap - line_length);
	ImVec2 top_max(center.x + line_width * 0.5f, center.y - gap);
	draw->AddRectFilled(top_min, top_max, col, 0.0f);

	ImVec2 bottom_min(center.x - line_width * 0.5f, center.y + gap);
	ImVec2 bottom_max(center.x + line_width * 0.5f, center.y + gap + line_length);
	draw->AddRectFilled(bottom_min, bottom_max, col, 0.0f);

	ImVec2 left_min(center.x - gap - line_length, center.y - line_width * 0.5f);
	ImVec2 left_max(center.x - gap, center.y + line_width * 0.5f);
	draw->AddRectFilled(left_min, left_max, col, 0.0f);

	ImVec2 right_min(center.x + gap, center.y - line_width * 0.5f);
	ImVec2 right_max(center.x + gap + line_length, center.y + line_width * 0.5f);
	draw->AddRectFilled(right_min, right_max, col, 0.0f);
}

static void draw_silent_aim_target_dot()
{
	if (!settings::silentaim::draw_target_dot || !settings::silentaim::enabled)
		return;

	if (!silentaim::state.data_ready || !silentaim::state.target.instance.address)
		return;

	if (!game::visengine.address)
		return;

	const math::matrix4 view = game::visengine.get_viewmatrix();
	const math::vector2 dims = game::visengine.get_dimensions();

	math::vector2 screen_pos;
	if (!game::visengine.world_to_screen(silentaim::state.target_world_pos, screen_pos, dims, view))
		return;

	if (screen_pos.x < 0.0f || screen_pos.y < 0.0f || screen_pos.x > dims.x || screen_pos.y > dims.y)
		return;

	ImDrawList* draw = ImGui::GetBackgroundDrawList();
	ImU32 dot_color = IM_COL32(
		static_cast<int>(settings::silentaim::target_dot_color[0] * 255.f),
		static_cast<int>(settings::silentaim::target_dot_color[1] * 255.f),
		static_cast<int>(settings::silentaim::target_dot_color[2] * 255.f),
		static_cast<int>(settings::silentaim::target_dot_color[3] * 255.f)
	);

	ImVec2 center(screen_pos.x, screen_pos.y);
	float dot_size = settings::silentaim::target_dot_size;

	draw->AddCircleFilled(center, dot_size, dot_color, 0);
}

static void draw_silent_aim_snap_line()
{
	if (!settings::silentaim::draw_snap_line || !settings::silentaim::enabled)
		return;

	if (!silentaim::state.data_ready || !silentaim::state.target.instance.address)
		return;

	if (!game::visengine.address)
		return;

	POINT cursor{};
	if (!GetCursorPos(&cursor))
		return;

	const math::matrix4 view = game::visengine.get_viewmatrix();
	const math::vector2 dims = game::visengine.get_dimensions();

	math::vector3 head_world_pos;
	if (!bodyparts::get_part_position(silentaim::state.target, "Head", head_world_pos))
		return;

	math::vector2 head_screen_pos;
	if (!game::visengine.world_to_screen(head_world_pos, head_screen_pos, dims, view))
		return;

	if (head_screen_pos.x < 0.0f || head_screen_pos.y < 0.0f || head_screen_pos.x > 10000.0f || head_screen_pos.y > 10000.0f)
		return;

	ImDrawList* draw = ImGui::GetBackgroundDrawList();
	ImVec2 cursor_pos(static_cast<float>(cursor.x), static_cast<float>(cursor.y));
	ImVec2 head_pos(head_screen_pos.x, head_screen_pos.y);

	float dx = head_pos.x - cursor_pos.x;
	float dy = head_pos.y - cursor_pos.y;
	float distance = std::sqrtf(dx * dx + dy * dy);

	if (distance > 0.0f)
	{
		const float dot_spacing = 15.0f;
		const float dot_radius = 3.0f;
		const int num_dots = static_cast<int>(distance / dot_spacing) + 1;

		ImDrawListFlags old_flags = draw->Flags;
		draw->Flags |= ImDrawListFlags_AntiAliasedFill | ImDrawListFlags_AntiAliasedLines;

		for (int i = 0; i <= num_dots; i++)
		{
			float t = static_cast<float>(i) / static_cast<float>(num_dots);
			ImVec2 dot_pos(
				cursor_pos.x + dx * t,
				cursor_pos.y + dy * t
			);

			ImU32 outer_glow_1 = IM_COL32(
				static_cast<int>(settings::silentaim::snap_line_color[0] * 255.f),
				static_cast<int>(settings::silentaim::snap_line_color[1] * 255.f),
				static_cast<int>(settings::silentaim::snap_line_color[2] * 255.f),
				static_cast<int>(settings::silentaim::snap_line_color[3] * 255.f * 0.08f)
			);

			ImU32 outer_glow_2 = IM_COL32(
				static_cast<int>(settings::silentaim::snap_line_color[0] * 255.f),
				static_cast<int>(settings::silentaim::snap_line_color[1] * 255.f),
				static_cast<int>(settings::silentaim::snap_line_color[2] * 255.f),
				static_cast<int>(settings::silentaim::snap_line_color[3] * 255.f * 0.15f)
			);

			ImU32 mid_glow = IM_COL32(
				static_cast<int>(settings::silentaim::snap_line_color[0] * 255.f),
				static_cast<int>(settings::silentaim::snap_line_color[1] * 255.f),
				static_cast<int>(settings::silentaim::snap_line_color[2] * 255.f),
				static_cast<int>(settings::silentaim::snap_line_color[3] * 255.f * 0.3f)
			);

			ImU32 inner_glow = IM_COL32(
				static_cast<int>(settings::silentaim::snap_line_color[0] * 255.f),
				static_cast<int>(settings::silentaim::snap_line_color[1] * 255.f),
				static_cast<int>(settings::silentaim::snap_line_color[2] * 255.f),
				static_cast<int>(settings::silentaim::snap_line_color[3] * 255.f * 0.5f)
			);

			ImU32 core_color = IM_COL32(
				static_cast<int>(settings::silentaim::snap_line_color[0] * 255.f),
				static_cast<int>(settings::silentaim::snap_line_color[1] * 255.f),
				static_cast<int>(settings::silentaim::snap_line_color[2] * 255.f),
				static_cast<int>(settings::silentaim::snap_line_color[3] * 255.f * 0.8f)
			);

			draw->AddCircleFilled(dot_pos, dot_radius + 7.0f, outer_glow_1, 0);
			draw->AddCircleFilled(dot_pos, dot_radius + 5.0f, outer_glow_2, 0);
			draw->AddCircleFilled(dot_pos, dot_radius + 3.0f, mid_glow, 0);
			draw->AddCircleFilled(dot_pos, dot_radius + 1.5f, inner_glow, 0);
			draw->AddCircleFilled(dot_pos, dot_radius, core_color, 0);
		}

		draw->Flags = old_flags;
	}
}

static ID3D11ShaderResourceView* g_warn_icon_texture = nullptr;
static bool g_warn_icon_loaded = false;
std::unique_ptr<AvatarManager> g_avatar_manager = nullptr;

static void load_warn_icon()
{
	if (g_warn_icon_loaded || !render || !render->detail || !render->detail->device)
		return;

	const unsigned char* icon_data = &warn_icon_data[0];
	constexpr size_t icon_data_size = 9621;

	g_warn_icon_texture = D3D11CreateTextureFromBytes(
		render->detail->device,
		icon_data,
		icon_data_size
	);

	if (g_warn_icon_texture)
		g_warn_icon_loaded = true;
}

static ImU32 get_blink_color(float speed = 2.0f)
{
	float time = static_cast<float>(ImGui::GetTime()) * speed;
	float cycle = fmodf(time, 2.0f);

	if (cycle < 1.0f)
	{
		return IM_COL32(255, 0, 0, 255);
	}
	else
	{
		return IM_COL32(0, 0, 0, 255);
	}
}

static void draw_target_warning_icon()
{
	if (!settings::visuals::target_warning_icon)
		return;

	if (!game::visengine.address)
		return;

	load_warn_icon();

	if (!g_warn_icon_texture)
		return;

	cache::entity_t target_entity{};
	bool has_target = false;

	if (settings::aimbot::enabled && aimbot::player.instance.address != 0)
	{
		target_entity = aimbot::player;
		has_target = true;
	}
	else if (settings::silentaim::enabled && silentaim::state.data_ready && silentaim::state.target.instance.address != 0)
	{
		target_entity = silentaim::state.target;
		has_target = true;
	}

	if (!has_target)
		return;

	const math::matrix4 view = game::visengine.get_viewmatrix();
	const math::vector2 dims = game::visengine.get_dimensions();

	float left = FLT_MAX, top = FLT_MAX;
	float right = -FLT_MAX, bottom = -FLT_MAX;
	bool valid = false;

	static const math::vector3 corners[] = {
		{ -1, -1, -1 }, { 1, -1, -1 }, { 1, 1, -1 }, { -1, 1, -1 },
		{ -1, -1, 1 }, { 1, -1, 1 }, { 1, 1, 1 }, { -1, 1, 1 }
	};

	for (auto& parts : target_entity.parts)
	{
		rbx::part_t part = parts.second;
		rbx::primitive_t prim = part.get_primitive();
		auto size = prim.get_size();
		auto pos = prim.get_position();
		auto rot = prim.get_rotation();

		if (size.x == 0 || size.y == 0 || size.z == 0)
			continue;

		for (auto& corner : corners)
		{
			math::vector3 world = pos + rot * math::vector3
			{
				corner.x * size.x * 0.5f,
				corner.y * size.y * 0.5f,
				corner.z * size.z * 0.5f
			};

			math::vector2 out{};
			if (game::visengine.world_to_screen(world, out, dims, view))
			{
				valid = true;
				left = min(left, out.x);
				top = min(top, out.y);
				right = max(right, out.x);
				bottom = max(bottom, out.y);
			}
		}
	}

	if (!valid || left >= right || top >= bottom)
		return;

	float box_center_x = (left + right) * 0.5f;
	float box_top = top;

	ImFont* font;
	float font_size;
	if (settings::visuals::esp_font == 0)
	{
		font = esp_font_tahoma;
		font_size = font_config::tahoma::font_size;
	}
	else if (settings::visuals::esp_font == 1)
	{
		font = esp_font_sp7;
		font_size = font_config::sp7::font_size;
	}
	else
	{
		font = esp_font_arial;
		font_size = font_config::arial::font_size;
	}
	if (!font)
		font = ImGui::GetFont();

	std::string name_to_display = target_entity.display_name.empty() ? target_entity.name : target_entity.display_name;
	float text_width = font->CalcTextSizeA(font_size, FLT_MAX, 0.f, name_to_display.c_str()).x;
	float name_y = box_top - font_size - 5.f;

	float icon_size = 24.0f;
	float icon_y = name_y - icon_size - 3.0f;

	ImDrawList* draw = ImGui::GetBackgroundDrawList();
	ImVec2 icon_min(box_center_x - icon_size * 0.5f, icon_y);
	ImVec2 icon_max(box_center_x + icon_size * 0.5f, icon_y + icon_size);

	ImU32 blink_color = get_blink_color(2.0f);
	draw->AddImage(reinterpret_cast<ImTextureID>(g_warn_icon_texture), icon_min, icon_max, ImVec2(0, 0), ImVec2(1, 1), blink_color);
}

void render_t::render_visuals()
{
	custom_entities::update_containers();
	if (g_avatar_manager)
	{
		g_avatar_manager->update();
	}
	radar::run();
	esp::run();
	if (settings::aimbot::draw_fov && settings::aimbot::enabled)
	{
		draw_fov_circle(settings::aimbot::fov, settings::aimbot::fov_circle_colour, settings::aimbot::fov_outline_colour);
	}

	if (settings::silentaim::draw_fov && settings::silentaim::enabled)
	{
		draw_fov_circle(settings::silentaim::fov, settings::silentaim::fov_circle_colour, settings::silentaim::fov_outline_colour, settings::silentaim::attach_fov_to_target);
	}

	draw_custom_cursor();
	draw_silent_aim_target_dot();

	draw_target_warning_icon();

	// The map behind the radar and every wall check is read on its own thread.
	wallcheck->tick();

	rage::draw_hit_tracers();

	Menu::DrawWatermark();
}