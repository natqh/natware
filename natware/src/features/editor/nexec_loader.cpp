#include "nexec_loader.h"
#include "client_manager.h"
#include "editor_diagnostics.h"
#include <string>
#include <vector>

namespace nexec_loader {

bool inject(std::string& error)
{
    error.clear();

    const auto clients = client_manager::refresh();
    const unsigned long pid = client_manager::resolve_primary(clients);
    if (pid == 0)
    {
        error = "Roblox is not running";
        return false;
    }

    editor_diagnostics::write("info", "loader.starting", {{"pid", static_cast<int>(pid)}});
    return client_manager::attach(pid, error);
}

}
