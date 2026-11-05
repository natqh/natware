#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <sdk/sdk.h>
#include <cache/cache.h>

namespace settings
{

	namespace aimbot
	{
		inline bool enabled{ false };
		inline int keybind{ 0 };
		inline int activation_mode{ 1 };

		inline int mode{ 1 };
		// modes: 0=Mouse, 1=Camera, 2=Silent Aim [PF only]

		inline int target_part{ 1 };
		inline bool air_part_enabled{ false };
		inline int air_part{ 1 };

		inline float fov{ 100.f };
		inline bool use_fov{ false };
		inline bool draw_fov{ false };
		inline float fov_circle_colour[4]{ 1.f, 1.f, 1.f, 1.f };
		inline float fov_outline_colour[4]{ 0.f, 0.f, 0.f, 1.f };

		inline bool smoothing{ false };
		inline float smoothingx{ 10.f };
		inline float smoothingy{ 10.f };

		inline bool enable_prediction{ false };
		inline float prediction_x{ 10.f };
		inline float prediction_y{ 10.f };

		inline bool air_prediction_enabled{ false };
		inline float air_prediction_x{ 10.f };
		inline float air_prediction_y{ 10.f };

		inline int smoothing_style{ 0 };

		inline bool teamcheck{ false };
		inline bool knock_check{ false };
		inline bool sticky_aim{ false };
		inline bool closest_player{ false };   // sort by world distance, not crosshair distance
		inline bool wallcheck{ false };        // skip targets behind walls

		inline bool health_check_enabled{ false };
		inline float min_health{ 0.0f };

		namespace triggerbot
		{
			inline bool enabled{ false };
			inline int keybind{ 0 };
			inline int activation_mode{ 1 }; // 0=Toggle, 1=Hold, 2=Always
			inline int fire_mode{ 0 }; // 0=Click, 1=Hold
			inline float clicks_per_second{ 10.f };
			inline float hold_duration{ 0.15f };
			inline float reaction_ms{ 80.f };
			inline float max_distance{ 300.f };
			inline bool max_distance_enabled{ false };
			inline bool wallcheck{ false };
		}

		inline bool offset_enabled{ false };
		inline float offset_x{ 0.0f };
		inline float offset_y{ 0.0f };

		// PF Silent Aim settings
		inline float ai_silent_y_offset{ 0.5f };   // upward offset in studs (head adjustment)

	}

	namespace rage
	{
		inline bool hitsounds{ false };
		inline int hitsound_type{ 0 };
		inline int hitsound_method{ 0 };
		inline bool rapidfire{ false };
		inline int rapidfire_key{ 0 };
		inline int rapidfire_key_mode{ 0 };    // 0 Toggle, 1 Hold, 2 Always
		inline int rapidfire_mode{ 2 };        // 0 gun values, 1 auto-click, 2 both
		inline float rapidfire_cps{ 15.0f };   // auto-click rate
		inline bool noclip{ false };
		inline int noclip_key{ 0 };
		inline int noclip_key_mode{ 0 };
		inline bool hit_tracers{ false };
		inline float hit_tracers_color[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
		inline float hit_tracers_duration{ 1.0f };

		namespace hipheight
		{
			inline bool enabled{ false };
			inline float height{ 2.0f };
			inline int key{ 0 };
			inline int key_mode{ 0 };
		}

		namespace hitbox_expander
		{
			inline bool enabled{ false };
			inline int target_part{ 1 };
			inline int key{ 0 };
			inline int key_mode{ 0 };
			inline float size_x{ 2.2f };
			inline float size_y{ 2.2f };
			inline float size_z{ 1.2f };
			inline bool knock_check{ false };
		}

		namespace spin360
		{
			inline bool enabled{ false };
			inline float speed{ 5.0f };
			inline int keybind{ 0 };
			inline int activation_mode{ 1 };
		}


		namespace playerlist
		{
			inline bool enabled{ false };
			inline int selected_index{ -1 };
			inline std::uint64_t selected_address{ 0 };
			inline std::string selected_name{};

			inline bool is_spectating{ false };
			inline std::string spectate_target_name{};
			inline std::uint64_t original_camera_subject{ 0 };

			inline math::vector3 saved_position{};
			inline bool has_saved_position{ false };

			inline std::unordered_set<std::string> whitelist{};

			inline std::string target_name{};
			inline std::uint64_t target_address{ 0 };
		}

		inline bool is_whitelisted(const std::string& name)
		{
			return playerlist::whitelist.count(name) > 0;
		}
	}

