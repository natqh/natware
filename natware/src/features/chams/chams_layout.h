#pragma once
#include <cstdint>
#include <Offsets/Offsets.hpp>

namespace engine_chams::layout
{
    inline std::uint64_t children()          { return Offsets::Instance::ChildrenStart; }
    inline std::uint64_t class_descriptor()  { return Offsets::Instance::ClassDescriptor; }
    inline std::uint64_t class_name()        { return Offsets::Instance::ClassName; }
    inline std::uint64_t parent()            { return Offsets::Instance::Parent; }
    inline std::uint64_t local_player()      { return Offsets::Player::LocalPlayer; }
    inline std::uint64_t model_instance()    { return Offsets::Player::ModelInstance; }
    inline std::uint64_t visual_engine_ptr() { return Offsets::VisualEngine::Pointer; }
    inline std::uint64_t fake_dm_ptr()       { return Offsets::FakeDataModel::Pointer; }
    inline std::uint64_t fake_dm_to_real()   { return Offsets::FakeDataModel::RealDataModel; }

    inline constexpr std::uint64_t manager_offset          = 0xC00;
    inline constexpr std::uint64_t merge_flag              = 0x8698948;
    inline constexpr std::uint64_t cluster_node            = 0x180;
    inline constexpr std::uint64_t cluster_binding         = 0x90;
    inline constexpr std::uint64_t cluster_vtable          = 0x6D6FC88;
    inline constexpr std::uint64_t entity_vtable           = 0x6D70CE8;
    inline constexpr std::uint64_t cluster_engine          = 0x10;
    inline constexpr std::uint64_t cluster_entities        = 0x48;
    inline constexpr std::uint64_t cluster_humanoid        = 0xF8;
    inline constexpr std::uint64_t entity_cluster          = 0x8;
    inline constexpr std::uint64_t entity_geometry         = 0x20;
    inline constexpr std::uint64_t entity_techniques       = 0x70;
    inline constexpr std::uint64_t entity_indices          = 0x80;
    inline constexpr std::uint64_t entity_highlight        = 0xB4;
    inline constexpr std::uint64_t contexts                = 0x100;
    inline constexpr std::uint64_t context_stride          = 9504;
    inline constexpr std::uint64_t fallback_depth          = 312;
    inline constexpr std::uint64_t fallback_active         = 1336;
    inline constexpr std::uint64_t fallback_fill           = 1592;
    inline constexpr std::uint64_t fallback_outline        = 5688;
    inline constexpr std::uint64_t context_depth           = 32;
    inline constexpr std::uint64_t context_active          = 1056;
    inline constexpr std::uint64_t context_fill            = 1312;
    inline constexpr std::uint64_t context_outline         = 5408;
    inline constexpr std::uint64_t global_next             = 9896;
    inline constexpr std::uint64_t global_depth            = 9900;
    inline constexpr std::uint64_t global_active           = 10924;
    inline constexpr std::uint64_t global_fill             = 11180;
    inline constexpr std::uint64_t global_outline          = 15276;
}
