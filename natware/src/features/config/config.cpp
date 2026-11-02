#include "config.h"
#include "../../settings.h"
#include "../../menu/keybind/keybind.h"
#include "../../cache/custom_entities/custom_entities.h"
#include "../../ext/json/json.hpp"
#include <windows.h>
#include <shellapi.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;

// Every setting a config carries is listed once below. Saving, loading and "reset to defaults"
// all walk this one table, so a setting can't be saved but not loaded (or the reverse), and the
// defaults are the values settings.h starts with, captured before anything changes them.
namespace
{
	enum class Kind { Bool, Int, Float, Color, Key };

	struct Entry
	{
		const char* path;     // where it lives in the file: "aimbot.triggerbot.enabled"
		const char* legacy;   // where configs written before this layout kept it, if elsewhere
		Kind kind;
		void* value;
		float initial[4];
	};

	Entry make(const char* path, Kind kind, void* value, const char* legacy = nullptr)
	{
		Entry e{ path, legacy, kind, value, { 0.0f, 0.0f, 0.0f, 0.0f } };
		switch (kind)
		{
		case Kind::Bool:  e.initial[0] = *static_cast<bool*>(value) ? 1.0f : 0.0f; break;
		case Kind::Int:
		case Kind::Key:   e.initial[0] = static_cast<float>(*static_cast<int*>(value)); break;
		case Kind::Float: e.initial[0] = *static_cast<float*>(value); break;
		case Kind::Color: for (int i = 0; i < 4; i++) e.initial[i] = static_cast<float*>(value)[i]; break;
		}
		return e;
	}

	Entry B(const char* p, bool& v, const char* legacy = nullptr)  { return make(p, Kind::Bool, &v, legacy); }
	Entry I(const char* p, int& v, const char* legacy = nullptr)   { return make(p, Kind::Int, &v, legacy); }
	Entry F(const char* p, float& v, const char* legacy = nullptr) { return make(p, Kind::Float, &v, legacy); }
	Entry C(const char* p, float* v, const char* legacy = nullptr) { return make(p, Kind::Color, v, legacy); }
	Entry K(const char* p, int& v, const char* legacy = nullptr)   { return make(p, Kind::Key, &v, legacy); }

