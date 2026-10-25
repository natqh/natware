#include "keybind.h"
#include "../../ext/imgui/addons/imgui_addons.h"
#include "../../ext/imgui/imgui.h"
#include "../../ext/imgui/imgui_internal.h"
#include <cstdio>
#include <vector>
#include <map>
#include <cstring>
#include <game/game.h>

namespace keybind
{
	key_data_t key_names[] = {
		{ "none", 0 },
		{ "lm", VK_LBUTTON },
		{ "rm", VK_RBUTTON },
		{ "mm", VK_MBUTTON },
		{ "mb1", VK_XBUTTON1 },
		{ "mb2", VK_XBUTTON2 },
		{ "backspace", VK_BACK },
		{ "tab", VK_TAB },
		{ "enter", VK_RETURN },
		{ "shift", VK_SHIFT },
		{ "ctrl", VK_CONTROL },
		{ "alt", VK_MENU },
		{ "pause", VK_PAUSE },
		{ "caps lock", VK_CAPITAL },
		{ "escape", VK_ESCAPE },
		{ "space", VK_SPACE },
		{ "page up", VK_PRIOR },
		{ "page down", VK_NEXT },
		{ "end", VK_END },
		{ "home", VK_HOME },
		{ "left arrow", VK_LEFT },
		{ "up arrow", VK_UP },
		{ "right arrow", VK_RIGHT },
		{ "down arrow", VK_DOWN },
		{ "insert", VK_INSERT },
		{ "delete", VK_DELETE },
		{ "0", '0' },
		{ "1", '1' },
		{ "2", '2' },
		{ "3", '3' },
		{ "4", '4' },
		{ "5", '5' },
		{ "6", '6' },
		{ "7", '7' },
		{ "8", '8' },
		{ "9", '9' },
		{ "a", 'A' },
		{ "b", 'B' },
		{ "c", 'C' },
		{ "d", 'D' },
		{ "e", 'E' },
		{ "f", 'F' },
		{ "g", 'G' },
		{ "h", 'H' },
		{ "i", 'I' },
		{ "j", 'J' },
		{ "k", 'K' },
		{ "l", 'L' },
		{ "m", 'M' },
		{ "n", 'N' },
		{ "o", 'O' },
		{ "p", 'P' },
		{ "q", 'Q' },
		{ "r", 'R' },
		{ "s", 'S' },
		{ "t", 'T' },
		{ "u", 'U' },
		{ "v", 'V' },
		{ "w", 'W' },
		{ "x", 'X' },
		{ "y", 'Y' },
		{ "z", 'Z' },
		{ "left Win", VK_LWIN },
		{ "right Win", VK_RWIN },
		{ "apps", VK_APPS },
		{ "numpad 0", VK_NUMPAD0 },
		{ "numpad 1", VK_NUMPAD1 },
		{ "numpad 2", VK_NUMPAD2 },
		{ "numpad 3", VK_NUMPAD3 },
		{ "numpad 4", VK_NUMPAD4 },
		{ "numpad 5", VK_NUMPAD5 },
		{ "numpad 6", VK_NUMPAD6 },
		{ "numpad 7", VK_NUMPAD7 },
		{ "numpad 8", VK_NUMPAD8 },
		{ "numpad 9", VK_NUMPAD9 },
		{ "multiply", VK_MULTIPLY },
		{ "add", VK_ADD },
		{ "separator", VK_SEPARATOR },
		{ "subtract", VK_SUBTRACT },
		{ "decimal", VK_DECIMAL },
		{ "divide", VK_DIVIDE },
		{ "f1", VK_F1 },
		{ "f2", VK_F2 },
		{ "f3", VK_F3 },
		{ "f4", VK_F4 },
		{ "f5", VK_F5 },
		{ "f6", VK_F6 },
		{ "f7", VK_F7 },
		{ "f8", VK_F8 },
		{ "f9", VK_F9 },
		{ "f10", VK_F10 },
		{ "f11", VK_F11 },
		{ "f12", VK_F12 },
		{ "num Lock", VK_NUMLOCK },
		{ "scroll Lock", VK_SCROLL },
		{ "semicolon", VK_OEM_1 },
		{ "squals", VK_OEM_PLUS },
		{ "comma", VK_OEM_COMMA },
		{ "minus", VK_OEM_MINUS },
		{ "period", VK_OEM_PERIOD },
		{ "slash", VK_OEM_2 },
		{ "tilde", VK_OEM_3 },
		{ "left bracket", VK_OEM_4 },
		{ "backslash", VK_OEM_5 },
		{ "right bracket", VK_OEM_6 },
		{ "quote", VK_OEM_7 }
	};

