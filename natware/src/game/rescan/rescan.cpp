#include <chrono>
#include <thread>
#include <windows.h>
#include <iostream>
#include <cstdlib>
#include <atomic>
#include <mutex>
#include <memory/memory.h>
#include <settings.h>
#include <gamesupport/gamesupport.h>
#include <sdk/sdk.h>
#include <Offsets/Offsets.hpp>
#include <game/game.h>

namespace AutoRescan {
    std::atomic<bool> active{ true };
    std::mutex updateMutex;
    std::atomic<bool> needsRescan{ false };
    std::atomic<bool> forceRescan{ false };
    std::atomic<uint64_t> lastGameId{ 0 };
    std::atomic<uint64_t> lastPlaceId{ 0 };
    std::atomic<uintptr_t> lastWorkspace{ 0 };
    std::atomic<uintptr_t> lastPlayers{ 0 };
    std::atomic<uintptr_t> lastDataModel{ 0 };
    std::atomic<uintptr_t> lastLocalPlayer{ 0 };
    std::atomic<DWORD> lastProcessId{ 0 };

    constexpr int LOBBY_SCAN_MS = 800;
    constexpr int LOADING_SCAN_MS = 300;
    constexpr int INGAME_SCAN_MS = 600;
    constexpr int TRANSITION_SCAN_MS = 200;
    constexpr int ERROR_RECOVERY_MS = 1500;
    constexpr int MEMORY_CHECK_MS = 3000;
    constexpr int PROCESS_CHECK_MS = 2000;

    std::atomic<int> consecutiveSuccesses{ 0 };
    std::atomic<int> consecutiveFailures{ 0 };
    std::atomic<bool> inTransition{ false };
    std::atomic<bool> memoryHealthy{ true };
    std::atomic<std::chrono::steady_clock::time_point> lastStateChange;
    std::atomic<std::chrono::steady_clock::time_point> lastMemoryCheck;
}
struct GameState {
    uint64_t gameId = 0;
    uint64_t placeId = 0;
    uintptr_t workspace = 0;
    uintptr_t players = 0;
    uintptr_t datamodel = 0;
    uintptr_t localplayer = 0;
    DWORD processId = 0;

    enum class StateType {
        INVALID,
        LOBBY_EMPTY,
        LOBBY_SEARCHING,
        LOADING,
        IN_GAME,
        DISCONNECTING
    };

    StateType GetStateType() const {
        if (processId == 0 || datamodel == 0) return StateType::INVALID;

        if (gameId == 0 && placeId == 0) return StateType::LOBBY_EMPTY;

        if (gameId == 0 || placeId <= 1000) return StateType::LOBBY_SEARCHING;

        if (workspace == 0 || players == 0) return StateType::LOADING;

        if (gameId != 0 && placeId > 1000 && (workspace == 0 || localplayer == 0)) {
            return StateType::DISCONNECTING;
        }

        return StateType::IN_GAME;
    }

    bool IsValid() const {
        return GetStateType() != StateType::INVALID;
    }

    bool HasChanged(const GameState& other) const {
        return (gameId != other.gameId ||
            placeId != other.placeId ||
            workspace != other.workspace ||
            players != other.players ||
            datamodel != other.datamodel ||
            localplayer != other.localplayer ||
            processId != other.processId);
    }

    bool HasCriticalChange(const GameState& other) const {
        return (gameId != other.gameId ||
            placeId != other.placeId ||
            processId != other.processId ||
            datamodel != other.datamodel);
    }

    bool RequiresImmediateRescan(const GameState& other) const {
        return (processId != other.processId ||
            (datamodel == 0 && other.datamodel != 0) ||
            (datamodel != 0 && other.datamodel == 0) ||
            (gameId == 0 && other.gameId != 0) ||
            (gameId != 0 && other.gameId == 0));
    }
};
static rbx::instance_t g_staticInstance;
static std::mutex g_instanceMutex;
struct SmartCache {
    GameState cachedState;
    std::chrono::steady_clock::time_point lastUpdate;
    std::chrono::steady_clock::time_point lastFullScan;
    std::mutex cacheMutex;
    bool isValid = false;
    int cacheHits = 0;
    int totalQueries = 0;