	std::vector<Entry> build_entries()
	{
		namespace ab = settings::aimbot;
		namespace abt = settings::aimbot::triggerbot;
		namespace sa = settings::silentaim;
		namespace sat = settings::silentaim::triggerbot;
		namespace rg = settings::rage;
		namespace v = settings::visuals;
		namespace mv = settings::movement;
		namespace lt = settings::lighting;
		namespace wm = settings::watermark;
		namespace ce = settings::custom_entities;

		return {
			// menu
			K("menu.key", settings::menu::menu_keybind, "menu.menu_keybind"),
			B("menu.streamproof", settings::menu::streamproof),
			B("menu.vsync", settings::menu::vsync),
			B("menu.performance_mode", settings::menu::performance_mode),
			B("menu.uncap_client_fps", settings::cilent::fpscaps::enabled),

			// watermark
			B("watermark.enabled", settings::menu::watermark, "menu.watermark"),
			B("watermark.cheat_name", wm::show_cheat_name),
			B("watermark.display_name", wm::show_display_name),
			B("watermark.username", wm::show_username),
			B("watermark.fps", wm::show_fps),
			B("watermark.server_ip", wm::show_server_ip),
			I("watermark.separator", wm::separator_type),
			C("watermark.text_color", wm::text_color),
			B("watermark.rainbow", wm::rainbow),
			F("watermark.rainbow_speed", wm::rainbow_speed),
			F("watermark.x", wm::pos_x),
			F("watermark.y", wm::pos_y),
			B("watermark.placed", wm::pos_initialized),

			// aimbot
			B("aimbot.enabled", ab::enabled),
			K("aimbot.keybind", ab::keybind),
			I("aimbot.activation_mode", ab::activation_mode),
			I("aimbot.mode", ab::mode),
			I("aimbot.target_part", ab::target_part),
			B("aimbot.air_part_enabled", ab::air_part_enabled),
			I("aimbot.air_part", ab::air_part),
			F("aimbot.fov", ab::fov),
			B("aimbot.use_fov", ab::use_fov),
			B("aimbot.draw_fov", ab::draw_fov),
			C("aimbot.fov_circle_colour", ab::fov_circle_colour),
			C("aimbot.fov_outline_colour", ab::fov_outline_colour),
			B("aimbot.smoothing", ab::smoothing),
			F("aimbot.smoothingx", ab::smoothingx),
			F("aimbot.smoothingy", ab::smoothingy),
			I("aimbot.smoothing_style", ab::smoothing_style),
			B("aimbot.enable_prediction", ab::enable_prediction),
			F("aimbot.prediction_x", ab::prediction_x),
			F("aimbot.prediction_y", ab::prediction_y),
			B("aimbot.air_prediction_enabled", ab::air_prediction_enabled),
			F("aimbot.air_prediction_x", ab::air_prediction_x),
			F("aimbot.air_prediction_y", ab::air_prediction_y),
			B("aimbot.teamcheck", ab::teamcheck),
			B("aimbot.knock_check", ab::knock_check),
			B("aimbot.sticky_aim", ab::sticky_aim),
			B("aimbot.closest_player", ab::closest_player),
			B("aimbot.wallcheck", ab::wallcheck),
			B("aimbot.health_check_enabled", ab::health_check_enabled),
			F("aimbot.min_health", ab::min_health),
			B("aimbot.offset_enabled", ab::offset_enabled),
			F("aimbot.offset_x", ab::offset_x),
			F("aimbot.offset_y", ab::offset_y),
			F("aimbot.silent_head_offset", ab::ai_silent_y_offset),
			B("aimbot.triggerbot.enabled", abt::enabled, "aimbot.triggerbot_enabled"),
			K("aimbot.triggerbot.keybind", abt::keybind, "aimbot.triggerbot_keybind"),
			I("aimbot.triggerbot.activation_mode", abt::activation_mode, "aimbot.triggerbot_activation_mode"),
			I("aimbot.triggerbot.fire_mode", abt::fire_mode, "aimbot.triggerbot_fire_mode"),
			F("aimbot.triggerbot.clicks_per_second", abt::clicks_per_second, "aimbot.triggerbot_clicks_per_second"),
			F("aimbot.triggerbot.hold_duration", abt::hold_duration, "aimbot.triggerbot_hold_duration"),
			F("aimbot.triggerbot.reaction_ms", abt::reaction_ms, "aimbot.triggerbot_reaction_ms"),
			B("aimbot.triggerbot.max_distance_enabled", abt::max_distance_enabled, "aimbot.triggerbot_max_distance_enabled"),
			F("aimbot.triggerbot.max_distance", abt::max_distance, "aimbot.triggerbot_max_distance"),
			B("aimbot.triggerbot.wallcheck", abt::wallcheck, "aimbot.triggerbot_wallcheck"),

			// silent aim
			B("silentaim.enabled", sa::enabled),
			K("silentaim.keybind", sa::keybind),
			I("silentaim.activation_mode", sa::activation_mode),
			I("silentaim.target_part", sa::target_part),
			F("silentaim.fov", sa::fov),
			B("silentaim.use_fov", sa::use_fov),
			B("silentaim.draw_fov", sa::draw_fov),
			B("silentaim.attach_fov_to_target", sa::attach_fov_to_target),
			C("silentaim.fov_circle_colour", sa::fov_circle_colour),
			C("silentaim.fov_outline_colour", sa::fov_outline_colour),
			B("silentaim.enable_prediction", sa::enable_prediction),
			F("silentaim.prediction_x", sa::prediction_x),
			F("silentaim.prediction_y", sa::prediction_y),
			B("silentaim.sticky_aim", sa::sticky_aim),
			B("silentaim.auto_switch", sa::auto_switch),
			B("silentaim.spoof_mouse", sa::spoof_mouse),
			B("silentaim.use_aimbot_target", sa::use_aimbot_target),
			B("silentaim.teamcheck", sa::teamcheck),
			B("silentaim.guncheck", sa::guncheck),
			B("silentaim.wallcheck", sa::wallcheck),
			B("silentaim.knock_check", sa::knock_check),
			I("silentaim.priorities", sa::priorities),
			B("silentaim.health_check_enabled", sa::health_check_enabled),
			F("silentaim.min_health", sa::min_health),
			B("silentaim.hitchance_enabled", sa::hitchance_enabled),
			F("silentaim.hitchance", sa::hitchance),
			B("silentaim.draw_target_dot", sa::draw_target_dot),
			C("silentaim.target_dot_color", sa::target_dot_color),
			F("silentaim.target_dot_size", sa::target_dot_size),
			B("silentaim.draw_snap_line", sa::draw_snap_line),
			C("silentaim.snap_line_color", sa::snap_line_color),
			B("silentaim.triggerbot.enabled", sat::enabled, "silentaim.triggerbot_enabled"),
			K("silentaim.triggerbot.keybind", sat::keybind, "silentaim.triggerbot_keybind"),
			I("silentaim.triggerbot.activation_mode", sat::activation_mode, "silentaim.triggerbot_activation_mode"),
			I("silentaim.triggerbot.fire_mode", sat::fire_mode, "silentaim.triggerbot_fire_mode"),
			F("silentaim.triggerbot.clicks_per_second", sat::clicks_per_second, "silentaim.triggerbot_clicks_per_second"),
			F("silentaim.triggerbot.hold_duration", sat::hold_duration, "silentaim.triggerbot_hold_duration"),
			F("silentaim.triggerbot.reaction_ms", sat::reaction_ms, "silentaim.triggerbot_reaction_ms"),
			B("silentaim.triggerbot.max_distance_enabled", sat::max_distance_enabled, "silentaim.triggerbot_max_distance_enabled"),
			F("silentaim.triggerbot.max_distance", sat::max_distance, "silentaim.triggerbot_max_distance"),
			B("silentaim.triggerbot.wallcheck", sat::wallcheck, "silentaim.triggerbot_wallcheck"),

			// combat
			B("rage.hitsounds", rg::hitsounds),
			I("rage.hitsound_type", rg::hitsound_type),
			I("rage.hitsound_method", rg::hitsound_method),
			B("rage.rapidfire", rg::rapidfire),
			I("rage.rapidfire_mode", rg::rapidfire_mode),
			F("rage.rapidfire_cps", rg::rapidfire_cps),
			K("rage.rapidfire_key", rg::rapidfire_key),
			I("rage.rapidfire_key_mode", rg::rapidfire_key_mode),
			B("rage.noclip", rg::noclip),
			K("rage.noclip_key", rg::noclip_key),
			I("rage.noclip_key_mode", rg::noclip_key_mode),
			B("rage.hit_tracers", rg::hit_tracers),
			C("rage.hit_tracers_color", rg::hit_tracers_color),
			F("rage.hit_tracers_duration", rg::hit_tracers_duration),
			B("rage.hipheight.enabled", rg::hipheight::enabled, "rage.hipheight_enabled"),
			K("rage.hipheight.key", rg::hipheight::key),
			I("rage.hipheight.key_mode", rg::hipheight::key_mode),
			F("rage.hipheight.height", rg::hipheight::height, "rage.hipheight_height"),
			B("rage.hitbox_expander.enabled", rg::hitbox_expander::enabled),
			K("rage.hitbox_expander.key", rg::hitbox_expander::key),
			I("rage.hitbox_expander.key_mode", rg::hitbox_expander::key_mode),
			I("rage.hitbox_expander.target_part", rg::hitbox_expander::target_part),
			F("rage.hitbox_expander.size_x", rg::hitbox_expander::size_x),
			F("rage.hitbox_expander.size_y", rg::hitbox_expander::size_y),
			F("rage.hitbox_expander.size_z", rg::hitbox_expander::size_z),
			B("rage.hitbox_expander.knock_check", rg::hitbox_expander::knock_check),
			B("rage.spin360.enabled", rg::spin360::enabled, "rage.spin360_enabled"),
			F("rage.spin360.speed", rg::spin360::speed, "rage.spin360_speed"),
			K("rage.spin360.keybind", rg::spin360::keybind, "rage.spin360_keybind"),
			I("rage.spin360.activation_mode", rg::spin360::activation_mode, "rage.spin360_activation_mode"),

			// visuals
			B("visuals.enable_enemies", v::enable_enemies),
			K("visuals.esp_key", v::esp_key),
			I("visuals.esp_key_mode", v::esp_key_mode),
			I("visuals.esp_style", v::esp_style),
			B("visuals.enable_client", v::enable_client),
			B("visuals.teamcheck", v::teamcheck),
			B("visuals.knock_check", v::knock_check),
			B("visuals.use_team_color", v::use_team_color),
			B("visuals.ignore_whitelisted", v::ignore_whitelisted),
			B("visuals.max_distance_enabled", v::max_distance_enabled),
			F("visuals.max_distance", v::max_distance),
			B("visuals.mm2_esp", v::mm2_esp),
			B("visuals.radar_enabled", v::radar_enabled),
			F("visuals.radar_size", v::radar_size),
			B("visuals.box", v::box),
			I("visuals.box_type", v::box_type),
			F("visuals.box_glow_strength", v::box_glow_strength),
			C("visuals.box_color", v::box_color),
			B("visuals.box_fill", v::box_fill),
			C("visuals.box_fill_color", v::box_fill_color),
			B("visuals.name", v::name),
			I("visuals.name_type", v::name_type),
			I("visuals.name_display_type", v::name_display_type),
			C("visuals.name_color", v::name_color),
			B("visuals.blend", v::blend),
			C("visuals.name_color_blend_start", v::name_color_blend_start, "visuals.colorblend_start"),
			C("visuals.name_color_blend_end", v::name_color_blend_end, "visuals.colorblend_end"),
			B("visuals.avatar", v::avatar),
			B("visuals.healthbar", v::healthbar),
			C("visuals.healthbar_color", v::healthbar_color),
			B("visuals.health_based_healthbar", v::health_based_healthbar),
			B("visuals.gradient_healthbar", v::gradient_healthbar),
			C("visuals.gradient_healthbar_color_start", v::gradient_healthbar_color_start),
			C("visuals.gradient_healthbar_color_end", v::gradient_healthbar_color_end),
			B("visuals.health_percent", v::health_percent),
			C("visuals.health_percent_color", v::health_percent_color),
			B("visuals.armorbar", v::armorbar),
			C("visuals.armorbar_color", v::armorbar_color),
			B("visuals.distance", v::distance),
			I("visuals.distance_measurement", v::distance_measurement),
			C("visuals.distance_color", v::distance_color),
			B("visuals.team_name", v::team_name),
			B("visuals.tool", v::tool),
			C("visuals.tool_color", v::tool_color),
			I("visuals.esp_font", v::esp_font),
			B("visuals.local_player", v::local_player),
			B("visuals.chams", v::chams),
			K("visuals.chams_key", v::chams_key),
			I("visuals.chams_key_mode", v::chams_key_mode),
			I("visuals.chams_style", v::chams_style),
			I("visuals.chams_effect", v::chams_effect),
			F("visuals.chams_texture_speed", v::chams_texture_speed),
			F("visuals.chams_texture_scale", v::chams_texture_scale),
			C("visuals.chams_fill_color", v::chams_fill_color),
			C("visuals.chams_outline_color", v::chams_outline_color),
			B("visuals.chams_fill_enabled", v::chams_fill_enabled),
			B("visuals.chams_outline_enabled", v::chams_outline_enabled),
			B("visuals.skeleton", v::skeleton),
			C("visuals.skeleton_color", v::skeleton_color),
			F("visuals.skeleton_thickness", v::skeleton_thickness),
			B("visuals.target_warning_icon", v::target_warning_icon),
			F("visuals.target_warning_icon_size", v::target_warning_icon_size),
			B("visuals.flags", v::flags),
			C("visuals.flags_state_colour", v::flags_state_colour),
			B("visuals.client_box", v::client_box),
			C("visuals.client_box_color", v::client_box_color),
			B("visuals.client_box_fill", v::client_box_fill),
			C("visuals.client_box_fill_color", v::client_box_fill_color),
			B("visuals.client_name", v::client_name),
			C("visuals.client_name_color", v::client_name_color),
			B("visuals.client_avatar", v::client_avatar),
			B("visuals.client_healthbar", v::client_healthbar),
			C("visuals.client_healthbar_color", v::client_healthbar_color),
			B("visuals.client_health_percent", v::client_health_percent),
			C("visuals.client_health_percent_color", v::client_health_percent_color),
			B("visuals.client_armorbar", v::client_armorbar),
			C("visuals.client_armorbar_color", v::client_armorbar_color),
			B("visuals.client_distance", v::client_distance),
			C("visuals.client_distance_color", v::client_distance_color),
			B("visuals.client_tool", v::client_tool),
			C("visuals.client_tool_color", v::client_tool_color),
			B("visuals.client_chams", v::client_chams),
			C("visuals.client_chams_fill_color", v::client_chams_fill_color),
			C("visuals.client_chams_outline_color", v::client_chams_outline_color),
			B("visuals.client_skeleton", v::client_skeleton),
			C("visuals.client_skeleton_color", v::client_skeleton_color),
			B("visuals.client_flags", v::client_flags),
			C("visuals.client_flags_state_colour", v::client_flags_state_colour),
			B("visuals.client_headless", v::client_headless),
			B("visuals.client_korblox", v::client_korblox),
			B("visuals.debug_wallcheck", v::debug_wallcheck),
			B("visuals.view_hitbox", v::view_hitbox),
			C("visuals.view_hitbox_color", v::view_hitbox_color),
			F("visuals.fade_in_speed", v::fade_in_speed),
			F("visuals.fade_out_speed", v::fade_out_speed),
			B("visuals.hit_tracers_enabled", v::hit_tracers_enabled),
			I("visuals.hit_tracers_method", v::hit_tracers_method),
			C("visuals.hit_tracers_color", v::hit_tracers_color),
			F("visuals.hit_tracers_duration", v::hit_tracers_duration),

			// movement
			B("movement.speedhack.enabled", mv::speedhack::enabled),
			I("movement.speedhack.mode", mv::speedhack::mode),
			F("movement.speedhack.speed", mv::speedhack::speed),
			K("movement.speedhack.keybind", mv::speedhack::keybind),
			I("movement.speedhack.activation_mode", mv::speedhack::activation_mode),
			B("movement.jumphack.enabled", mv::jumphack::enabled),
			F("movement.jumphack.value", mv::jumphack::value),
			K("movement.jumphack.keybind", mv::jumphack::keybind),
			I("movement.jumphack.activation_mode", mv::jumphack::activation_mode),
			B("movement.nojumpcooldown.enabled", mv::nojumpcooldown::enabled, "rage.nojumpcooldown"),
			B("movement.flyhack.enabled", mv::flyhack::enabled),
			I("movement.flyhack.mode", mv::flyhack::mode),
			F("movement.flyhack.speed", mv::flyhack::speed),
			K("movement.flyhack.keybind", mv::flyhack::keybind),
			I("movement.flyhack.activation_mode", mv::flyhack::activation_mode),
			B("movement.tickrate.enabled", mv::tickrate::enabled),
			F("movement.tickrate.value", mv::tickrate::value),
			B("movement.orbit.enabled", mv::orbit::enabled),
			K("movement.orbit.key", mv::orbit::key),
			I("movement.orbit.key_mode", mv::orbit::key_mode),
			I("movement.orbit.orbit_type", mv::orbit::orbit_type),
			F("movement.orbit.speed", mv::orbit::speed),
			F("movement.orbit.radius", mv::orbit::radius),
			F("movement.orbit.height_offset", mv::orbit::height_offset),
			B("movement.orbit.spectate_target", mv::orbit::spectate_target),
			B("movement.orbit.randomize", mv::orbit::randomize),
			F("movement.orbit.randomize_x", mv::orbit::randomize_x),
			F("movement.orbit.randomize_y", mv::orbit::randomize_y),
			B("movement.gravity.enabled", mv::gravity::enabled),
			F("movement.gravity.value", mv::gravity::value),

			// world
			B("lighting.fog.enabled", lt::fog::enabled),
			F("lighting.fog.fog_start", lt::fog::fog_start),
			F("lighting.fog.fog_end", lt::fog::fog_end),
			F("lighting.fog.fog_r", lt::fog::fog_r),
			F("lighting.fog.fog_g", lt::fog::fog_g),
			F("lighting.fog.fog_b", lt::fog::fog_b),
			B("lighting.exposure.enabled", lt::exposure::enabled),
			F("lighting.exposure.value", lt::exposure::exposure),
			B("lighting.shadows.disable", lt::shadows::disable),
			B("lighting.clocktime.enabled", lt::clocktime::enabled),
			F("lighting.clocktime.clock_time", lt::clocktime::clock_time),
			B("lighting.skybox.enabled", lt::skybox::enabled),
			I("lighting.skybox.preset_index", lt::skybox::preset_index),
			B("custom_entities.enabled", ce::show_custom_entities),
			B("custom_entities.auto_refresh", ce::auto_refresh),
			F("custom_entities.refresh_rate", ce::refresh_rate),

			// exploits
			B("exploits.antiafk.enabled", settings::exploits::antiafk::enabled),
			B("exploits.freezeplayer.enabled", settings::exploits::freezeplayer::enabled),
			K("exploits.freezeplayer.keybind", settings::exploits::freezeplayer::keybind),
			I("exploits.freezeplayer.activation_mode", settings::exploits::freezeplayer::activation_mode),
		};
	}

