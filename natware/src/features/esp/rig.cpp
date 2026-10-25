#include "rig.h"

#include <array>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <windows.h>

#include <game/game.h>
#include <memory/memory.h>
#include <Offsets/Offsets.hpp>

namespace
{
	constexpr const char* kR15[] = {
		"Head", "UpperTorso", "LowerTorso",
		"LeftUpperArm", "LeftLowerArm", "LeftHand", "RightUpperArm", "RightLowerArm", "RightHand",
		"LeftUpperLeg", "LeftLowerLeg", "LeftFoot", "RightUpperLeg", "RightLowerLeg", "RightFoot",
	};
	constexpr const char* kR6[] = { "Head", "Torso", "Left Arm", "Right Arm", "Left Leg", "Right Leg" };

	bool finite3(const math::vector3& v)
	{
		return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
	}

	// Primitive pointers per part, refreshed every couple of seconds. One map per thread, so
	// the ESP and the triggerbot never share it.
	struct prim_entry_t
	{
		std::uint64_t prim;
		ULONGLONG read_at;
	};

	std::uint64_t primitive_of(std::uint64_t part)
	{
		thread_local std::unordered_map<std::uint64_t, prim_entry_t> cache;
		thread_local ULONGLONG last_sweep = 0;
		const ULONGLONG now = GetTickCount64();

		if (now - last_sweep > 10000)
		{
			for (auto it = cache.begin(); it != cache.end();)
				it = now - it->second.read_at > 10000 ? cache.erase(it) : std::next(it);
			last_sweep = now;
		}

		auto it = cache.find(part);
		if (it != cache.end() && now - it->second.read_at < 2000)
			return it->second.prim;

		const std::uint64_t prim = memory->read<std::uint64_t>(part + Offsets::BasePart::Primitive);
		cache[part] = { prim, now };
		return prim;
	}

	// Rotation, position and size sit close together in the primitive: read them in one go
	// when they fit in one block, which they do in every layout seen so far.
	bool read_transform(std::uint64_t prim, rig::part_t& p)
	{
		const std::uintptr_t rot_off = Offsets::Primitive::Rotation;
		const std::uintptr_t pos_off = Offsets::Primitive::Position;
		const std::uintptr_t size_off = Offsets::Primitive::Size;
		const std::uintptr_t lo = (std::min)((std::min)(rot_off, pos_off), size_off);
		const std::uintptr_t hi = (std::max)((std::max)(rot_off + sizeof(math::matrix3), pos_off + sizeof(math::vector3)), size_off + sizeof(math::vector3));

		constexpr std::size_t kBlock = 512;
		if (hi - lo <= kBlock)
		{
			const auto block = memory->read<std::array<std::uint8_t, kBlock>>(prim + lo);
			std::memcpy(&p.rot, block.data() + (rot_off - lo), sizeof(math::matrix3));
			std::memcpy(&p.pos, block.data() + (pos_off - lo), sizeof(math::vector3));
			std::memcpy(&p.size, block.data() + (size_off - lo), sizeof(math::vector3));
		}
		else
		{
			p.rot = memory->read<math::matrix3>(prim + rot_off);
			p.pos = memory->read<math::vector3>(prim + pos_off);
			p.size = memory->read<math::vector3>(prim + size_off);
		}

		if (!finite3(p.pos) || !finite3(p.size) || p.size.x <= 0.f || p.size.y <= 0.f || p.size.z <= 0.f)
			return false;
		for (int i = 0; i < 9; ++i)
			if (!std::isfinite(p.rot.data()[i]))
				return false;
		return true;
	}

	// Every visible part of a character model, for rigs that aren't R6 or R15 (Phantom Forces,
	// custom characters): the player cache only names a few of their parts. The list is read
	// again every 1.5 s; invisible parts (hitboxes) and the root are left out.
	struct model_entry_t
	{
		std::vector<std::uint64_t> parts;
		ULONGLONG read_at = 0;
	};

	const std::vector<std::uint64_t>& model_parts(const cache::entity_t& e)
	{
		thread_local std::unordered_map<std::uint64_t, model_entry_t> models;
		thread_local ULONGLONG last_sweep = 0;
		const ULONGLONG now = GetTickCount64();
		if (now - last_sweep > 10000)
		{
			for (auto it = models.begin(); it != models.end();)
				it = now - it->second.read_at > 10000 ? models.erase(it) : std::next(it);
			last_sweep = now;
		}

		model_entry_t& m = models[e.instance.address];
		if (m.read_at != 0 && now - m.read_at < 1500)
			return m.parts;
		m.parts.clear();
		m.read_at = now;

		// In Phantom Forces the entity is the character model itself; elsewhere it's the Player.
		const std::uint64_t model = game::is_phantom_forces
			? e.instance.address
			: rbx::player_t(e.instance.address).get_model_instance().address;
		if (!model)
			return m.parts;

		for (const rbx::instance_t& child : rbx::instance_t(model).get_children())
		{
			const std::string cls = child.get_class_name();
			if (cls.find("Part") == std::string::npos && cls != "UnionOperation")
				continue;
			if (child.get_name() == "HumanoidRootPart")
				continue;
			const float transparency = memory->read<float>(child.address + Offsets::BasePart::Transparency);
			if (std::isfinite(transparency) && transparency >= 0.95f)
				continue;
			m.parts.push_back(child.address);
			if (m.parts.size() >= 48)
				break;
		}
		return m.parts;
	}