	int key_count = static_cast<int>(sizeof(key_names) / sizeof(key_names[0]));

	const char* get_key_name(int key)
	{
		if (key == VK_LSHIFT || key == VK_RSHIFT)
			key = VK_SHIFT;

		if (key == VK_LCONTROL || key == VK_RCONTROL)
			key = VK_CONTROL;

		if (key == VK_LMENU || key == VK_RMENU)
			key = VK_MENU;

		if (key == 0)
			return "None";

		for (int i = 0; i < key_count; i++)
		{
			if (key_names[i].value == key)
			{
				return key_names[i].name;
			}
		}

		if (key >= 'A' && key <= 'Z')
		{
			static char key_buf[2] = { 0 };
			key_buf[0] = static_cast<char>(key + 32);
			return key_buf;
		}

		if (key >= '0' && key <= '9')
		{
			static char key_buf[2] = { 0 };
			key_buf[0] = static_cast<char>(key);
			return key_buf;
		}

		if (key >= VK_F1 && key <= VK_F12)
		{
			static char f_key[4] = { 0 };
			sprintf_s(f_key, "f%d", key - VK_F1 + 1);
			return f_key;
		}

		return "Unknown";
	}

	bool keybind_selector(const char* label, int* key, int* activation_mode, const ImVec2& size_arg)
	{
		using namespace ImGui;

		ImGuiWindow* window = GetCurrentWindow();
		if (window->SkipItems)
			return false;

		ImGuiContext& g = *GImGui;
		const ImGuiStyle& style = g.Style;
		const ImGuiID id = window->GetID(label);

		char buf_display[32] = "[unbinded]";
		bool is_selecting = false;

		if (*key != 0 && g.ActiveId != id)
		{
			const char* key_name = get_key_name(*key);
			snprintf(buf_display, sizeof(buf_display), "[%s]", key_name);
		}
		else if (g.ActiveId == id)
		{
			is_selecting = true;
			strcpy_s(buf_display, sizeof(buf_display), "[...]");
		}

		ImVec2 buf_display_size = CalcTextSize(buf_display, NULL, true);
		ImVec2 size = CalcItemSize(size_arg, buf_display_size.x + style.FramePadding.x * 2.0f, g.FontSize + style.FramePadding.y * 2.0f);
		ImRect frame_bb(window->DC.CursorPos, window->DC.CursorPos + size);
		ImRect total_bb(frame_bb.Min, frame_bb.Max);

		ItemSize(total_bb);
		if (!ItemAdd(total_bb, id))
			return false;

		const bool hovered = ItemHoverable(frame_bb, id, 0);

		ImVec4 colLabel = GetStyleColorVec4(is_selecting ? ImGuiCol_Text : ImGuiCol_TextDisabled);

		struct stColors_State {
			ImColor Label;
		};

		static std::map<ImGuiID, stColors_State> anim;
		auto it_anim = anim.find(id);

		if (it_anim == anim.end())
		{
			anim.insert({ id, stColors_State() });
			it_anim = anim.find(id);
			it_anim->second.Label = colLabel;
		}

		it_anim->second.Label.Value = ImLerp(it_anim->second.Label.Value, colLabel, 1.0f / 0.07f * GetIO().DeltaTime);

		if (hovered)
		{
			SetHoveredID(id);
		}

		const bool user_clicked = hovered && IsMouseClicked(ImGuiMouseButton_Left);
		const bool right_clicked = hovered && IsMouseClicked(ImGuiMouseButton_Right);

		static std::map<ImGuiID, bool> just_activated;
		static std::map<ImGuiID, std::map<int, bool>> initial_key_states;

		bool value_changed = false;

		char popup_id[64];
		snprintf(popup_id, sizeof(popup_id), "##keybind_context_%d", id);

		if (right_clicked && g.ActiveId != id)
		{
			SetNextWindowPos(GetMousePosOnOpeningCurrentPopup());
			OpenPopup(popup_id);
		}

		int current_mode = activation_mode != nullptr ? *activation_mode : 1;

		float max_text_width = 0.0f;
		max_text_width = ImMax(max_text_width, CalcTextSize("Hold").x);
		max_text_width = ImMax(max_text_width, CalcTextSize("Toggle").x);
		max_text_width = ImMax(max_text_width, CalcTextSize("Always").x);
		max_text_width = ImMax(max_text_width, CalcTextSize("Clear").x);

		float popup_width = max_text_width + style.FramePadding.x * 2.0f;

		PushStyleVar(ImGuiStyleVar_WindowPadding, style.FramePadding);
		SetNextWindowSize(ImVec2(popup_width, 0), ImGuiCond_Always);
		if (BeginPopup(popup_id))
		{
			if (activation_mode != nullptr)
			{
				if (ImAdd::SelectableLabel("Hold", current_mode == 1))
				{
					*activation_mode = 1;
					CloseCurrentPopup();
				}
				if (ImAdd::SelectableLabel("Toggle", current_mode == 0))
				{
					*activation_mode = 0;
					CloseCurrentPopup();
				}
				if (ImAdd::SelectableLabel("Always", current_mode == 2))
				{
					*activation_mode = 2;
					CloseCurrentPopup();
				}
				Separator();
			}
			if (ImAdd::SelectableLabel("Clear", false))
			{
				int old_key = *key;
				*key = 0;
				if (old_key != 0)
				{
					value_changed = true;
				}
				if (g.ActiveId == id)
				{
					ClearActiveID();
					just_activated.erase(id);
					initial_key_states.erase(id);
				}
				CloseCurrentPopup();
			}
			EndPopup();
		}
		PopStyleVar();

		if (user_clicked)
		{
			SetActiveID(id, window);
			FocusWindow(window);
			just_activated[id] = true;

			initial_key_states[id].clear();
			for (int i = 0; i < key_count; i++)
			{
				int vk_code = key_names[i].value;
				if (vk_code == 0) continue;
				initial_key_states[id][vk_code] = (GetAsyncKeyState(vk_code) & 0x8000) != 0;
			}
		}
		else if (IsMouseClicked(ImGuiMouseButton_Left))
		{
			if (g.ActiveId == id)
			{
				ClearActiveID();
				just_activated.erase(id);
				initial_key_states.erase(id);
			}
		}

		if (IsMouseClicked(ImGuiMouseButton_Left) && g.ActiveId == id && !hovered)
		{
			ClearActiveID();
			just_activated.erase(id);
			initial_key_states.erase(id);
		}

		if (g.ActiveId == id)
		{
			if (just_activated[id])
			{
				just_activated[id] = false;
			}
			else
			{
				for (int i = 0; i < key_count; i++)
				{
					int vk_code = key_names[i].value;
					if (vk_code == 0) continue;

					bool is_pressed = (GetAsyncKeyState(vk_code) & 0x8000) != 0;
					bool was_initially_pressed = initial_key_states[id].find(vk_code) != initial_key_states[id].end() && initial_key_states[id][vk_code];

					if (is_pressed && !was_initially_pressed)
					{
						*key = vk_code;
						value_changed = true;
						ClearActiveID();
						just_activated.erase(id);
						initial_key_states.erase(id);
						break;
					}
				}
			}
		}

		RenderNavHighlight(total_bb, id);

		ImU32 frame_col = GetColorU32(ImGuiCol_FrameBg);
		ImU32 border_col = GetColorU32(ImGuiCol_Border);

		window->DrawList->AddRectFilled(frame_bb.Min, frame_bb.Max, frame_col, style.FrameRounding);
		if (style.FrameBorderSize > 0.0f)
		{
			window->DrawList->AddRect(frame_bb.Min, frame_bb.Max, border_col, style.FrameRounding, 0, style.FrameBorderSize);
		}

		ImVec2 text_pos = frame_bb.Min + ImTrunc((size - buf_display_size) * 0.5f);
		window->DrawList->AddText(text_pos, ImGui::GetColorU32(ImGuiCol_Text), buf_display);

		return value_changed;
	}