	// Built during static initialisation, before main(): every setting it points at is
	// constant-initialised, so the captured values are the program's defaults.
	std::vector<Entry>& entries()
	{
		static std::vector<Entry> table = build_entries();
		return table;
	}
	const bool g_entries_built = (entries(), true);
	constexpr const char* kDefaultContainerPath = "Workspace.Bots";

	// ---- paths ---------------------------------------------------------------------------

	constexpr const char* kConfigDir = "configs";
	constexpr const char* kDataDir = "data";

	std::string lower(std::string s)
	{
		for (char& c : s)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return s;
	}

	// "My Configs\\legit.json" -> "My Configs/legit": no drive letters, no "..", no characters
	// Windows won't take in a file name.
	std::string clean_name(std::string name)
	{
		std::replace(name.begin(), name.end(), '\\', '/');
		if (lower(name).size() >= 5 && lower(name).substr(name.size() - 5) == ".json")
			name.resize(name.size() - 5);
		std::string out, segment;
		auto flush = [&]()
		{
			while (!segment.empty() && (segment.back() == ' ' || segment.back() == '.'))
				segment.pop_back();
			size_t first = segment.find_first_not_of(' ');
			segment = first == std::string::npos ? "" : segment.substr(first);
			if (!segment.empty() && segment != "." && segment != "..")
				out += (out.empty() ? "" : "/") + segment;
			segment.clear();
		};
		for (char c : name)
		{
			if (c == '/') { flush(); continue; }
			if (static_cast<unsigned char>(c) < 32 || std::string("<>:\"|?*").find(c) != std::string::npos)
				continue;
			segment += c;
		}
		flush();
		return out;
	}

