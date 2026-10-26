#pragma once
#include "../../../ext/json/json.hpp"
#include <string>

// The editor's Explorer: scripts in the natware folder's workspace\ and autoexec\ (paths.h).
// The page sends "files:*" requests; each gets one reply of the same type.
//   files:list                       both folders' .lua/.luau/.txt files
//   files:read     group, rel        one file's text
//   files:write    group, rel, source
//   files:create   group, rel        a new empty file (fails if it exists)
//   files:rename   group, rel, to    renames a file inside its folder (a renamed tab)
//   files:reveal   group             opens the folder in Explorer
//   files:autoexec                   every autoexec script's name and text, run after an attach
// `group` is "workspace" or "autoexec"; `rel` is a path inside it with / separators.
namespace editor_files {
nlohmann::json handle(const std::string& type, const nlohmann::json& request);
}
