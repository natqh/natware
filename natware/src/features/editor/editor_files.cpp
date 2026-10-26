#include "editor_files.h"
#include "editor_diagnostics.h"
#include "../../paths.h"
#include <Windows.h>
#include <shellapi.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>
#pragma comment(lib, "shell32.lib")

namespace fs = std::filesystem;

namespace editor_files {
namespace {

constexpr std::uintmax_t kMaxBytes = 2 * 1024 * 1024;   // the execute limit
constexpr size_t kMaxFiles = 1000;

std::wstring widen(const std::string& s)
{
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string narrow(const std::wstring& w)
{
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring folder(const std::string& group)
{
    if (group == "workspace") return natware_paths::workspace();
    if (group == "autoexec") return natware_paths::autoexec();
    return {};
}

bool script_extension(const fs::path& p)
{
    std::wstring ext = p.extension().wstring();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
    return ext == L".lua" || ext == L".luau" || ext == L".txt";
}

// `rel` from the page, inside `group`'s folder; empty when it would leave it.
fs::path resolve(const std::string& group, const std::string& rel)
{
    const std::wstring base = folder(group);
    if (base.empty() || rel.empty() || rel.size() > 260) return {};
    std::wstring r = widen(rel);
    std::replace(r.begin(), r.end(), L'/', L'\\');
    if (r.front() == L'\\' || r.find(L':') != std::wstring::npos) return {};
    for (const fs::path& part : fs::path(r))
        if (part == L".." || part == L".") return {};
    return fs::path(base) / r;
}

// File text as UTF-8; text saved in the ANSI code page is converted, so the reply stays valid JSON.
std::string read_text(const fs::path& p, std::string& error)
{
    std::error_code ec;
    if (fs::file_size(p, ec) > kMaxBytes) { error = "The file is over 2 MB"; return {}; }
    std::ifstream in(p, std::ios::binary);
    if (!in) { error = "The file couldn't be opened"; return {}; }
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string text = ss.str();
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF)
        text.erase(0, 3);
    if (!text.empty() && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0) == 0)
    {
        const int n = MultiByteToWideChar(CP_ACP, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        std::wstring w(n, L'\0');
        MultiByteToWideChar(CP_ACP, 0, text.data(), static_cast<int>(text.size()), w.data(), n);
        text = narrow(w);
    }
    return text;
}

nlohmann::json list_group(const std::string& group)
{
    const std::wstring base = folder(group);
    nlohmann::json files = nlohmann::json::array();
    std::vector<std::pair<std::wstring, std::uintmax_t>> found;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(base, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator() && found.size() < kMaxFiles; it.increment(ec))
    {
        if (!it->is_regular_file(ec) || !script_extension(it->path())) continue;
        std::wstring rel = fs::relative(it->path(), base, ec).wstring();
        std::replace(rel.begin(), rel.end(), L'\\', L'/');
        found.emplace_back(rel, it->file_size(ec));
    }
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return _wcsicmp(a.first.c_str(), b.first.c_str()) < 0; });
    for (const auto& [rel, size] : found)
        files.push_back({{"rel", narrow(rel)}, {"name", narrow(fs::path(rel).filename().wstring())}, {"size", size}});
    return {{"id", group}, {"path", narrow(base)}, {"files", files}};
}

std::string field(const nlohmann::json& request, const char* key)
{
    return request.contains(key) && request[key].is_string() ? request[key].get<std::string>() : std::string();
}

} // namespace

nlohmann::json handle(const std::string& type, const nlohmann::json& request)
{
    nlohmann::json reply{{"type", type}, {"ok", false}};
    const std::string group = field(request, "group"), rel = field(request, "rel");
    std::error_code ec;

    if (type == "files:list")
    {
        reply["groups"] = nlohmann::json::array({list_group("workspace"), list_group("autoexec")});
        reply["ok"] = true;
    }
    else if (type == "files:read")
    {
        const fs::path p = resolve(group, rel);
        std::string error;
        if (p.empty() || !fs::is_regular_file(p, ec)) error = "That file isn't there anymore";
        const std::string text = error.empty() ? read_text(p, error) : std::string();
        reply.update({{"group", group}, {"rel", rel}, {"ok", error.empty()}, {"error", error}});
        if (error.empty()) reply["source"] = text;
    }
    else if (type == "files:write" || type == "files:create")
    {
        const fs::path p = resolve(group, rel);
        const std::string source = type == "files:write" ? field(request, "source") : std::string();
        std::string error;
        if (p.empty() || !script_extension(p)) error = "Scripts end in .lua, .luau or .txt";
        else if (source.size() > kMaxBytes) error = "The script is over 2 MB";
        else if (type == "files:create" && fs::exists(p, ec)) error = "A file with that name already exists";
        else
        {
            fs::create_directories(p.parent_path(), ec);
            // Written beside and moved over, so a failed write never leaves half a script.
            const fs::path temp = p.wstring() + L".tmp";
            {
                std::ofstream out(temp, std::ios::binary | std::ios::trunc);
                out.write(source.data(), static_cast<std::streamsize>(source.size()));
                if (!out) error = "The file couldn't be written";
            }
            if (error.empty() && !MoveFileExW(temp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING))
                error = "The file couldn't be written (error " + std::to_string(GetLastError()) + ")";
            if (!error.empty()) fs::remove(temp, ec);
        }
        reply.update({{"group", group}, {"rel", rel}, {"ok", error.empty()}, {"error", error}});
        editor_diagnostics::write(error.empty() ? "info" : "error", "files.write", {{"group", group}, {"bytes", source.size()}, {"error", error}});
    }
    else if (type == "files:rename")
    {
        const fs::path from = resolve(group, rel), to = resolve(group, field(request, "to"));
        std::string error;
        if (from.empty() || !fs::is_regular_file(from, ec)) error = "That file isn't there anymore";
        else if (to.empty() || !script_extension(to)) error = "Scripts end in .lua, .luau or .txt";
        else if (fs::exists(to, ec) && !fs::equivalent(from, to, ec)) error = "A file with that name already exists";
        else if (!MoveFileExW(from.c_str(), to.c_str(), 0) && !fs::equivalent(from, to, ec))
            error = "The file couldn't be renamed (error " + std::to_string(GetLastError()) + ")";
        reply.update({{"group", group}, {"rel", rel}, {"to", field(request, "to")}, {"ok", error.empty()}, {"error", error}});
    }
    else if (type == "files:reveal")
    {
        const std::wstring base = folder(group);
        const auto shown = reinterpret_cast<INT_PTR>(base.empty() ? nullptr : ShellExecuteW(nullptr, L"explore", base.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
        reply["ok"] = shown > 32;
    }
    else if (type == "files:autoexec")
    {
        nlohmann::json scripts = nlohmann::json::array();
        for (const auto& f : list_group("autoexec")["files"])
        {
            std::string error;
            const std::string text = read_text(resolve("autoexec", f["rel"].get<std::string>()), error);
            if (error.empty() && !text.empty())
                scripts.push_back({{"name", f["name"]}, {"source", text}});
        }
        reply.update({{"ok", true}, {"scripts", scripts}});
    }
    else reply["error"] = "unknown request";
    return reply;
}

}
