#pragma once
#include <cstdint>
#include <atomic>
#include <mutex>
#include <chrono>

namespace AutoRescan {
    extern std::atomic<bool> active;
    extern std::mutex updateMutex;
    extern std::atomic<bool> needsRescan;
    extern std::atomic<bool> forceRescan;
    extern std::atomic<uint64_t> lastGameId;
    extern std::atomic<uint64_t> lastPlaceId;
    extern std::atomic<uintptr_t> lastWorkspace;
    extern std::atomic<uintptr_t> lastPlayers;
    extern std::atomic<uintptr_t> lastDataModel;
    extern std::atomic<uintptr_t> lastLocalPlayer;
    extern std::atomic<DWORD> lastProcessId;
    extern std::atomic<int> consecutiveSuccesses;
    extern std::atomic<int> consecutiveFailures;
    extern std::atomic<bool> inTransition;
    extern std::atomic<bool> memoryHealthy;
    extern std::atomic<std::chrono::steady_clock::time_point> lastStateChange;
    extern std::atomic<std::chrono::steady_clock::time_point> lastMemoryCheck;
}

bool InitializeStorage();
float GetCacheEfficiency();
void AutoRescanHandler();