	std::wstring widen(const std::string& s)
	{
		if (s.empty())
			return {};
		const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
		std::wstring w(static_cast<size_t>(n), L'\0');
		MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
		return w;
	}

	std::string narrow(const std::wstring& w)
	{
		if (w.empty())
			return {};
		const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
		std::string s(static_cast<size_t>(n), '\0');
		WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
		return s;
	}

	// ---- JSON helpers -------------------------------------------------------------------

	// Configs written before this layout ended every object with a trailing comma, which
	// strict JSON rejects. Drop commas that come right before a closing bracket.
	std::string strip_trailing_commas(const std::string& text)
	{
		std::string out;
		out.reserve(text.size());
		bool in_string = false, escaped = false;
		for (size_t i = 0; i < text.size(); i++)
		{
			const char c = text[i];
			if (in_string)
			{
				out += c;
				if (escaped) escaped = false;
				else if (c == '\\') escaped = true;
				else if (c == '"') in_string = false;
				continue;
			}
			if (c == '"')
				in_string = true;
			else if (c == ',')
			{
				size_t j = i + 1;
				while (j < text.size() && std::isspace(static_cast<unsigned char>(text[j])))
					j++;
				if (j >= text.size() || text[j] == '}' || text[j] == ']')
					continue;
			}
			out += c;
		}
		return out;
	}

