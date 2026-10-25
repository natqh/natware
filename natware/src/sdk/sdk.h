#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <memory/memory.h>
#include <Offsets/Offsets.hpp>
#include "math/math.h"

namespace rbx
{
	struct vector2int16 final
	{
		int16_t x{ 0 };
		int16_t y{ 0 };
	};

	struct instance_t;
	struct primitive_t;
	struct model_instance_t;

	struct addressable_t
	{
		std::uint64_t address;

		addressable_t() : address(0) {}
		addressable_t(std::uint64_t address) : address(address) {}
	};

	struct nameable_t : public addressable_t
	{
		using addressable_t::addressable_t;

		std::string get_name() const;
		std::string get_class_name() const;
	};

	struct interface_t
	{
		template <typename T>
		std::vector<T> get_children() const;

		std::vector<rbx::instance_t> get_children() const;
		rbx::instance_t find_first_child(std::string_view str) const;
		rbx::instance_t find_first_child_by_class(std::string_view str) const;
	};

	struct instance_t : public nameable_t, public interface_t
	{
		using nameable_t::nameable_t;

		void set_instance_double(double v);
	};

	struct player_t final : public instance_t
	{
		using instance_t::instance_t;

		rbx::model_instance_t get_model_instance();
		std::string get_display_name();
	};

	struct model_instance_t final : public instance_t
	{
		using instance_t::instance_t;
	};

	struct humanoid_t final : public addressable_t
	{
		using addressable_t::addressable_t;

		std::uint8_t get_rig_type();
		std::uint16_t get_state();
		static void write_gravity(float gravity);
		static float read_gravity();
		static void write_tickrate(float tickrate);
	};

	struct part_t : public instance_t
	{
		using instance_t::instance_t;

		rbx::primitive_t get_primitive() const;
	};

	struct primitive_t final : public addressable_t
	{
		using addressable_t::addressable_t;

		math::vector3 get_size();
		math::vector3 get_position();
		math::matrix3 get_rotation();
	};

	struct visualengine_t final : public addressable_t
	{
		math::vector2 get_dimensions();
		math::matrix4 get_viewmatrix();
		bool world_to_screen(const math::vector3& world, math::vector2& out, const math::vector2& dims, const math::matrix4& view);
	};

	struct camera_t final : public addressable_t
	{
		using addressable_t::addressable_t;

		void set_viewport(const rbx::vector2int16& val);
	};

	struct c_silent_help final : public addressable_t
	{
		using addressable_t::addressable_t;

		static std::uint64_t cached_input_object;
		void initialize_mouse_service(std::uint64_t address);
		void write_mouse_position(std::uint64_t address, float x, float y);
	};
}

template <typename T>
std::vector<T> rbx::interface_t::get_children() const
{
	const rbx::instance_t* base = static_cast<const rbx::instance_t*>(this);
	std::vector<T> children;

	if (base->address == 0)
		return children;

	try
	{
		std::uint64_t children_struct = memory->read<std::uint64_t>(base->address + Offsets::Instance::ChildrenStart);
		if (children_struct == 0)
			return children;

		std::uint64_t start = memory->read<std::uint64_t>(children_struct);
		std::uint64_t end = memory->read<std::uint64_t>(children_struct + Offsets::Instance::ChildrenEnd);

		if (start == 0 || end == 0 || end <= start)
			return children;

		constexpr std::uint64_t kStep = 16;
		constexpr std::uint64_t kMaxChildren = 10000;
		constexpr std::uint64_t kMaxByteSize = kMaxChildren * kStep;

		std::uint64_t byte_size = end - start;
		if (byte_size > kMaxByteSize)
			return children;

		if (byte_size % kStep != 0)
			return children;

		std::uint64_t count = byte_size / kStep;
		if (count == 0)
			return children;

		children.reserve(static_cast<size_t>(count));

		for (std::uint64_t instance = start; instance < end; instance += kStep)
		{
			std::uint64_t child_address = memory->read<std::uint64_t>(instance);
			if (child_address != 0 && child_address != 0xFFFFFFFFFFFFFFFF)
			{
				children.emplace_back(child_address);
			}
		}
	}
	catch (...)
	{
		return children;
	}

	return children;
}