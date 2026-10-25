#pragma once
#include <windows.h>
#include <string>
#include <imgui.h>

namespace keybind
{
	enum class activation_mode
	{
		toggle,
		hold,
		always
	};

	struct keybind_t
	{
		int key = 0;
		activation_mode mode = activation_mode::hold;
		bool state = false;
		bool was_pressed = false;
	};

	// A feature switched on in the menu that can also have a key: with no key it's simply on;
	// with one it follows the key's mode (Toggle flips it, Hold while held, Always on). Call it
	// from one thread per feature: `state` remembers the toggle.
	bool feature_on(bool enabled, int key, int mode, keybind_t& state);

	struct key_data_t
	{
		const char* name;
		int value;
	};

	extern key_data_t key_names[];
	extern int key_count;

	const char* get_key_name(int key);
	bool keybind_selector(const char* label, int* key, int* activation_mode = nullptr, const ImVec2& size_arg = ImVec2(0, 0));
	bool is_active(keybind_t& kb);
	ImGuiKey vk_to_imgui_key(int vk);
	int imgui_key_to_vk(ImGuiKey key);
}