	// They also never wrote the root object's closing brace. Close whatever is left open.
	std::string close_open_brackets(const std::string& text)
	{
		std::string open;
		bool in_string = false, escaped = false;
		for (const char c : text)
		{
			if (in_string)
			{
				if (escaped) escaped = false;
				else if (c == '\\') escaped = true;
				else if (c == '"') in_string = false;
				continue;
			}
			if (c == '"') in_string = true;
			else if (c == '{' || c == '[') open += c;
			else if ((c == '}' || c == ']') && !open.empty()) open.pop_back();
		}
		std::string out = text;
		for (auto it = open.rbegin(); it != open.rend(); ++it)
			out += *it == '{' ? "\n}" : "\n]";
		return out;
	}

	const json* find(const json& root, const char* path)
	{
		const json* node = &root;
		std::string key;
		for (const char* p = path;; p++)
		{
			if (*p == '.' || *p == '\0')
			{
				if (!node->is_object())
					return nullptr;
				auto it = node->find(key);
				if (it == node->end())
					return nullptr;
				node = &*it;
				key.clear();
				if (*p == '\0')
					return node;
			}
			else
				key += *p;
		}
	}

	json& place(json& root, const char* path)
	{
		json* node = &root;
		std::string key;
		for (const char* p = path;; p++)
		{
			if (*p == '.' || *p == '\0')
			{
				node = &(*node)[key];
				key.clear();
				if (*p == '\0')
					return *node;
			}
			else
				key += *p;
		}
	}

	int key_from_name(const std::string& name)
	{
		const std::string wanted = lower(name);
		if (wanted.empty() || wanted == "none")
			return 0;
		for (int i = 0; i < keybind::key_count; i++)
			if (keybind::key_names[i].name && lower(keybind::key_names[i].name) == wanted)
				return keybind::key_names[i].value;
		return -1;
	}

	json key_to_json(int key)
	{
		if (key == 0)
			return "none";
		const std::string name = keybind::get_key_name(key);
		return key_from_name(name) == key ? json(lower(name)) : json(key);
	}

	bool apply(const json& value, const Entry& e)
	{
		switch (e.kind)
		{
		case Kind::Bool:
			if (value.is_boolean()) *static_cast<bool*>(e.value) = value.get<bool>();
			else if (value.is_number()) *static_cast<bool*>(e.value) = value.get<double>() != 0.0;
			else return false;
			return true;
		case Kind::Int:
			if (value.is_number()) *static_cast<int*>(e.value) = static_cast<int>(std::lround(value.get<double>()));
			else if (value.is_boolean()) *static_cast<int*>(e.value) = value.get<bool>() ? 1 : 0;
			else return false;
			return true;
		case Kind::Float:
			if (!value.is_number()) return false;
			{
				const double d = value.get<double>();
				if (!std::isfinite(d)) return false;
				*static_cast<float*>(e.value) = static_cast<float>(d);
			}
			return true;
		case Kind::Color:
			if (!value.is_array() || value.size() < 3) return false;
			for (size_t i = 0; i < 4; i++)
			{
				const float fallback = i == 3 ? 1.0f : 0.0f;
				static_cast<float*>(e.value)[i] = i < value.size() && value[i].is_number() ? value[i].get<float>() : fallback;
			}
			return true;
		case Kind::Key:
			if (value.is_string())
			{
				const int key = key_from_name(value.get<std::string>());
				if (key < 0) return false;
				*static_cast<int*>(e.value) = key;
			}
			else if (value.is_number()) *static_cast<int*>(e.value) = value.get<int>();
			else return false;
			return true;
		}
		return false;
	}

	json to_json(const Entry& e)
	{
		switch (e.kind)
		{
		case Kind::Bool:  return *static_cast<bool*>(e.value);
		case Kind::Int:   return *static_cast<int*>(e.value);
		case Kind::Float:
		{
			// Round to what the sliders show, so 0.30000001 is written as 0.3.
			const float f = *static_cast<float*>(e.value);
			return std::round(static_cast<double>(f) * 10000.0) / 10000.0;
		}
		case Kind::Color:
		{
			json a = json::array();
			for (int i = 0; i < 4; i++)
				a.push_back(std::round(static_cast<double>(static_cast<float*>(e.value)[i]) * 1000.0) / 1000.0);
			return a;
		}
		case Kind::Key:   return key_to_json(*static_cast<int*>(e.value));
		}
		return nullptr;
	}

	bool read_json(const std::string& path, json& out)
	{
		std::ifstream file(widen(path), std::ios::binary);
		if (!file.is_open())
			return false;
		std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF)   // UTF-8 BOM
			text.erase(0, 3);
		out = json::parse(text, nullptr, false, true);
		if (out.is_discarded())
		{
			// Only configs from the old writer get repaired. A current one that doesn't parse is
			// incomplete (still being copied in) and must not be applied half-read.
			out = json::parse(close_open_brackets(strip_trailing_commas(text)), nullptr, false, true);
			if (!out.is_discarded() && out.is_object() && out.contains("natware_config"))
				return false;
		}
		return !out.is_discarded() && out.is_object();
	}

	// Write beside the target, then swap it in, so a crash mid-save never leaves half a file.
	bool write_file(const std::string& path, const std::string& text)
	{
		const std::wstring target = widen(path), temp = target + L".tmp";
		{
			std::ofstream file(temp, std::ios::binary | std::ios::trunc);
			if (!file.is_open())
				return false;
			file << text;
			file.flush();
			if (!file.good())
				return false;
		}
		if (!MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		{
			DeleteFileW(temp.c_str());
			return false;
		}
		return true;
	}

	// ---- the live folder -------------------------------------------------------------------

	struct Stamp
	{
		fs::file_time_type time{};
		uintmax_t size = 0;
		bool operator==(const Stamp& o) const { return time == o.time && size == o.size; }
	};

	Stamp stamp_of(const std::string& path)
	{
		std::error_code ec;
		Stamp s;
		s.time = fs::last_write_time(widen(path), ec);
		s.size = fs::file_size(widen(path), ec);
		return s;
	}

	struct LiveState
	{
		std::vector<config::config_info_t> list;
		bool listed = false;
		HANDLE watch = INVALID_HANDLE_VALUE;
		bool pending = false;
		ULONGLONG changed_at = 0;
		ULONGLONG polled_at = 0;
		std::string loaded;
		Stamp loaded_stamp;
		std::string message;
		ULONGLONG message_at = 0;
	};

	LiveState& live()
	{
		static LiveState state;
		return state;
	}

	void note(const std::string& message)
	{
		live().message = message;
		live().message_at = GetTickCount64();
	}

	void remember_loaded(const std::string& name)
	{
		live().loaded = name;
		live().loaded_stamp = stamp_of(config::get_config_path(name));
	}

	bool same_list(const std::vector<config::config_info_t>& a, const std::vector<config::config_info_t>& b)
	{
		if (a.size() != b.size())
			return false;
		for (size_t i = 0; i < a.size(); i++)
			if (a[i].name != b[i].name || a[i].modified != b[i].modified)
				return false;
		return true;
	}

	bool move_into(const fs::path& from, fs::path to)
	{
		std::error_code ec;
		if (fs::exists(to, ec))
		{
			const fs::path stem = to.stem(), ext = to.extension(), dir = to.parent_path();
			for (int n = 2; fs::exists(to, ec); n++)
				to = dir / (stem.wstring() + L" (" + std::to_wstring(n) + L")" + ext.wstring());
		}
		fs::rename(from, to, ec);
		return !ec;
	}

	void remove_if_empty(const fs::path& dir)
	{
		std::error_code ec;
		if (fs::is_directory(dir, ec) && fs::is_empty(dir, ec))
			fs::remove(dir, ec);
	}

	std::wstring exe_path()
	{
		wchar_t buf[MAX_PATH]{};
		GetModuleFileNameW(nullptr, buf, MAX_PATH);
		return buf;
	}
}

