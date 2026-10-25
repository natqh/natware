#pragma once
#include <string>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <imgui/imgui.h>
#include "../cache.h"
#include "../../game/game.h"

namespace custom_entities
{
	inline std::mutex containers_mtx;

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

	void update_containers();
	void scan_container(const std::string& path);
	void set_container(const std::string& path);
	void refresh_all_containers();
};
