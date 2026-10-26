#pragma once
#include <string>
#include <vector>
#include "nexec_bridge.h"

// Every running Roblox client, with the nexec backend that serves it (if any). Each client gets its
// own nexec.exe on its own port, so several clients can be attached and driven at once.
namespace client_manager {

struct Client {
    unsigned long pid = 0;          // the Roblox process
    bool launching = false;         // a backend was started for it and has not answered yet
    std::string error;              // last launch problem for this client, empty when fine
    nexec_bridge::Status status;    // status.up is false while no backend serves this client
};

// All running Roblox clients, freshest state first-hand from the backends. Safe from any thread.
std::vector<Client> refresh();

// Starts a backend for the client when it has none. Returns at once; poll refresh() until the
// client reports attached. False (with `error`) when the backend could not be started.
bool attach(unsigned long pid, std::string& error);

// Stops the backend serving the client.
bool detach(unsigned long pid, std::string& error);

// The client the toolbar's Execute and Attach act on.
unsigned long selected();
void select(unsigned long pid);

// The pid to act on: the selected client if it still runs, else the first attached one, else the
// first one. 0 when no Roblox is running.
unsigned long resolve_primary(const std::vector<Client>& clients);

// True when the client's backend reports the injected script alive in its current game.
bool is_attached(unsigned long pid);

}