namespace
{
	// Every setting as one JSON document: the config table, the player whitelist and the
	// custom entity containers. Configs and last.json both use it.
	json current_document()
	{
		json root;
		root["natware_config"] = 2;
		for (const Entry& e : entries())
			place(root, e.path) = to_json(e);

		{
			std::vector<std::string> names(settings::rage::playerlist::whitelist.begin(), settings::rage::playerlist::whitelist.end());
			std::sort(names.begin(), names.end());
			root["players"]["whitelist"] = names;
		}
		{
			json containers = json::array();
			std::lock_guard<std::mutex> lock(custom_entities::containers_mtx);
			for (const auto& c : settings::custom_entities::containers)
				containers.push_back({ { "path", c.path }, { "enabled", c.enabled } });
			root["custom_entities"]["path"] = settings::custom_entities::current_input;
			root["custom_entities"]["containers"] = containers;
		}
		return root;
	}

	// Puts a document's values in place: everything not in it goes back to its default.
	void apply_document(const json& root)
	{
		config::reset_to_defaults();
		for (const Entry& e : entries())
		{
			const json* value = find(root, e.path);
			if (!value && e.legacy)
				value = find(root, e.legacy);
			if (value)
				apply(*value, e);
		}

		// Chams used to be a type (0 cube, 1 highlight, 2 mesh, 3 engine) plus a texture laid over
		// it (0 none, 1 water .. 4 galaxy). Now the textures are Engine's effects and the Roblox
		// highlight is its own style.
		if (!find(root, "visuals.chams_style"))
		{
			const json* type = find(root, "visuals.chams_type");
			const json* texture = find(root, "visuals.chams_texture");
			const int t = type && type->is_number_integer() ? type->get<int>() : 0;
			const int tex = texture && texture->is_number_integer() ? texture->get<int>() : 0;
			settings::visuals::chams_style = t == 3 ? 2 : (tex > 0 ? 1 : 0);
			settings::visuals::chams_effect = tex > 0 ? std::clamp(tex - 1, 0, 3) : 0;
		}

		// Very old configs named the blend toggle "blend_name".
		if (!find(root, "visuals.blend"))
			if (const json* old = find(root, "visuals.blend_name"); old && old->is_boolean() && old->get<bool>())
				settings::visuals::blend = true;

		if (const json* list = find(root, "players.whitelist"); list && list->is_array())
			for (const json& n : *list)
				if (n.is_string())
					settings::rage::playerlist::whitelist.insert(n.get<std::string>());

		if (const json* p = find(root, "custom_entities.path"); p && p->is_string())
			settings::custom_entities::current_input = p->get<std::string>();
		if (const json* list = find(root, "custom_entities.containers"); list && list->is_array())
		{
			std::lock_guard<std::mutex> lock(custom_entities::containers_mtx);
			settings::custom_entities::containers.clear();
			for (const json& c : *list)
				if (c.is_object() && c.contains("path") && c["path"].is_string())
				{
					const std::string p = c["path"].get<std::string>();
					const bool enabled = !c.contains("enabled") || !c["enabled"].is_boolean() || c["enabled"].get<bool>();
					settings::custom_entities::containers.push_back({ p, p, enabled, {} });
				}
		}
	}
}

namespace config
{
	std::string get_config_directory()
	{
		return kConfigDir;
	}

	std::string data_path(const char* file)
	{
		return std::string(kDataDir) + "\\" + file;
	}

	std::string get_config_path(const std::string& name)
	{
		const std::string clean = clean_name(name);
		if (clean.empty())
			return "";
		std::string relative = clean;
		std::replace(relative.begin(), relative.end(), '/', '\\');
		return std::string(kConfigDir) + "\\" + relative + ".json";
	}

	bool ensure_config_directory()
	{
		std::error_code ec;
		fs::create_directories(kConfigDir, ec);
		return fs::is_directory(kConfigDir, ec);
	}