	namespace custom_entities
	{
		struct custom_entity_t
		{
			rbx::instance_t instance;
			std::string name;
			std::string container_path;
			float distance = 0.f;
			std::unordered_map<std::string, cache::part_data_t> parts;
			cache::part_data_t root_part;
			cache::part_data_t head;
			bool enabled = true;
		};

		struct custom_container_t
		{
			std::string path;
			std::string name;
			bool enabled = true;
			std::vector<custom_entity_t> entities;
		};

		inline std::vector<custom_container_t> containers;
		inline std::string current_input = "Workspace.Bots";
		inline bool show_custom_entities = false;
		inline bool auto_refresh = false;
		inline float refresh_rate = 0.005f;
	}

	namespace silentaim
	{
		inline bool enabled{ false };
		inline int keybind{ 0 };
		inline int activation_mode{ 1 };

		inline int target_part{ 1 };

		inline float fov{ 100.f };
		inline bool use_fov{ false };
		inline bool draw_fov{ false };
		inline bool attach_fov_to_target{ false };
		inline float fov_circle_colour[4]{ 1.f, 1.f, 1.f, 1.f };
		inline float fov_outline_colour[4]{ 0.f, 0.f, 0.f, 1.f };

		inline bool enable_prediction{ false };
		inline float prediction_x{ 10.f };
		inline float prediction_y{ 10.f };

		inline bool sticky_aim{ false };
		inline bool auto_switch{ false };
		inline bool spoof_mouse{ true };
		inline bool use_aimbot_target{ false };

		inline bool teamcheck{ false };
		inline bool guncheck{ false };
		inline bool wallcheck{ false };        // skip targets behind walls
		inline bool knock_check{ false };

		inline int priorities{ 0 };
		inline bool health_check_enabled{ false };
		inline float min_health{ 0.0f };

		inline bool hitchance_enabled{ false };
		inline float hitchance{ 100.0f };

		inline bool draw_target_dot{ false };
		inline float target_dot_color[4]{ 1.f, 0.f, 0.f, 1.f };
		inline float target_dot_size{ 4.0f };

		inline bool draw_snap_line{ false };
		inline float snap_line_color[4]{ 1.f, 1.f, 1.f, 1.f };

		namespace triggerbot
		{
			inline bool enabled{ false };
			inline int keybind{ 0 };
			inline int activation_mode{ 1 }; // 0=Toggle, 1=Hold, 2=Always
			inline int fire_mode{ 0 }; // 0=Click, 1=Hold
			inline float clicks_per_second{ 10.f };
			inline float hold_duration{ 0.15f };
			inline float reaction_ms{ 80.f };
			inline float max_distance{ 300.f };
			inline bool max_distance_enabled{ false };
			inline bool wallcheck{ false };
		}
	}

	namespace visuals
	{
		inline bool radar_enabled{ false };
		inline float radar_size{ 0.f };

		inline bool enable_enemies{ false };
		inline int esp_key{ 0 };
		inline int esp_key_mode{ 0 };
		inline int esp_style{ 0 };             // 0 natware, 1 classic (pixel fonts, hard outlines)
		inline bool enable_client{ false };

		inline bool box{ false };
		inline int box_type{ 0 };
		inline float box_glow_strength{ 0.5f };
		inline float box_color[4]{ 1.f, 1.f, 1.f, 1.f };
		inline bool box_fill{ false };
		inline float box_fill_color[4]{ 0.2f, 0.2f, 0.2f, 0.3f };

		inline bool name{ false };
		inline int name_type{ 0 };
		inline int name_display_type{ 0 };
		inline float name_color[4]{ 1.f, 1.f, 1.f, 1.f };
		inline float name_color_blend_start[4]{ 1.f, 1.f, 1.f, 1.f };
		inline float name_color_blend_end[4]{ 0.f, 0.f, 1.f, 1.f };
		inline bool blend{ false };
		inline bool avatar{ false };