	bool is_active(keybind_t& kb)
	{
		// "Always" needs no key; the other modes do.
		if (kb.key == 0 && kb.mode != activation_mode::always)
		{
			return false;
		}

		HWND foreground_window = GetForegroundWindow();
		HWND roblox_window = game::get_roblox_window();
		bool is_game_focused = (foreground_window == roblox_window);

		if (!is_game_focused)
		{
			if (kb.mode == activation_mode::toggle)
			{
				kb.was_pressed = false;
			}
			return false;
		}

		if (kb.mode == activation_mode::always)
		{
			return true;
		}

		bool is_pressed = (GetAsyncKeyState(kb.key) & 0x8000) != 0;
		if (kb.mode == activation_mode::hold)
		{
			return is_pressed;
		}
		else if (kb.mode == activation_mode::toggle)
		{
			if (is_pressed && !kb.was_pressed)
			{
				kb.state = !kb.state;
				kb.was_pressed = true;
			}
			else if (!is_pressed)
			{
				kb.was_pressed = false;
			}
			return kb.state;
		}

		return false;
	}

	bool feature_on(bool enabled, int key, int mode, keybind_t& state)
	{
		if (!enabled)
			return false;
		if (key == 0)
			return true;
		state.key = key;
		state.mode = static_cast<activation_mode>(mode);
		return is_active(state);
	}

