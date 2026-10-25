#include "bodyparts.h"
#include <sdk/math/math.h>

namespace bodyparts
{
	bool is_r15(const cache::entity_t& entity)
	{
		auto upper_torso_it = entity.parts.find("UpperTorso");
		auto lower_torso_it = entity.parts.find("LowerTorso");
		return (upper_torso_it != entity.parts.end() && lower_torso_it != entity.parts.end());
	}

	std::vector<std::string> get_part_names(const cache::entity_t& entity, const std::string& part_type)
	{
		std::vector<std::string> names;
		bool r15 = is_r15(entity);

		if (part_type == "Head")
		{
			names.push_back("Head");
		}
		else if (part_type == "HumanoidRootPart")
		{
			names.push_back("HumanoidRootPart");
		}
		else if (part_type == "LeftArm")
		{
			if (r15)
			{
				names.push_back("LeftUpperArm");
			}
			else
			{
				names.push_back("Left Arm");
			}
		}
		else if (part_type == "RightArm")
		{
			if (r15)
			{
				names.push_back("RightUpperArm");
			}
			else
			{
				names.push_back("Right Arm");
			}
		}
		else if (part_type == "LeftLeg")
		{
			if (r15)
			{
				names.push_back("LeftUpperLeg");
			}
			else
			{
				names.push_back("Left Leg");
			}
		}
		else if (part_type == "RightLeg")
		{
			if (r15)
			{
				names.push_back("RightUpperLeg");
			}
			else
			{
				names.push_back("Right Leg");
			}
		}

		return names;
	}

	bool get_part_position(const cache::entity_t& entity, const std::string& part_type, math::vector3& out_pos)
	{
		auto part_names = get_part_names(entity, part_type);

		for (const auto& name : part_names)
		{
			auto it = entity.parts.find(name);
			if (it != entity.parts.end() && it->second.address)
			{
				rbx::part_t part{ it->second };
				if (part.address)
				{
					rbx::primitive_t prim = part.get_primitive();
					if (prim.address)
					{
						out_pos = prim.get_position();
						return true;
					}
				}
			}
		}

		return false;
	}
}