		inline bool healthbar{ false };
		inline float healthbar_color[4]{ 0.f, 1.f, 0.f, 1.f };
		inline bool health_based_healthbar{ false };
		inline bool gradient_healthbar{ false };
		inline float gradient_healthbar_color_start[4]{ 1.f, 1.f, 1.f, 1.f };
		inline float gradient_healthbar_color_end[4]{ 0.f, 1.f, 0.f, 1.f };
		inline bool health_percent{ false };
		inline float health_percent_color[4]{ 1.f, 1.f, 1.f, 1.f };

		inline bool armorbar{ false };
		inline float armorbar_color[4]{ 0.275f, 0.627f, 1.f, 1.f };

		inline bool distance{ false };
		inline int distance_measurement{ 0 };
		inline float distance_color[4]{ 1.f, 1.f, 1.f, 1.f };

		inline bool tool{ false };
		inline float tool_color[4]{ 1.f, 1.f, 1.f, 1.f };

		inline bool team_name{ false };

		inline int esp_font{ 0 };
		inline bool local_player{ false };
		inline bool chams{ false };
		inline int chams_key{ 0 };
		inline int chams_key_mode{ 0 };
		inline int chams_style{ 0 };           // 0 Flat, 1 Engine (animated effects), 2 Roblox highlight
		inline int chams_effect{ 0 };          // Engine's effect: 0 Water, 1 Plasma, 2 Hologram, 3 Galaxy
		inline float chams_texture_speed{ 1.0f };
		inline float chams_texture_scale{ 1.0f };
		inline float chams_fill_color[4]{ 1.f, 0.f, 0.f, 0.5f };
		inline float chams_outline_color[4]{ 1.f, 1.f, 1.f, 1.f };
		inline bool chams_fill_enabled{ true };
		inline bool chams_outline_enabled{ true };

		inline bool skeleton{ false };
		inline float skeleton_color[4]{ 1.f, 1.f, 1.f, 1.f };
		inline float skeleton_thickness{ 1.5f };

		inline bool target_warning_icon{ false };
		inline float target_warning_icon_size{ 24.0f };

		inline bool flags{ false };
		inline float flags_state_colour[4]{ 1.f, 1.f, 1.f, 1.f };

		inline bool client_box{ false };
		inline float client_box_color[4]{ 1.f, 1.f, 1.f, 1.f };
		inline bool client_box_fill{ false };
		inline float client_box_fill_color[4]{ 0.2f, 0.2f, 0.2f, 0.3f };

		inline bool client_name{ false };
		inline float client_name_color[4]{ 1.f, 1.f, 1.f, 1.f };
		inline bool client_avatar{ false };

		inline bool client_healthbar{ false };
		inline float client_healthbar_color[4]{ 0.f, 1.f, 0.f, 1.f };
		inline bool client_health_percent{ false };
		inline float client_health_percent_color[4]{ 1.f, 1.f, 1.f, 1.f };

		inline bool client_armorbar{ false };
		inline float client_armorbar_color[4]{ 0.275f, 0.627f, 1.f, 1.f };

		inline bool client_distance{ false };
		inline float client_distance_color[4]{ 1.f, 1.f, 1.f, 1.f };

		inline bool client_tool{ false };
		inline float client_tool_color[4]{ 1.f, 1.f, 1.f, 1.f };

		inline bool client_chams{ false };
		inline float client_chams_fill_color[4]{ 1.f, 0.f, 0.f, 0.5f };
		inline float client_chams_outline_color[4]{ 1.f, 1.f, 1.f, 1.f };

		inline bool client_skeleton{ false };
		inline float client_skeleton_color[4]{ 1.f, 1.f, 1.f, 1.f };

		inline bool client_flags{ false };
		inline float client_flags_state_colour[4]{ 1.f, 1.f, 1.f, 1.f };

		inline bool client_headless{ false };
		inline bool client_korblox{ false };

		inline bool debug_wallcheck{ false };

		inline bool view_hitbox{ false };
		inline float view_hitbox_color[4]{ 1.f, 0.f, 0.f, 1.f };

		inline float fade_in_speed{ 5.0f };
		inline float fade_out_speed{ 5.0f };

		inline bool knock_check{ false };
		inline bool teamcheck{ false };
		inline bool use_team_color{ false };
		inline bool ignore_whitelisted{ false };

		inline bool max_distance_enabled{ false };
		inline float max_distance{ 500.f };

