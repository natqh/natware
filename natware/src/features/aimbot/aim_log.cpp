#include "aim_log.h"

#include <chrono>
#include <cstdio>
#include <mutex>
#include <vector>

#include <features/config/config.h>

namespace
{
	using clock = std::chrono::steady_clock;

	struct entry_t
	{
		float t_ms;
		aim_log::row_t row;
		int move_x, move_y;
	};

	std::mutex g_mtx;
	bool g_on = false;
	clock::time_point g_started{}, g_until{};
	std::vector<entry_t> g_rows;
	int g_move_x = 0, g_move_y = 0;
	std::string g_status;

	// Commas and quotes out of names, so the file stays one value per column.
	std::string clean(std::string s)
	{
		for (char& c : s)
			if (c == ',' || c == '"' || c == '\n' || c == '\r')
				c = ' ';
		return s;
	}

	void save_locked()
	{
		const std::string path = config::data_path("aim_log.csv");
		FILE* f = nullptr;
		if (fopen_s(&f, path.c_str(), "w") != 0 || !f)
		{
			g_status = "Couldn't write " + path;
			return;
		}
		fprintf(f, "t_ms,mode,target,part,status,hold,distance_px,origin_x,origin_y,screen_x,screen_y,"
			"body_x,body_y,body_z,point_x,point_y,point_z,move_x,move_y\n");
		for (const entry_t& e : g_rows)
		{
			const aim_log::row_t& r = e.row;
			fprintf(f, "%.2f,%d,%s,%s,%s,%d,%.1f,%.1f,%.1f,%.1f,%.1f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%d,%d\n",
				e.t_ms, r.mode, clean(r.target).c_str(), clean(r.part).c_str(), clean(r.status).c_str(), r.hold ? 1 : 0,
				r.distance_px, r.origin.x, r.origin.y, r.screen.x, r.screen.y,
				r.body.x, r.body.y, r.body.z, r.point.x, r.point.y, r.point.z, e.move_x, e.move_y);
		}
		fclose(f);
		g_status = "Saved " + std::to_string(g_rows.size()) + " rows to data\\aim_log.csv";
		g_rows.clear();
		g_rows.shrink_to_fit();
	}
}

void aim_log::start(float seconds)
{
	std::lock_guard<std::mutex> lock(g_mtx);
	g_on = true;
	g_started = clock::now();
	g_until = g_started + std::chrono::milliseconds(static_cast<int>(seconds * 1000.0f));
	g_rows.clear();
	g_rows.reserve(12000);
	g_move_x = g_move_y = 0;
	g_status.clear();
}

bool aim_log::recording()
{
	std::lock_guard<std::mutex> lock(g_mtx);
	return g_on;
}

void aim_log::note_move(int dx, int dy)
{
	std::lock_guard<std::mutex> lock(g_mtx);
	if (!g_on)
		return;
	g_move_x += dx;
	g_move_y += dy;
}

void aim_log::tick(const row_t& row)
{
	std::lock_guard<std::mutex> lock(g_mtx);
	if (!g_on)
		return;
	const clock::time_point now = clock::now();
	g_rows.push_back({ std::chrono::duration<float, std::milli>(now - g_started).count(), row, g_move_x, g_move_y });
	g_move_x = g_move_y = 0;
	if (now >= g_until || g_rows.size() >= 20000)
	{
		g_on = false;
		save_locked();
	}
}

std::string aim_log::status()
{
	std::lock_guard<std::mutex> lock(g_mtx);
	if (g_on)
	{
		const float left = std::chrono::duration<float>(g_until - clock::now()).count();
		char text[96];
		snprintf(text, sizeof(text), "Recording, %.0f s left: hold your aim key on someone (%zu rows)", left > 0 ? left : 0.0f, g_rows.size());
		return text;
	}
	return g_status;
}
