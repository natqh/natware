#pragma once
#include <cache/cache.h>
#include <unordered_map>
#include <sdk/math/math.h>

namespace aimbot
{
	inline cache::entity_t player{};
	inline cache::entity_t sticky_target{};
	inline cache::entity_t last_target{};

	void run();
	std::string status();   // what the aimbot is doing, for the menu
}
