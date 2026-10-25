#pragma once
#include <filesystem>
#include <map>
#include <string>
#include <vector>

// Layout beside the exe:
//   configs\        your configs, one .json each (subfolders allowed); dropped-in files show up
//                   in the menu without a restart
//   data\           the program's own files: settings.json, offsets.json, the editor's storage
namespace config
{
	struct config_info_t
	{
		std::string name;   // relative to configs\, without .json; "folder/name" for subfolders
		std::string path;
		std::filesystem::file_time_type modified{};
	};

	std::string get_config_directory();
	std::string get_config_path(const std::string& name);
	std::string data_path(const char* file);
	bool ensure_config_directory();
	// Moves files from the old layout (configs\game, configs\external, offsets.json and the
	// editor storage beside the exe) into the one above. Safe to call more than once.
	void organize_folders();

	std::vector<config_info_t> get_config_list();
	bool save_config(const std::string& name);
	bool load_config(const std::string& name);
	bool delete_config(const std::string& name);
	bool config_exists(const std::string& name);
	void open_file_location();
	void reset_to_defaults();

	// Live folder: call once a frame. Rescans when files in configs\ change and reloads the
	// loaded config when its file is replaced on disk. Returns true when the list changed.
	bool watch(bool force = false);
	const std::vector<config_info_t>& list();
	const std::string& loaded();          // name of the last config loaded or saved, "" if none
	const std::string& last_message();    // what the last reload or failure was, for the menu
	double last_message_time();           // seconds since the message was set

	// Your last settings, kept in data\last.json so natware opens the way you left it.
	bool restore_last_settings();         // at startup; false when there's nothing saved yet
	void autosave();                      // once a frame: writes within a second of a change
	void save_last_settings();            // right away (on quit)
}

namespace external_config
{
	inline std::string cheat_name = "natware";
	inline std::string autoload_config = "";
	// Where updates come from (see updater.h).
	inline std::string update_repo = "natqh/natware";
	inline std::string update_token = "";
	inline std::string update_api = "https://api.github.com";
	// "components" in settings.json: per natware, editor and nexec, anything that differs from
	// the defaults. Empty fields keep the default.
	struct update_component_t
	{
		std::string repo;         // "owner/name" when it isn't released in update_repo
		std::string tag_prefix;   // the tags its releases have, before the version ("editor-v")
		std::string asset;        // the release asset to download ("editor.exe")
		bool enabled = true;      // false: never checked or updated
	};
	inline std::map<std::string, update_component_t> update_components;

	void load();
	void save();
	void ensure();
}
