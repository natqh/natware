#pragma once

#include <cstdint>
#include <vector>
#include <mutex>
#include <chrono>
#include <sdk/math/math.h>

namespace rage
{
	struct hit_tracer_data_t
	{
		math::vector3 hit_position;
		math::vector3 source_position;
		std::chrono::steady_clock::time_point timestamp;
	};

	void hittracers_detector_thread();
	void draw_hit_tracers();
}