	rig::kind_t kind_of(const cache::entity_t& e)
	{
		if (game::is_phantom_forces)
			return rig::kind_t::unknown;   // its parts are named by order, not by limb
		if (e.parts.count("UpperTorso") && e.parts.count("LowerTorso"))
			return rig::kind_t::r15;
		if (e.parts.count("Torso") && (e.parts.count("Left Arm") || e.parts.count("Right Arm")))
			return rig::kind_t::r6;
		return rig::kind_t::unknown;
	}
}

const rig::part_t* rig::body_t::find(std::string_view name) const
{
	for (const part_t& p : parts)
		if (p.name == name)
			return &p;
	return nullptr;
}

bool rig::position(std::uint64_t part_address, math::vector3& out)
{
	if (!part_address)
		return false;
	const std::uint64_t prim = primitive_of(part_address);
	if (!prim)
		return false;
	out = memory->read<math::vector3>(prim + Offsets::Primitive::Position);
	return finite3(out);
}

bool rig::read(const cache::entity_t& entity, body_t& out)
{
	out.parts.clear();
	out.kind = kind_of(entity);

	auto add = [&](const std::string& name, const rbx::part_t& part)
	{
		if (!part.address)
			return;
		const std::uint64_t prim = primitive_of(part.address);
		if (!prim)
			return;
		part_t p;
		p.name = name;
		p.address = part.address;
		if (!read_transform(prim, p))
			return;
		if (out.kind == kind_t::unknown && (std::max)((std::max)(p.size.x, p.size.y), p.size.z) > 10.f)
			return;   // not a body part (a vehicle, a giant hitbox)
		out.parts.push_back(std::move(p));
	};

	if (out.kind == kind_t::r15 || out.kind == kind_t::r6)
	{
		const auto add_named = [&](auto& names)
		{
			for (const char* name : names)
				if (auto it = entity.parts.find(name); it != entity.parts.end())
					add(it->first, it->second);
		};
		if (out.kind == kind_t::r15)
			add_named(kR15);
		else
			add_named(kR6);
	}
	else
	{
		// The whole visible model; the part the cache knows as the head keeps that name.
		const auto head_it = entity.parts.find("Head");
		const std::uint64_t head = head_it != entity.parts.end() ? head_it->second.address : 0;
		static const std::string kHeadName = "Head", kPartName = "Part";
		for (std::uint64_t address : model_parts(entity))
			add(address == head ? kHeadName : kPartName, rbx::part_t(address));

		if (out.parts.empty())
		{
			// Couldn't list the model: fall back to the parts the cache named.
			std::uint64_t seen[64]{};
			int seen_count = 0;
			for (const auto& [name, part] : entity.parts)
			{
				if (name == "HumanoidRootPart" && entity.parts.size() > 2)
					continue;
				bool dup = false;
				for (int i = 0; i < seen_count; ++i)
					dup = dup || seen[i] == part.address;
				if (dup)
					continue;   // Phantom Forces maps two names to its torso
				if (seen_count < 64)
					seen[seen_count++] = part.address;
				add(name, part);
			}
		}
	}
	return !out.parts.empty();
}

void rig::skeleton(const body_t& body, std::vector<std::pair<math::vector3, math::vector3>>& bones)
{
	bones.clear();
	auto bone = [&](const math::vector3& a, const math::vector3& b) { bones.emplace_back(a, b); };

	if (body.kind == kind_t::r15)
	{
		const part_t* head = body.find("Head");
		const part_t* upper = body.find("UpperTorso");
		const part_t* lower = body.find("LowerTorso");
		if (!upper)
			return;
		const math::vector3 neck = upper->top();
		const math::vector3 pelvis = lower ? lower->pos : upper->bottom();
		if (head)
			bone(head->pos, neck);
		bone(neck, pelvis);

		// shoulder or hip, then down each segment's top, ending at the hand or foot tip
		auto limb = [&](const math::vector3& from, const char* a, const char* b, const char* c)
		{
			const part_t* p1 = body.find(a);
			if (!p1)
				return;
			const part_t* p2 = body.find(b);
			const part_t* p3 = body.find(c);
			bone(from, p1->top());
			const math::vector3 joint2 = p2 ? p2->top() : p1->bottom();
			bone(p1->top(), joint2);
			if (!p2)
				return;
			const math::vector3 joint3 = p3 ? p3->top() : p2->bottom();
			bone(joint2, joint3);
			if (p3)
				bone(joint3, p3->bottom());
		};
		limb(neck, "LeftUpperArm", "LeftLowerArm", "LeftHand");
		limb(neck, "RightUpperArm", "RightLowerArm", "RightHand");
		limb(pelvis, "LeftUpperLeg", "LeftLowerLeg", "LeftFoot");
		limb(pelvis, "RightUpperLeg", "RightLowerLeg", "RightFoot");
	}
	else if (body.kind == kind_t::r6)
	{
		const part_t* head = body.find("Head");
		const part_t* torso = body.find("Torso");
		if (!torso)
			return;
		const math::vector3 neck = torso->top();
		const math::vector3 pelvis = torso->bottom();
		if (head)
			bone(head->pos, neck);
		bone(neck, pelvis);

		// R6 limbs hang from the torso's top (arms) and bottom (legs); their joints sit half a
		// stud below the part's top, where Roblox puts the shoulder and hip.
		auto limb = [&](const math::vector3& from, const char* name)
		{
			const part_t* p = body.find(name);
			if (!p)
				return;
			const math::vector3 joint = p->top() - p->up() * 0.5f;
			bone(from, joint);
			bone(joint, p->bottom());
		};
		limb(neck, "Left Arm");
		limb(neck, "Right Arm");
		limb(pelvis, "Left Leg");
		limb(pelvis, "Right Leg");
	}
}