    std::chrono::milliseconds getCacheValidityDuration(GameState::StateType stateType) const {
        switch (stateType) {
        case GameState::StateType::LOBBY_EMPTY:
            return std::chrono::milliseconds(1500);
        case GameState::StateType::LOBBY_SEARCHING:
            return std::chrono::milliseconds(800);
        case GameState::StateType::LOADING:
            return std::chrono::milliseconds(300);
        case GameState::StateType::IN_GAME:
            return std::chrono::milliseconds(1200);
        case GameState::StateType::DISCONNECTING:
            return std::chrono::milliseconds(200);
        default:
            return std::chrono::milliseconds(400);
        }
    }

    float getCacheHitRate() const {
        return totalQueries > 0 ? (float)cacheHits / totalQueries : 0.0f;
    }
} g_smartCache;
bool QuickMemoryCheck() {
    auto now = std::chrono::steady_clock::now();
    auto lastCheck = AutoRescan::lastMemoryCheck.load();
    auto timeSinceCheck = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastCheck);

    if (timeSinceCheck < std::chrono::milliseconds(AutoRescan::MEMORY_CHECK_MS)) {
        return AutoRescan::memoryHealthy.load();
    }

    if (!memory->get_process_handle() || memory->get_process_handle() == INVALID_HANDLE_VALUE) {
        AutoRescan::memoryHealthy = false;
        return false;
    }

    DWORD testValue;
    SIZE_T bytesRead;
    bool healthy = (ReadProcessMemory(memory->get_process_handle(), (LPCVOID)memory->get_module_address(), &testValue, sizeof(DWORD), &bytesRead) != FALSE);

    AutoRescan::memoryHealthy = healthy;
    AutoRescan::lastMemoryCheck = now;

    return healthy;
}
GameState CaptureCurrentState() {
    auto now = std::chrono::steady_clock::now();

    {
        std::lock_guard<std::mutex> lock(g_smartCache.cacheMutex);
        g_smartCache.totalQueries++;

        if (g_smartCache.isValid) {
            auto cacheValidity = g_smartCache.getCacheValidityDuration(g_smartCache.cachedState.GetStateType());
            auto timeSinceUpdate = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_smartCache.lastUpdate);

            if (timeSinceUpdate < cacheValidity && AutoRescan::memoryHealthy.load()) {
                g_smartCache.cacheHits++;
                return g_smartCache.cachedState;
            }
        }
    }

    GameState state;

    try {
        if (!QuickMemoryCheck()) {
            std::lock_guard<std::mutex> lock(g_smartCache.cacheMutex);
            g_smartCache.isValid = false;
            return state;
        }

        state.processId = memory->get_process_id();

        std::lock_guard<std::mutex> instanceLock(g_instanceMutex);

        bool fullScan = false;
        {
            std::lock_guard<std::mutex> lock(g_smartCache.cacheMutex);
            auto timeSinceFullScan = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_smartCache.lastFullScan);
            fullScan = (timeSinceFullScan > std::chrono::milliseconds(5000)) || !g_smartCache.isValid;
        }

        std::uint64_t fake_datamodel = memory->read<std::uint64_t>(memory->get_module_address() + Offsets::FakeDataModel::Pointer);
        rbx::instance_t datamodel = rbx::instance_t(memory->read<std::uint64_t>(fake_datamodel + Offsets::FakeDataModel::RealDataModel));

        game::datamodel = datamodel;
        state.datamodel = datamodel.address;

        if (datamodel.address) {
            state.gameId = memory->read<uint64_t>(datamodel.address + Offsets::DataModel::GameId);
            state.placeId = memory->read<uint64_t>(datamodel.address + Offsets::DataModel::PlaceId);
        }

        if (fullScan || state.gameId != 0 || state.placeId != 0) {
            rbx::instance_t workspace = datamodel.find_first_child_by_class("Workspace");
            rbx::instance_t players = datamodel.find_first_child_by_class("Players");

            state.workspace = workspace.address;
            state.players = players.address;

            if (players.address) {
                rbx::instance_t localPlayer = memory->read<rbx::instance_t>(players.address + Offsets::Player::LocalPlayer);
                state.localplayer = localPlayer.address;
            }

            std::lock_guard<std::mutex> lock(g_smartCache.cacheMutex);
            g_smartCache.lastFullScan = now;
        }
        else {
            std::lock_guard<std::mutex> lock(g_smartCache.cacheMutex);
            if (g_smartCache.isValid) {
                state.workspace = g_smartCache.cachedState.workspace;
                state.players = g_smartCache.cachedState.players;
                state.localplayer = g_smartCache.cachedState.localplayer;
            }
        }

        {
            std::lock_guard<std::mutex> lock(g_smartCache.cacheMutex);
            g_smartCache.cachedState = state;
            g_smartCache.lastUpdate = now;
            g_smartCache.isValid = true;
        }

    }
    catch (...) {
        std::lock_guard<std::mutex> lock(g_smartCache.cacheMutex);
        g_smartCache.isValid = false;
        AutoRescan::consecutiveFailures++;
    }

    return state;
}
bool PerformSmartRescan(GameState::StateType expectedState) {
    try {
        static uint64_t last_datamodel = 0;
        static uint64_t last_visengine = 0;
        static uint64_t last_workspace = 0;
        static uint64_t last_players = 0;
        static uint64_t last_gameid = 0;

        static auto lastProcessCheck = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        auto timeSinceProcessCheck = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastProcessCheck);

        if (timeSinceProcessCheck > std::chrono::milliseconds(AutoRescan::PROCESS_CHECK_MS)) {
            DWORD currentPID = memory->find_process_id("RobloxPlayerBeta.exe");
            if (currentPID != memory->get_process_id() || currentPID == 0) {
                if (currentPID == 0) {
                    return false;
                }

                if (!memory->attach_to_process("RobloxPlayerBeta.exe")) {
                    return false;
                }
            }
            lastProcessCheck = now;
        }

        if (!QuickMemoryCheck()) {
            return false;
        }

        std::lock_guard<std::mutex> instanceLock(g_instanceMutex);

        std::uint64_t fake_datamodel = memory->read<std::uint64_t>(memory->get_module_address() + Offsets::FakeDataModel::Pointer);
        rbx::instance_t datamodel = rbx::instance_t(memory->read<std::uint64_t>(fake_datamodel + Offsets::FakeDataModel::RealDataModel));

        game::datamodel = datamodel;

        if (!datamodel.address) {
            return false;
        }

        uint64_t gameid = memory->read<uint64_t>(datamodel.address + Offsets::DataModel::GameId);
        uint64_t placeid = memory->read<uint64_t>(datamodel.address + Offsets::DataModel::PlaceId);

        bool needWorkspaceAndPlayers = (expectedState == GameState::StateType::IN_GAME ||
            expectedState == GameState::StateType::LOADING ||
            gameid != 0 || placeid != 0);

        if (needWorkspaceAndPlayers) {
            game::workspace = datamodel.find_first_child_by_class("Workspace");
            game::players = datamodel.find_first_child_by_class("Players");

            if (game::players.address) {
                game::local_player = memory->read<rbx::instance_t>(game::players.address + Offsets::Player::LocalPlayer);
                if (game::local_player.address) {
                    rbx::player_t local_player_obj = { game::local_player.address };
                    game::local_character = { local_player_obj.get_model_instance().address };
                }
            }
        }

        game::visengine = { memory->read<std::uint64_t>(memory->get_module_address() + Offsets::VisualEngine::Pointer) };

        bool hasChanges = false;
        if (last_datamodel != game::datamodel.address) {
            hasChanges = true;
        }
        if (last_visengine != game::visengine.address) {
            hasChanges = true;
        }
        if (last_workspace != game::workspace.address) {
            hasChanges = true;
        }
        if (last_players != game::players.address) {
            hasChanges = true;
        }
        if (last_gameid != gameid) {
            hasChanges = true;
        }

        if (hasChanges) {
            system("cls");
            printf("base address: 0x%llx\n", memory->get_module_address());
            printf("datamodel: 0x%llx\n", game::datamodel.address);
            printf("visualengine: 0x%llx\n", game::visengine.address);
            printf("workspace: 0x%llx\n", game::workspace.address);
            printf("players: 0x%llx\n", game::players.address);
            printf("gameid: %llu\n", gameid);

            if (last_gameid != gameid) {
                gamesupport::log_support_status(gamesupport::detect(gameid, placeid));
            }

            if (!settings::globals::offset_validation_result.empty()) {
                printf("%s", settings::globals::offset_validation_result.c_str());
            }
        }

        last_datamodel = game::datamodel.address;
        last_visengine = game::visengine.address;
        last_workspace = game::workspace.address;
        last_players = game::players.address;
        last_gameid = gameid;

        AutoRescan::consecutiveSuccesses++;
        AutoRescan::consecutiveFailures = 0;
        return true;

    }
    catch (...) {
        AutoRescan::consecutiveFailures++;
        AutoRescan::consecutiveSuccesses = 0;
        return false;
    }
}
void SmartChangeDetectionLoop() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

    GameState previousState;
    GameState::StateType previousStateType = GameState::StateType::INVALID;
    auto lastRescanTime = std::chrono::steady_clock::now();

    auto getSmartDelay = [&](GameState::StateType currentType, bool hasChanges, bool isTransition) -> int {
        int failures = AutoRescan::consecutiveFailures.load();
        if (failures > 3) return AutoRescan::ERROR_RECOVERY_MS;
        if (failures > 0) return 1000 + (failures * 200);

        int successes = AutoRescan::consecutiveSuccesses.load();
        float successBonus = min(0.3f, successes * 0.05f);

        if (isTransition || hasChanges) {
            return (int)(AutoRescan::TRANSITION_SCAN_MS * (1.0f - successBonus));
        }

        int baseDelay;
        switch (currentType) {
        case GameState::StateType::LOBBY_EMPTY:
            baseDelay = AutoRescan::LOBBY_SCAN_MS;
            break;
        case GameState::StateType::LOBBY_SEARCHING:
            baseDelay = AutoRescan::LOBBY_SCAN_MS - 200;
            break;
        case GameState::StateType::LOADING:
            baseDelay = AutoRescan::LOADING_SCAN_MS;
            break;
        case GameState::StateType::IN_GAME:
            baseDelay = AutoRescan::INGAME_SCAN_MS;
            break;
        case GameState::StateType::DISCONNECTING:
            baseDelay = AutoRescan::LOADING_SCAN_MS;
            break;
        default:
            baseDelay = 600;
        }

        return (int)(baseDelay * (1.0f - successBonus));
        };

    while (AutoRescan::active) {
        try {
            GameState currentState = CaptureCurrentState();
            GameState::StateType currentStateType = currentState.GetStateType();

            bool hasAnyChange = currentState.HasChanged(previousState);
            bool hasCriticalChange = currentState.HasCriticalChange(previousState);
            bool requiresImmediate = currentState.RequiresImmediateRescan(previousState);
            bool stateTypeChanged = (currentStateType != previousStateType);
            bool forceRescan = AutoRescan::needsRescan.exchange(false) || AutoRescan::forceRescan.exchange(false);

            bool isTransition = stateTypeChanged || requiresImmediate;
            if (isTransition) {
                AutoRescan::inTransition = true;
                AutoRescan::lastStateChange = std::chrono::steady_clock::now();
            }
            else {
                auto now = std::chrono::steady_clock::now();
                auto timeSinceChange = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - AutoRescan::lastStateChange.load());
                if (timeSinceChange > std::chrono::milliseconds(2000)) {
                    AutoRescan::inTransition = false;
                }
            }

            if (hasAnyChange || stateTypeChanged || forceRescan || requiresImmediate) {
                auto now = std::chrono::steady_clock::now();
                auto timeSinceLastRescan = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastRescanTime);

                int requiredCooldown = 100;

                if (requiresImmediate || forceRescan) {
                    requiredCooldown = 50;
                }
                else if (hasCriticalChange) {
                    requiredCooldown = 200;
                }
                else if (stateTypeChanged) {
                    requiredCooldown = 300;
                }
                else {
                    requiredCooldown = 500;
                }

                if (timeSinceLastRescan.count() >= requiredCooldown || forceRescan || requiresImmediate) {
                    std::lock_guard<std::mutex> lock(AutoRescan::updateMutex);

                    bool rescanSuccess = false;
                    int maxRetries = requiresImmediate ? 3 : 2;

                    for (int retry = 0; retry < maxRetries && !rescanSuccess; retry++) {
                        if (retry > 0) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(200 * retry));
                        }
                        rescanSuccess = PerformSmartRescan(currentStateType);
                    }

                    if (rescanSuccess) {
                        std::lock_guard<std::mutex> cacheLock(g_smartCache.cacheMutex);
                        g_smartCache.isValid = false;
                        previousState = CaptureCurrentState();
                        previousStateType = previousState.GetStateType();
                        lastRescanTime = now;

                    }
                }
            }
            else {
                if (currentState.IsValid()) {
                    previousState = currentState;
                    previousStateType = currentStateType;

                }
            }

        }
        catch (...) {
            AutoRescan::consecutiveFailures++;
        }

        GameState::StateType currentType = previousState.GetStateType();
        bool hasChanges = (AutoRescan::consecutiveFailures.load() == 0);
        bool isTransition = AutoRescan::inTransition.load();
        int delayMs = getSmartDelay(currentType, hasChanges, isTransition);

        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
    }
}
void GameTransitionMonitor() {
    GameState::StateType lastKnownState = GameState::StateType::INVALID;
    auto stateChangeTime = std::chrono::steady_clock::now();

    while (AutoRescan::active) {
        try {
            GameState currentState = CaptureCurrentState();
            GameState::StateType currentStateType = currentState.GetStateType();

            if (currentStateType != lastKnownState) {
                stateChangeTime = std::chrono::steady_clock::now();
                lastKnownState = currentStateType;

                if (currentStateType == GameState::StateType::LOADING ||
                    currentStateType == GameState::StateType::LOBBY_EMPTY ||
                    currentStateType == GameState::StateType::DISCONNECTING) {
                    AutoRescan::forceRescan = true;
                }
            }

            auto now = std::chrono::steady_clock::now();
            auto timeInState = std::chrono::duration_cast<std::chrono::milliseconds>(now - stateChangeTime);

            switch (currentStateType) {
            case GameState::StateType::LOADING:
                if (timeInState.count() > 12000) {
                    AutoRescan::forceRescan = true;
                    stateChangeTime = now;
                }
                break;

            case GameState::StateType::LOBBY_EMPTY:
                if (timeInState.count() > 5000) {
                    AutoRescan::needsRescan = true;
                    stateChangeTime = now;
                }
                break;

            case GameState::StateType::DISCONNECTING:
                if (timeInState.count() > 8000) {
                    AutoRescan::forceRescan = true;
                    stateChangeTime = now;
                }
                break;
            }

        }
        catch (...) {
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(800));
    }
}
float GetCacheEfficiency() {
    std::lock_guard<std::mutex> lock(g_smartCache.cacheMutex);
    return g_smartCache.getCacheHitRate();
}
void AutoRescanHandler() {
    GameState initialState = CaptureCurrentState();
    AutoRescan::lastGameId = initialState.gameId;
    AutoRescan::lastPlaceId = initialState.placeId;
    AutoRescan::lastStateChange = std::chrono::steady_clock::now();
    AutoRescan::lastMemoryCheck = std::chrono::steady_clock::now();

    std::thread(SmartChangeDetectionLoop).detach();
    std::thread(GameTransitionMonitor).detach();

    while (AutoRescan::active) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));

        static int debugCounter = 0;
        if (++debugCounter % 30 == 0) {
            float efficiency = GetCacheEfficiency();
            int failures = AutoRescan::consecutiveFailures.load();
            int successes = AutoRescan::consecutiveSuccesses.load();
        }
    }
}
bool InitializeStorage() {
    return PerformSmartRescan(GameState::StateType::LOBBY_EMPTY);
}
