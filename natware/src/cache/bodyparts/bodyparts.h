#pragma once
#include "../cache.h"
#include <sdk/math/math.h>
#include <string>
#include <vector>

namespace bodyparts
{
	std::vector<std::string> get_part_names(const cache::entity_t& entity, const std::string& part_type);

	bool get_part_position(const cache::entity_t& entity, const std::string& part_type, math::vector3& out_pos);

	bool is_r15(const cache::entity_t& entity);
}