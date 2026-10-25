#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <cache/cache.h>
#include <sdk/math/math.h>

// One frame's read of a character's body: each body part's position, rotation and size.
// A part costs one memory read per frame (its primitive pointer is remembered between
// frames), and the skeleton, chams and hitbox views all share the same read.
namespace rig
{
	enum class kind_t { unknown, r6, r15 };

	struct part_t
	{
		std::string name;
		std::uint64_t address{ 0 };   // the BasePart
		math::vector3 pos;
		math::matrix3 rot;            // columns: the part's right, up and back vectors
		math::vector3 size;

		math::vector3 up() const { return { rot.m[0][1], rot.m[1][1], rot.m[2][1] }; }
		math::vector3 top() const { return pos + up() * (size.y * 0.5f); }
		math::vector3 bottom() const { return pos - up() * (size.y * 0.5f); }
	};

	struct body_t
	{
		kind_t kind{ kind_t::unknown };
		std::vector<part_t> parts;    // body parts only: no HumanoidRootPart, no accessories

		const part_t* find(std::string_view name) const;
	};

	// False when none of the entity's body parts could be read.
	bool read(const cache::entity_t& entity, body_t& out);

	// One part's position: a single read (its primitive pointer is remembered).
	bool position(std::uint64_t part_address, math::vector3& out);

	// Bones as pairs of world points (R6 and R15 rigs; nothing for other rigs).
	void skeleton(const body_t& body, std::vector<std::pair<math::vector3, math::vector3>>& bones);
}
