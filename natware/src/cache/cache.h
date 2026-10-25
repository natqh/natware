#pragma once
#include <string>
#include <mutex>
#include <unordered_map>

#include <sdk/sdk.h>
#include <sdk/math/math.h>

namespace cache
{
	inline std::mutex mtx;

	struct part_data_t
	{
		rbx::part_t part;
		std::string name;
		math::vector3 position;
		math::vector3 size;
		math::matrix3 rotation;
	};

	struct entity_t final
	{
		rbx::instance_t instance;
		std::string name;
		std::string display_name;

		std::uint8_t rig_type;

		rbx::humanoid_t humanoid;
		std::unordered_map<std::string, rbx::part_t> parts;

		std::uint64_t team{ 0 };
		float team_color[3]{ 0.f, 0.f, 0.f };
		bool has_team_color{ false };
		float health{ 0.0f };
		float max_health{ 0.0f };
		bool knocked{ false };

		std::string tool_name;
		float armor_percent{ 0.0f };
		int armor_value{ -1 };

		// MM2 role: 0=Unknown, 1=Innocent, 2=Sheriff, 3=Murderer
		int mm2_role{ 0 };
	};

	inline cache::entity_t cached_local_player;
	inline std::vector<cache::entity_t> cached_players;

	void run();
}