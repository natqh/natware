#include "gamesupport.h"

gamesupport::Detection gamesupport::detect(std::uint64_t game_id, std::uint64_t place_id)
{
	Detection d{};
	d.game_id = game_id;
	d.place_id = place_id;

	switch (game_id)
	{
	case game_ids::PhantomForces:
		d.key = GameKey::PhantomForces;
		d.supported = true;
		d.name = "phantom forces";
		return d;
	case game_ids::MurderMystery2:
		d.key = GameKey::MurderMystery2;
		d.supported = true;
		d.name = "murder mystery 2";
		return d;
	case game_ids::LumberTycoon2:
		d.key = GameKey::LumberTycoon2;
		d.supported = true;
		d.name = "lumber tycoon 2";
		return d;
	default:
		break;
	}

	d.key = GameKey::Unknown;
	d.supported = false;
	d.name = "Unknown";
	return d;
}

void gamesupport::log_support_status(const Detection& d)
{
	if (d.key == GameKey::Unknown)
	{
		return;
	}

	printf("%.*s -> game %s\n",
		(int)d.name.size(),
		d.name.data(),
		d.supported ? "supported" : "unsupported");
}

