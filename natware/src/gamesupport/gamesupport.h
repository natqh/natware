#pragma once

#include <cstdint>
#include <string_view>

namespace gamesupport
{
	enum class GameKey : std::uint8_t
	{
		Unknown = 0,
		PhantomForces,
		MurderMystery2,
		LumberTycoon2,
	};

	namespace game_ids
	{
		constexpr std::uint64_t PhantomForces    = 113491250ULL;
		constexpr std::uint64_t MurderMystery2   = 66654135ULL;
		constexpr std::uint64_t LumberTycoon2   = 2471084ULL;
	}

	struct Detection final
	{
		GameKey key{ GameKey::Unknown };
		std::uint64_t game_id{ 0 };
		std::uint64_t place_id{ 0 };
		bool supported{ false };
		std::string_view name{ "Unknown" };
	};

	Detection detect(std::uint64_t game_id, std::uint64_t place_id);

	void log_support_status(const Detection& d);
}