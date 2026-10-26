#pragma once
#include <string>

namespace nexec_loader {

// Starts a backend for the primary Roblox client (the selected one, else the first). Returns at
// once; the client is attached when its backend reports it (see client_manager).
bool inject(std::string& error);

}