		inline bool mm2_esp{ false };

		inline bool hit_tracers_enabled{ false };
		inline int hit_tracers_method{ 0 };
		inline float hit_tracers_color[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
		inline float hit_tracers_duration{ 1.0f };
	}

	namespace movement
	{
		namespace speedhack
		{
			inline bool enabled{ false };
			inline int mode{ 0 };
			inline float speed{ 50.0f };
			inline int keybind{ 0 };
			inline int activation_mode{ 1 };
		}

		namespace jumphack
		{
			inline bool enabled{ false };
			inline float value{ 50.0f };
			inline int keybind{ 0 };
			inline int activation_mode{ 1 };
		}

		namespace nojumpcooldown
		{
			inline bool enabled{ false };
		}

		namespace flyhack
		{
			inline bool enabled{ false };
			inline int mode{ 0 };
			inline float speed{ 50.0f };
			inline int keybind{ 0 };
			inline int activation_mode{ 1 };
		}

		namespace tickrate
		{
			inline bool enabled{ false };
			inline float value{ 240.0f };
		}

		namespace orbit
		{
			inline bool enabled{ false };
			inline int key{ 0 };
			inline int key_mode{ 0 };
			inline int orbit_type{ 0 };
			inline float speed{ 30.0f };
			inline float radius{ 10.0f };
			inline float height_offset{ 10.0f };
			inline bool spectate_target{ false };
			inline bool randomize{ false };
			inline float randomize_x{ 5.0f };
			inline float randomize_y{ 5.0f };
		}

		namespace gravity
		{
			inline bool enabled{ false };
			inline float value{ 196.2f };
		}

	}

	namespace ui
	{
		inline bool watermark{ true };
		inline bool keybinds{ true };
	}

	namespace watermark
	{
		// Elements toggle
		inline bool show_cheat_name{ true };
		inline bool show_display_name{ false };
		inline bool show_username{ false };
		inline bool show_fps{ true };
		inline bool show_server_ip{ false };

		// Separator
		inline int separator_type{ 0 }; // 0= " | ", 1= " - ", 2= " / ", 3= " :: ", 4= "  "

		// Color
		inline float text_color[4]{ 1.f, 1.f, 1.f, 1.f };
		inline bool rainbow{ false };
		inline float rainbow_speed{ 1.0f };

		// Drag position (saved)
		inline float pos_x{ 20.0f };
		inline float pos_y{ 100.0f };
		inline bool pos_initialized{ false };
	}

	namespace menu
	{
		inline int menu_keybind{ VK_F6 };
		inline bool watermark{ false };
		inline bool vsync{ false };
		inline bool performance_mode{ false };
		inline bool stay_loaded{ true };
	}

	namespace lighting
	{
		namespace fog
		{
			inline bool enabled{ false };
			inline float fog_start{ 0.0f };
			inline float fog_end{ 500.0f };
			inline float fog_r{ 0.75f };
			inline float fog_g{ 0.75f };
			inline float fog_b{ 0.75f };
		}

		namespace shadows
		{
			inline bool disable{ false };
		}

		namespace clocktime
		{
			inline bool enabled{ false };
			inline float clock_time{ 12.0f };
		}

		namespace skybox
		{
			inline bool enabled{ false };
			inline int preset_index{ 0 };
		}

		namespace exposure
		{
			inline bool enabled{ false };
			inline float exposure{ 0.f };
		}


	}

	namespace exploits
	{
		namespace antiafk
		{
			inline bool enabled{ false };
		}

		namespace freezeplayer
		{
			inline bool enabled{ false };
			inline int keybind{ 0 };
			inline int activation_mode{ 1 };
		}

	}

	enum AppMode { MODE_PICKER, MODE_EXECUTOR, MODE_EXTERNAL };
	inline AppMode app_mode{ MODE_PICKER };

	namespace executor
	{
		inline char script_buffer[4096]{ "print(\"hello from natware\")" };
		inline bool auto_init{ true };
	}

	namespace cilent
	{
		namespace fpscaps
		{
			inline bool enabled{ false };
		}
	}

	namespace globals
	{
		inline bool is_game_active{ true };
		// Closing natware from the picker, the waiting card, the taskbar or Alt+F4.
		inline bool quit_requested{ false };
		inline std::string offset_validation_result{};
	}

}