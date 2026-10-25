#pragma once
#include <Windows.h>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

// One entry per attached target. Each target gets its own nexec.exe backend on its own port.
struct ClientInfo {
    unsigned long pid = 0;              // target process PID
    unsigned short port = 0;            // backend port (6969, 6970, 6971, ...)
    HANDLE backend_process = nullptr;   // nexec.exe process handle (owned by the manager)
    HANDLE target_process = nullptr;    // SYNCHRONIZE handle on the target; null when it could not be opened
    bool connected = false;             // bridge available on this port
    bool attaching = false;             // launched, port not open yet
    std::string label;                  // display name ("Client <pid>")
    std::string error;                  // last attach problem, empty when fine
    ULONGLONG launched_at = 0;          // GetTickCount64() when the backend started
};

// Thread-safe. The lock is never held across a socket call, so a slow backend cannot stall
// attach/detach/clients() on other threads.
class ClientManager {
public:
    ClientManager();
    ~ClientManager();
    ClientManager(const ClientManager&) = delete;
    ClientManager& operator=(const ClientManager&) = delete;

    // Scan for all running target instances, return their PIDs.
    std::vector<unsigned long> discover() const;

    // Attach to a specific PID on the next free port. Launches the backend and returns at once;
    // poll() notices when its port opens. port_out receives the assigned port.
    bool attach(unsigned long pid, std::string& error, unsigned short* port_out = nullptr);

    // Kill the client's backend and forget it.
    void detach(unsigned long pid);

    // Copy of all managed clients. The handles stay owned by the manager.
    std::vector<ClientInfo> clients() const;
    bool find(unsigned long pid, ClientInfo& out) const;

    // Probe every client's port, drop clients whose target or backend exited, and time out
    // attaches that never opened their port. Returns true when anything visible changed.
    bool poll();

    // Removals poll() made since the last call: (pid, reason).
    std::vector<std::pair<unsigned long, std::string>> take_removed();

    // Probe one client's port until it opens or timeout_ms passes. Used by the legacy attach path.
    bool wait_connected(unsigned long pid, int timeout_ms);

    // Execute on one client.
    bool execute(unsigned long pid, const std::string& source, std::string& error);

    // Execute on the given clients in parallel. Each result is (pid, error); an empty error means
    // the backend accepted the script.
    std::vector<std::pair<unsigned long, std::string>> execute_many(const std::vector<unsigned long>& pids, const std::string& source);

    // Execute on all connected clients.
    std::vector<std::pair<unsigned long, std::string>> execute_all(const std::string& source);

    // The client the legacy nexec:* messages act on: the last selected or attached one, or the
    // first managed client when that one is gone. 0 when nothing is managed.
    unsigned long primary() const;
    bool select(unsigned long pid);

    static constexpr unsigned short kFirstPort = 6969;
    static constexpr int kAttachTimeoutMs = 60000;   // cold start: Roblox attach + Luau bootstrap

private:
    unsigned short free_port_locked() const;
    void release_locked(ClientInfo& client, bool kill_backend);

    mutable std::mutex mutex_;
    std::vector<ClientInfo> clients_;
    std::vector<std::pair<unsigned long, std::string>> removed_;
    unsigned short next_port_ = kFirstPort;   // lowest port that may be free; ports are recycled
    unsigned long primary_ = 0;
    HANDLE job_ = nullptr;                    // kills every backend when this process exits
};

// The process-wide manager. Clients outlive the editor window, like the single backend did.
ClientManager& client_manager();