	void organize_folders()
	{
		std::error_code ec;
		fs::create_directories(kConfigDir, ec);
		fs::create_directories(kDataDir, ec);

		// configs\game\*.json -> configs\*.json
		const fs::path old_games = fs::path(kConfigDir) / "game";
		if (fs::is_directory(old_games, ec))
		{
			for (const auto& entry : fs::directory_iterator(old_games, ec))
				if (entry.is_regular_file(ec) && lower(entry.path().extension().string()) == ".json")
					move_into(entry.path(), fs::path(kConfigDir) / entry.path().filename());
			remove_if_empty(old_games);
		}

		// configs\external\config.json -> data\settings.json
		const fs::path old_external = fs::path(kConfigDir) / "external";
		const fs::path settings_file = fs::path(kDataDir) / "settings.json";
		if (fs::exists(old_external / "config.json", ec) && !fs::exists(settings_file, ec))
			fs::rename(old_external / "config.json", settings_file, ec);
		remove_if_empty(old_external);

		// offsets.json beside the exe -> data\offsets.json (an empty one was only a placeholder)
		if (fs::exists("offsets.json", ec))
		{
			if (fs::file_size("offsets.json", ec) == 0)
				fs::remove("offsets.json", ec);
			else if (!fs::exists(fs::path(kDataDir) / "offsets.json", ec))
				fs::rename("offsets.json", fs::path(kDataDir) / "offsets.json", ec);
		}

		// The editor's page is built into the exe now; drop the copy that used to sit beside it
		// (only these files, and the folder only if nothing else is in it).
		if (fs::exists("assets/editor.html", ec))
		{
			for (const char* f : { "editor.html", "workspace.css", "workspace.js", "glass.js", "dark-veil.js", "brand.ttf" })
				fs::remove(fs::path("assets") / f, ec);
			remove_if_empty("assets");
		}

		// The editor's storage (scripts, tabs, favourites): WebView2 kept it beside the exe, one
		// folder per exe. Keep the most recently used one as data\webview.
		const fs::path webview = fs::path(kDataDir) / "webview";
		if (!fs::exists(webview, ec))
		{
			const fs::path exe = exe_path();
			const fs::path candidates[] = { exe.parent_path() / (exe.filename().wstring() + L".WebView2"),
				exe.parent_path() / L"CLAUDEAI.exe.WebView2", exe.parent_path() / L"NEXEC-GUI.exe.WebView2" };
			fs::path newest;
			fs::file_time_type newest_time{};
			for (const fs::path& c : candidates)
			{
				if (!fs::is_directory(c, ec))
					continue;
				// Last use = the newest file in its page storage (a folder's own time only moves
				// when files are added or removed).
				fs::file_time_type t{};
				const fs::path storage = c / "EBWebView" / "Default" / "Local Storage" / "leveldb";
				for (const auto& f : fs::directory_iterator(storage, ec))
					if (f.is_regular_file(ec))
						t = (std::max)(t, f.last_write_time(ec));
				if (newest.empty() || t > newest_time)
				{
					newest = c;
					newest_time = t;
				}
			}
			if (!newest.empty())
				fs::rename(newest, webview, ec);   // fails while that profile is open; the editor then keeps using it where it is
		}
	}

	std::vector<config_info_t> get_config_list()
	{
		std::vector<config_info_t> configs;
		std::error_code ec;
		if (!fs::is_directory(kConfigDir, ec))
			return configs;

		const fs::path root(kConfigDir);
		fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
		for (; !ec && it != end; it.increment(ec))
		{
			const fs::directory_entry& entry = *it;
			if (entry.is_directory(ec))
			{
				// One level of folders is plenty for sorting configs by game.
				if (it.depth() >= 1 || entry.path().filename().wstring().rfind(L".", 0) == 0)
					it.disable_recursion_pending();
				continue;
			}
			if (!entry.is_regular_file(ec) || lower(entry.path().extension().string()) != ".json")
				continue;
			fs::path relative = fs::relative(entry.path(), root, ec);
			relative.replace_extension();
			std::string name = narrow(relative.wstring());
			std::replace(name.begin(), name.end(), '\\', '/');
			configs.push_back({ name, narrow(entry.path().wstring()), entry.last_write_time(ec) });
		}
		std::sort(configs.begin(), configs.end(), [](const config_info_t& a, const config_info_t& b)
		{
			return lower(a.name) < lower(b.name);
		});
		return configs;
	}

	bool save_config(const std::string& name)
	{
		const std::string path = get_config_path(name);
		if (path.empty() || !ensure_config_directory())
			return false;
		std::error_code ec;
		fs::create_directories(fs::path(widen(path)).parent_path(), ec);

		if (!write_file(path, current_document().dump(2) + "\n"))
			return false;
		remember_loaded(clean_name(name));
		return true;
	}

	void reset_to_defaults()
	{
		for (const Entry& e : entries())
		{
			switch (e.kind)
			{
			case Kind::Bool:  *static_cast<bool*>(e.value) = e.initial[0] != 0.0f; break;
			case Kind::Int:
			case Kind::Key:   *static_cast<int*>(e.value) = static_cast<int>(e.initial[0]); break;
			case Kind::Float: *static_cast<float*>(e.value) = e.initial[0]; break;
			case Kind::Color: for (int i = 0; i < 4; i++) static_cast<float*>(e.value)[i] = e.initial[i]; break;
			}
		}
		settings::rage::playerlist::whitelist.clear();
		settings::custom_entities::current_input = kDefaultContainerPath;
	}

	bool load_config(const std::string& name)
	{
		const std::string path = get_config_path(name);
		json root;
		// Parse first: a file that can't be read leaves the current settings alone.
		if (path.empty() || !read_json(path, root))
			return false;

		apply_document(root);
		remember_loaded(clean_name(name));
		return true;
	}

	// ---- last settings ---------------------------------------------------------------------
	// Whatever you have set is kept in data\last.json: written a moment after anything
	// changes and when natware closes, and put back the next time External starts.

	namespace
	{
		std::string g_last_written;     // the text last.json holds, to skip identical writes
		ULONGLONG g_last_check = 0;

		std::string last_text()
		{
			json root = current_document();
			root["natware_last"]["loaded_config"] = live().loaded;
			return root.dump(2) + "\n";
		}
	}

	bool restore_last_settings()
	{
		json root;
		if (!read_json(data_path("last.json"), root))
			return false;
		apply_document(root);
		if (const json* name = find(root, "natware_last.loaded_config"); name && name->is_string())
		{
			const std::string loaded = name->get<std::string>();
			if (!loaded.empty() && config_exists(loaded))
				remember_loaded(loaded);
		}
		g_last_written = last_text();
		note("Restored your last settings");
		return true;
	}

	void save_last_settings()
	{
		const std::string text = last_text();
		if (text == g_last_written)
			return;
		std::error_code ec;
		fs::create_directories(kDataDir, ec);
		if (write_file(data_path("last.json"), text))
			g_last_written = text;
	}

	void autosave()
	{
		const ULONGLONG now = GetTickCount64();
		if (now - g_last_check < 1000)
			return;
		g_last_check = now;
		save_last_settings();
	}

