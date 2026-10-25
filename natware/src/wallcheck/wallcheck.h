#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "obb.h"
#include <sdk/sdk.h>
#include <sdk/math/math.h>

// One read of the map: the parts that block sight, a BVH over them for fast segment tests, and
// the parts the radar draws. It's built on a background thread and swapped in whole, so a reader
// on any thread keeps a consistent copy for as long as it holds the pointer.
struct wallcheck_snapshot_t final
{
	std::vector<rbx::obb> obstacles;

	// How much of its box each obstacle fills: a wedge is half of it (the slope faces the part's
	// front), a ball and a cylinder (along the part's X) are round. Missing means the whole box.
	// Mesh and union parts stay whole boxes: their real shape isn't in memory.
	enum shape_t : std::uint8_t { box_shape = 0, wedge_shape = 1, ball_shape = 2, cylinder_shape = 3 };
	std::vector<std::uint8_t> shapes;

	// Every part read (walls or not), so a quick re-read can follow parts that move, vanish or
	// turn solid (doors, barriers) between full reads of the map.
	struct source_t
	{
		std::uint64_t part{ 0 };
		std::uint64_t prim{ 0 };
		math::vector3 size{};
		std::uint8_t shape{ box_shape };
	};
	std::vector<source_t> sources;
	bool trust_transparency{ false };
	bool trust_query{ false };
	bool trust_collide{ false };

	// count == 0 marks an inner node whose children sit at first and first + 1; a leaf covers
	// order[first, first + count).
	struct node_t
	{
		math::vector3 mn, mx;
		std::int32_t first{ 0 };
		std::int32_t count{ 0 };
	};
	std::vector<node_t> nodes;
	std::vector<std::int32_t> order;

	// What the read found, for the menu's readout.
	struct stats_t
	{
		int parts{ 0 };              // parts read (characters excluded)
		int walls{ 0 };              // the ones that block sight
		int invisible{ 0 };          // skipped: Transparency 0.5 or more (glass, invisible)
		int no_query{ 0 };           // skipped: CanQuery or CanCollide off
		bool transparency_trusted{ true };
		bool flags_trusted{ true };
		bool budget_hit{ false };    // the place was too big to read whole
		float read_ms{ 0.0f };
	} stats;
	std::chrono::steady_clock::time_point built_at{};   // the last full read of the map
	std::chrono::steady_clock::time_point posed_at{};   // the last quick re-read of where parts are

	bool has_radar{ false };
	std::vector<rbx::obb> radar_geometry;
	std::vector<std::uint32_t> radar_geometry_colors;

	// False if a wall blocks the segment from origin to target. A box holding either end (a zone
	// the camera sits in, a part the target stands inside) doesn't count as a wall.
	bool is_visible(const math::vector3& origin, const math::vector3& target) const;
};

class c_wallcheck final {
public:
	// Called every frame: starts the background reader once. It re-reads the map every few
	// seconds while the radar, the ESP wall check or a triggerbot wall check is on, and straight
	// away when one of them is turned on or Roblox moves to a new place. In between, where every
	// part is and whether it's solid is re-read several times a second.
	void tick();

	bool ready() const;
	std::shared_ptr<const wallcheck_snapshot_t> snapshot() const;

	// True until the first read lands (no map yet means nothing to hide behind).
	bool is_visible(const math::vector3& origin, const math::vector3& target) const;

private:
	static bool needed();
	void worker();
	std::shared_ptr<const wallcheck_snapshot_t> build(bool want_radar) const;
	static std::shared_ptr<const wallcheck_snapshot_t> repose(const wallcheck_snapshot_t& last);

	mutable std::mutex mtx;
	std::shared_ptr<const wallcheck_snapshot_t> current;
	std::atomic<bool> started{ false };
};

inline std::unique_ptr<c_wallcheck> wallcheck = std::make_unique<c_wallcheck>();
