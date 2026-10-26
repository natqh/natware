#pragma once
#include <string>

// Talks to the nexec backends over their local HTTP bridge. One backend runs per Roblox client,
// each on its own port (kFirstPort, kFirstPort + 1, ...).
namespace nexec_bridge {

constexpr unsigned short kFirstPort = 6969;
constexpr unsigned short kPortCount = 16;

// What a backend reports on GET /status.
struct Status {
    bool up = false;            // the port answered
    bool attached = false;      // the injected script is alive in the client's current game
    bool inGame = false;        // the client is in a game (not on the home screen)
    bool injectFailed = false;  // the backend gave up injecting into the current game
    unsigned long pid = 0;      // the Roblox process this backend serves
    unsigned long backendPid = 0;
    unsigned short port = 0;
    std::string name, userId, display, avatar, placeId, jobId;
};

// False when nothing answers on the port.
bool status(unsigned short port, Status& out, int timeout_ms = 400);

// Asks the backend to try injecting into the client's current game again.
bool reinject(unsigned short port, unsigned long pid);

// Sends the script to the backend serving `pid`. The backend refuses it unless the client is
// attached, so a script is never queued for a game the client has already left.
bool execute(unsigned short port, unsigned long pid, const std::string& source, std::string& error);

}