	bool delete_config(const std::string& name)
	{
		const std::string path = get_config_path(name);
		std::error_code ec;
		if (path.empty() || !fs::exists(widen(path), ec))
			return false;
		const bool removed = fs::remove(widen(path), ec);
		if (removed && live().loaded == clean_name(name))
			live().loaded.clear();
		if (removed)
			remove_if_empty(fs::path(widen(path)).parent_path() == fs::path(kConfigDir) ? fs::path() : fs::path(widen(path)).parent_path());
		return removed;
	}

	bool config_exists(const std::string& name)
	{
		const std::string path = get_config_path(name);
		std::error_code ec;
		return !path.empty() && fs::exists(widen(path), ec);
	}

	void open_file_location()
	{
		ensure_config_directory();
		const std::wstring dir = fs::absolute(kConfigDir).wstring();
		ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWDEFAULT);
	}

	bool watch(bool force)
	{
		LiveState& s = live();
		const ULONGLONG now = GetTickCount64();
		bool rescan = !s.listed || force;

		if (s.watch == INVALID_HANDLE_VALUE && ensure_config_directory())
			s.watch = FindFirstChangeNotificationW(widen(kConfigDir).c_str(), TRUE,
				FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE);
		if (s.watch != INVALID_HANDLE_VALUE)
		{
			if (WaitForSingleObject(s.watch, 0) == WAIT_OBJECT_0)
			{
				s.pending = true;
				s.changed_at = now;
				if (!FindNextChangeNotification(s.watch))
				{
					FindCloseChangeNotification(s.watch);
					s.watch = INVALID_HANDLE_VALUE;
				}
			}
		}
		else if (now - s.polled_at > 1000)
		{
			// No change notifications (the folder is missing or on an odd drive): poll.
			s.polled_at = now;
			s.pending = true;
			s.changed_at = 0;
		}

		// Wait for a burst of writes (a file being copied in) to settle before reading.
		if (s.pending && now - s.changed_at > 300)
		{
			s.pending = false;
			rescan = true;
		}
		if (!rescan)
			return false;

		std::vector<config_info_t> fresh = get_config_list();
		const bool changed = !s.listed || !same_list(fresh, s.list);
		s.list = std::move(fresh);
		s.listed = true;

		// The loaded config's file was replaced on disk: apply the new one.
		if (!s.loaded.empty())
		{
			const std::string path = get_config_path(s.loaded);
			std::error_code ec;
			if (fs::exists(widen(path), ec))
			{
				const Stamp current = stamp_of(path);
				if (!(current == s.loaded_stamp))
				{
					const std::string name = s.loaded;
					if (load_config(name))
						note("Reloaded " + name + " from disk");
					else
					{
						s.loaded_stamp = current;   // don't retry until the file changes again
						note("Couldn't read " + name + ".json, kept the current settings");
					}
				}
			}
		}
		return changed;
	}

	const std::vector<config_info_t>& list()
	{
		if (!live().listed)
			watch();
		return live().list;
	}

	const std::string& loaded()
	{
		return live().loaded;
	}

	const std::string& last_message()
	{
		return live().message;
	}

	double last_message_time()
	{
		return live().message.empty() ? 1e9 : static_cast<double>(GetTickCount64() - live().message_at) / 1000.0;
	}
}

// ---- app settings (data\settings.json) -------------------------------------------------------

void external_config::ensure()
{
	config::organize_folders();
	std::error_code ec;
	if (fs::exists(config::data_path("settings.json"), ec))
		return;
	json j;
	j["cheat_name"] = "natware";
	j["autoload_config"] = "";
	j["update_repo"] = external_config::update_repo;
	j["update_token"] = "";
	write_file(config::data_path("settings.json"), j.dump(2) + "\n");
}

void external_config::load()
{
	ensure();
	json j;
	if (!read_json(config::data_path("settings.json"), j))
		return;
	if (j.contains("cheat_name") && j["cheat_name"].is_string())
		external_config::cheat_name = j["cheat_name"].get<std::string>();
	if (j.contains("autoload_config") && j["autoload_config"].is_string())
		external_config::autoload_config = j["autoload_config"].get<std::string>();
	if (j.contains("update_repo") && j["update_repo"].is_string())
		external_config::update_repo = j["update_repo"].get<std::string>();
	if (j.contains("update_token") && j["update_token"].is_string())
		external_config::update_token = j["update_token"].get<std::string>();
	if (j.contains("update_api") && j["update_api"].is_string() && !j["update_api"].get<std::string>().empty())
		external_config::update_api = j["update_api"].get<std::string>();
	while (!external_config::update_api.empty() && external_config::update_api.back() == '/')
		external_config::update_api.pop_back();
	external_config::update_components.clear();
	if (j.contains("components") && j["components"].is_object())
		for (const auto& [name, entry] : j["components"].items())
		{
			if (!entry.is_object())
				continue;
			external_config::update_component_t c;
			auto text = [&](const char* key) { return entry.contains(key) && entry[key].is_string() ? entry[key].get<std::string>() : std::string(); };
			c.repo = text("repo");
			c.tag_prefix = text("tag_prefix");
			c.asset = text("asset");
			if (entry.contains("enabled") && entry["enabled"].is_boolean())
				c.enabled = entry["enabled"].get<bool>();
			external_config::update_components[name] = c;
		}
	// The program used to be called CLAUDEAI; it's natware everywhere now.
	if (lower(external_config::cheat_name) == "claudeai")
	{
		external_config::cheat_name = "natware";
		external_config::save();
	}
}

void external_config::save()
{
	std::error_code ec;
	fs::create_directories("data", ec);
	json j;
	j["cheat_name"] = external_config::cheat_name;
	j["autoload_config"] = external_config::autoload_config;
	j["update_repo"] = external_config::update_repo;
	j["update_token"] = external_config::update_token;
	if (external_config::update_api != "https://api.github.com")
		j["update_api"] = external_config::update_api;
	for (const auto& [name, c] : external_config::update_components)
	{
		json entry = json::object();
		if (!c.repo.empty()) entry["repo"] = c.repo;
		if (!c.tag_prefix.empty()) entry["tag_prefix"] = c.tag_prefix;
		if (!c.asset.empty()) entry["asset"] = c.asset;
		if (!c.enabled) entry["enabled"] = false;
		j["components"][name] = entry;
	}
	write_file(config::data_path("settings.json"), j.dump(2) + "\n");
}