	ImGuiKey vk_to_imgui_key(int vk)
	{
		if (vk >= 'A' && vk <= 'Z') return static_cast<ImGuiKey>(ImGuiKey_A + (vk - 'A'));
		if (vk >= '0' && vk <= '9') return static_cast<ImGuiKey>(ImGuiKey_0 + (vk - '0'));

		switch (vk)
		{
		case VK_LBUTTON: return ImGuiKey_MouseLeft;
		case VK_RBUTTON: return ImGuiKey_MouseRight;
		case VK_MBUTTON: return ImGuiKey_MouseMiddle;
		case VK_XBUTTON1: return ImGuiKey_MouseX1;
		case VK_XBUTTON2: return ImGuiKey_MouseX2;
		case VK_BACK: return ImGuiKey_Backspace;
		case VK_TAB: return ImGuiKey_Tab;
		case VK_RETURN: return ImGuiKey_Enter;
		case VK_LSHIFT:
		case VK_RSHIFT: return ImGuiKey_LeftShift;
		case VK_LCONTROL:
		case VK_RCONTROL: return ImGuiKey_LeftCtrl;
		case VK_LMENU:
		case VK_RMENU: return ImGuiKey_LeftAlt;
		case VK_LWIN: return ImGuiKey_LeftSuper;
		case VK_RWIN: return ImGuiKey_RightSuper;
		case VK_PAUSE: return ImGuiKey_Pause;
		case VK_CAPITAL: return ImGuiKey_CapsLock;
		case VK_ESCAPE: return ImGuiKey_Escape;
		case VK_SPACE: return ImGuiKey_Space;
		case VK_PRIOR: return ImGuiKey_PageUp;
		case VK_NEXT: return ImGuiKey_PageDown;
		case VK_END: return ImGuiKey_End;
		case VK_HOME: return ImGuiKey_Home;
		case VK_LEFT: return ImGuiKey_LeftArrow;
		case VK_UP: return ImGuiKey_UpArrow;
		case VK_RIGHT: return ImGuiKey_RightArrow;
		case VK_DOWN: return ImGuiKey_DownArrow;
		case VK_INSERT: return ImGuiKey_Insert;
		case VK_DELETE: return ImGuiKey_Delete;
		case VK_F1: return ImGuiKey_F1;
		case VK_F2: return ImGuiKey_F2;
		case VK_F3: return ImGuiKey_F3;
		case VK_F4: return ImGuiKey_F4;
		case VK_F5: return ImGuiKey_F5;
		case VK_F6: return ImGuiKey_F6;
		case VK_F7: return ImGuiKey_F7;
		case VK_F8: return ImGuiKey_F8;
		case VK_F9: return ImGuiKey_F9;
		case VK_F10: return ImGuiKey_F10;
		case VK_F11: return ImGuiKey_F11;
		case VK_F12: return ImGuiKey_F12;
		default: return ImGuiKey_None;
		}
	}

