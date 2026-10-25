#pragma once
#include <string>
#include <sdk/math/math.h>

// Ten seconds of what the aimbot saw and did, saved to data\aim_log.csv for diagnosing aim
// problems in a real game. Start it from the Aimbot page, then aim at someone as usual.
namespace aim_log
{
	struct row_t
	{
		bool have_target = false;
		std::string target, part, status;
		math::vector3 body{}, point{};
		math::vector2 screen{}, origin{};
		float distance_px = 0.0f;
		bool hold = false;
		int mode = 0;
	};

	void start(float seconds = 10.0f);
	bool recording();
	void tick(const row_t& row);           // once per aimbot pass (the aim key held)
	void note_move(int dx, int dy);       // a mouse move the aimbot sent this pass
	std::string status();                 // "Recording 6 s..." or where the last log went
}
