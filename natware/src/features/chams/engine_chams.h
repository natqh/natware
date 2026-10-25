#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

namespace engine_chams
{
    enum class Status
    {
        Disabled,
        UnsupportedVersion,
        WaitingForGame,
        WaitingForCharacters,
        NoAvailableIds,
        MemoryUnavailable,
        Active
    };

    struct Settings
    {
        bool enabled = false;
        bool through_walls = true;
        std::array<float, 4> fill = { 0.32f, 0.65f, 1.0f, 0.4f };
        std::array<float, 4> outline = { 0.75f, 0.88f, 1.0f, 1.0f };
    };

    void initialize();
    void shutdown();
    void update(const Settings& settings);

    Status      get_status();
    const char* describe_status(Status s);
    std::size_t get_entity_count();
    std::size_t get_player_count();

    inline std::atomic<bool> running{ false };
    void thread_func();
}
