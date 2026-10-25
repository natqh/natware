#include <sdk/sdk.h>
#include <Offsets/Offsets.hpp>
#include <memory/memory.h>
#include <game/game.h>
#include <xmmintrin.h>

std::string rbx::nameable_t::get_name() const
{
	std::uint64_t name_container = memory->read<std::uint64_t>(this->address + Offsets::Instance::NameContainer);
	if (name_container == 0)
		return "unknown";

	return memory->read_string(name_container + Offsets::Instance::Name);
}

	



std::string rbx::nameable_t::get_class_name() const
{
	std::uint64_t class_descriptor = memory->read<std::uint64_t>(this->address + Offsets::Instance::ClassDescriptor);
	std::uint64_t class_name = memory->read<std::uint64_t>(class_descriptor + Offsets::Instance::ClassName);

	if (class_name)
	{
		return memory->read_string(class_name);
	}

	return "unknown";
}

std::vector<rbx::instance_t> rbx::interface_t::get_children() const
{
	const rbx::instance_t* base = static_cast<const rbx::instance_t*>(this);
	std::vector<rbx::instance_t> children;

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

rbx::instance_t rbx::interface_t::find_first_child(std::string_view str) const
{
	std::vector<rbx::instance_t> children = this->get_children();

	for (const rbx::instance_t& child : children)
	{
		if (child.get_name() == str)
		{
			return child;
		}
	}

	return {};
}

rbx::instance_t rbx::interface_t::find_first_child_by_class(std::string_view str) const
{
	std::vector<rbx::instance_t> children = this->get_children();

	for (const rbx::instance_t& child : children)
	{
		if (child.get_class_name() == str)
		{
			return child;
		}
	}

	return {};
}

rbx::model_instance_t rbx::player_t::get_model_instance()
{
	return { memory->read<std::uint64_t>(this->address + Offsets::Player::ModelInstance) };
}

std::string rbx::player_t::get_display_name()
{
	return memory->read_string(this->address + Offsets::Player::DisplayName);
}

std::uint8_t rbx::humanoid_t::get_rig_type()
{
	return { memory->read<std::uint8_t>(this->address + Offsets::Humanoid::RigType) };
}

std::uint16_t rbx::humanoid_t::get_state()
{
	auto humanoid_state{ memory->read<std::uint64_t>(this->address + Offsets::Humanoid::HumanoidState) };
	return { memory->read<std::uint16_t>(humanoid_state + Offsets::Humanoid::HumanoidStateID) };
}

void rbx::humanoid_t::write_gravity(float gravity)
{
	if (game::workspace.address == 0)
		return;

	uint64_t world = memory->read<uint64_t>(game::workspace.address + Offsets::Workspace::World);
	if (world == 0 || world == 0xFFFFFFFFFFFFFFFF)
		return;

	memory->write<float>(world + Offsets::World::Gravity, gravity);
}

float rbx::humanoid_t::read_gravity()
{
	if (game::workspace.address == 0)
		return 0.0f;

	uint64_t world = memory->read<uint64_t>(game::workspace.address + Offsets::Workspace::World);
	if (world == 0 || world == 0xFFFFFFFFFFFFFFFF)
		return 0.0f;

	return memory->read<float>(world + Offsets::World::Gravity);
}

void rbx::humanoid_t::write_tickrate(float tickrate)
{
	uint64_t world = memory->read<uint64_t>(game::workspace.address + Offsets::Workspace::World);
	memory->write<float>(world + Offsets::World::worldStepsPerSec, tickrate);
}

rbx::primitive_t rbx::part_t::get_primitive() const
{
	return { memory->read<std::uint64_t>(this->address + Offsets::BasePart::Primitive) };
}

math::vector3 rbx::primitive_t::get_size()
{
	return memory->read<math::vector3>(this->address + Offsets::Primitive::Size);
}

math::vector3 rbx::primitive_t::get_position()
{
	return memory->read<math::vector3>(this->address + Offsets::Primitive::Position);
}

math::matrix3 rbx::primitive_t::get_rotation()
{
	return memory->read<math::matrix3>(this->address + Offsets::Primitive::Rotation);
}

math::vector2 rbx::visualengine_t::get_dimensions()
{
	return memory->read<math::vector2>(this->address + Offsets::VisualEngine::Dimensions);
}

math::matrix4 rbx::visualengine_t::get_viewmatrix()
{
	return memory->read<math::matrix4>(this->address + Offsets::VisualEngine::ViewMatrix);
}

bool rbx::visualengine_t::world_to_screen(const math::vector3& world, math::vector2& out, const math::vector2& dims, const math::matrix4& view)
{
	math::vector4 clip = view.multiply({ world.x, world.y, world.z, 1.f });

	if (clip.w < 0.1f)
	{
		return false;
	}

	clip.x /= clip.w;
	clip.y /= clip.w;

	out.x = (dims.x * 0.5f * clip.x) + (dims.x * 0.5f);
	out.y = -(dims.y * 0.5f * clip.y) + (dims.y * 0.5f);

	out.x += game::window_offset_x;
	out.y += game::window_offset_y;

	return true;
}

void rbx::camera_t::set_viewport(const rbx::vector2int16& val)
{
	if (this->address == 0)
		return;

	memory->write<rbx::vector2int16>(this->address + Offsets::Camera::Viewport, val);
}


void rbx::instance_t::set_instance_double(double v)
{
	memory->write<double>(this->address + Offsets::Misc::Value, v);
}

std::uint64_t rbx::c_silent_help::cached_input_object = 0;

void rbx::c_silent_help::initialize_mouse_service(std::uint64_t address)
{
	cached_input_object = memory->read<std::uint64_t>(address + Offsets::MouseService::InputObject);
	if (cached_input_object == 0 || cached_input_object == 0xFFFFFFFFFFFFFFFF)
		cached_input_object = memory->read<std::uint64_t>(address + Offsets::MouseService::InputObject2);
}

void rbx::c_silent_help::write_mouse_position(std::uint64_t address, float x, float y)
{
	math::vector2 new_position = { x, y };

	std::uint64_t obj1 = memory->read<std::uint64_t>(address + Offsets::MouseService::InputObject);
	if (obj1 != 0 && obj1 != 0xFFFFFFFFFFFFFFFF)
		memory->write<math::vector2>(obj1 + Offsets::MouseService::MousePosition, new_position);

	std::uint64_t obj2 = memory->read<std::uint64_t>(address + Offsets::MouseService::InputObject2);
	if (obj2 != 0 && obj2 != 0xFFFFFFFFFFFFFFFF && obj2 != obj1)
		memory->write<math::vector2>(obj2 + Offsets::MouseService::MousePosition, new_position);
}