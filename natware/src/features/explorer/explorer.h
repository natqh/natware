#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include <atomic>
#include <d3d11.h>
#include <sdk/sdk.h>
#include "../../../ext/imgui/imgui.h"

namespace explorer
{
	void run();

	struct icon_texture_t final
	{
		ID3D11ShaderResourceView* texture = nullptr;
		int width = 0;
		int height = 0;
	};

	struct node_t final
	{
		node_t* parent;
		rbx::instance_t self;
		std::vector<node_t*> children;

		std::string name;
		std::string class_name;

		std::string get_path()
		{
			if (!parent)
			{
				return self.get_name();
			}

			return parent->get_path() + "." + self.get_name();
		}
	};

	class explorer_t final
	{
	public:
		void cache();
		void free_tree(explorer::node_t* node);
		void render_node(explorer::node_t* node);
		void render_properties();
		void render_settings();

		void render_path();
		void render_address();

		void render_part_properties();
		void render_model_teleport();

		bool load_texture_from_memory(const unsigned char* data, unsigned int data_size, const std::string& name);
		void load_all_icons();
		icon_texture_t* get_icon_for_classname(const std::string& classname);

		node_t* root = nullptr;
		node_t* selected_node = nullptr;
		std::unordered_map<std::string, icon_texture_t> icon_cache;
		std::atomic<bool> is_refreshing = false;
		bool textures_loaded = false;
	};

	inline std::unique_ptr<explorer_t> explorer = std::make_unique<explorer_t>();
}