	int imgui_key_to_vk(ImGuiKey key)
	{
		if (key >= ImGuiKey_A && key <= ImGuiKey_Z) return 'A' + (key - ImGuiKey_A);
		if (key >= ImGuiKey_0 && key <= ImGuiKey_9) return '0' + (key - ImGuiKey_0);

		switch (key)
		{
		case ImGuiKey_MouseLeft: return VK_LBUTTON;
		case ImGuiKey_MouseRight: return VK_RBUTTON;
		case ImGuiKey_MouseMiddle: return VK_MBUTTON;
		case ImGuiKey_MouseX1: return VK_XBUTTON1;
		case ImGuiKey_MouseX2: return VK_XBUTTON2;
		case ImGuiKey_Backspace: return VK_BACK;
		case ImGuiKey_Tab: return VK_TAB;
		case ImGuiKey_Enter: return VK_RETURN;
		case ImGuiKey_LeftShift:
		case ImGuiKey_RightShift: return VK_LSHIFT;
		case ImGuiKey_LeftCtrl:
		case ImGuiKey_RightCtrl: return VK_LCONTROL;
		case ImGuiKey_LeftAlt:
		case ImGuiKey_RightAlt: return VK_LMENU;
		case ImGuiKey_LeftSuper: return VK_LWIN;
		case ImGuiKey_RightSuper: return VK_RWIN;
		case ImGuiKey_Pause: return VK_PAUSE;
		case ImGuiKey_CapsLock: return VK_CAPITAL;
		case ImGuiKey_Escape: return VK_ESCAPE;
		case ImGuiKey_Space: return VK_SPACE;
		case ImGuiKey_PageUp: return VK_PRIOR;
		case ImGuiKey_PageDown: return VK_NEXT;
		case ImGuiKey_End: return VK_END;
		case ImGuiKey_Home: return VK_HOME;
		case ImGuiKey_LeftArrow: return VK_LEFT;
		case ImGuiKey_UpArrow: return VK_UP;
		case ImGuiKey_RightArrow: return VK_RIGHT;
		case ImGuiKey_DownArrow: return VK_DOWN;
		case ImGuiKey_Insert: return VK_INSERT;
		case ImGuiKey_Delete: return VK_DELETE;
		case ImGuiKey_F1: return VK_F1;
		case ImGuiKey_F2: return VK_F2;
		case ImGuiKey_F3: return VK_F3;
		case ImGuiKey_F4: return VK_F4;
		case ImGuiKey_F5: return VK_F5;
		case ImGuiKey_F6: return VK_F6;
		case ImGuiKey_F7: return VK_F7;
		case ImGuiKey_F8: return VK_F8;
		case ImGuiKey_F9: return VK_F9;
		case ImGuiKey_F10: return VK_F10;
		case ImGuiKey_F11: return VK_F11;
		case ImGuiKey_F12: return VK_F12;
		default: return 0;
		}
	}
}