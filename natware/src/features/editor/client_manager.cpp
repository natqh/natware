#include "client_manager.h"
#include "editor_diagnostics.h"
#include <Windows.h>
#include <TlHelp32.h>
#include <algorithm>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace client_manager {

namespace {

std::mutex s_mutex;
unsigned long s_selected = 0;

struct Launch {
    unsigned short port = 0;
    ULONGLONG at = 0;
    std::string error;
};
std::map<unsigned long, Launch> s_launches;     // pid -> backend we started for it
HANDLE s_job = nullptr;                          // kills the backends we started when the editor exits

constexpr ULONGLONG kLaunchGraceMs = 60000;      // a cold start (Roblox attach + bootstrap) is slow

std::wstring executable_dir()
{
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir(exe);
    dir.resize(dir.find_last_of(L'\\') + 1);
    return dir;
}

std::wstring nexec_path()
{
    const std::wstring dir = executable_dir();
    const std::wstring candidates[] = {
        dir + L"nexec.exe",
        dir + L"..\\..\\nexec\\target\\release\\nexec.exe",
        dir + L"..\\..\\..\\nexec\\target\\release\\nexec.exe",
    };
    for (const auto& c : candidates)
        if (GetFileAttributesW(c.c_str()) != INVALID_FILE_ATTRIBUTES)
            return c;
    return {};
}

std::vector<unsigned long> roblox_pids()
{
    std::vector<unsigned long> pids;
    const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return pids;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snap, &entry))
    {
        do {
            if (!lstrcmpiW(entry.szExeFile, L"RobloxPlayerBeta.exe"))
                pids.push_back(entry.th32ProcessID);
        } while (Process32NextW(snap, &entry));
    }
    CloseHandle(snap);
    std::sort(pids.begin(), pids.end());
    return pids;
}

// Every backend that answers on the port range, whoever started it (a nexec started by hand or by
// an earlier editor is adopted instead of being launched twice).
std::vector<nexec_bridge::Status> probe_backends()
{
    std::vector<nexec_bridge::Status> found;
    for (unsigned short i = 0; i < nexec_bridge::kPortCount; ++i)
    {
        nexec_bridge::Status status;
        if (nexec_bridge::status(static_cast<unsigned short>(nexec_bridge::kFirstPort + i), status, 150))
            found.push_back(std::move(status));
    }
    return found;
}

}

std::vector<Client> refresh()
{
    const auto pids = roblox_pids();
    const auto backends = probe_backends();

    std::lock_guard<std::mutex> lock(s_mutex);

    // forget launches whose client is gone
    for (auto it = s_launches.begin(); it != s_launches.end();)
        it = std::find(pids.begin(), pids.end(), it->first) == pids.end() ? s_launches.erase(it) : std::next(it);

    std::vector<Client> clients;
    for (const unsigned long pid : pids)
    {
        Client client;
        client.pid = pid;
        for (const auto& backend : backends)
            if (backend.pid == pid)
                client.status = backend;

        const auto launch = s_launches.find(pid);
        if (launch != s_launches.end())
        {
            if (client.status.up)
            {
                launch->second.error.clear();
            }
            else if (GetTickCount64() - launch->second.at < kLaunchGraceMs)
            {
                client.launching = true;
            }
            else
            {
                launch->second.error = "The backend did not start";
            }
            client.error = launch->second.error;
        }
        clients.push_back(std::move(client));
    }

    if (s_selected != 0 && std::find(pids.begin(), pids.end(), s_selected) == pids.end())
        s_selected = 0;
    return clients;
}

bool attach(unsigned long pid, std::string& error)
{
    error.clear();
    if (pid == 0)
    {
        error = "Roblox is not running";
        return false;
    }

    // a backend already serves it, or one is starting
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        const auto launch = s_launches.find(pid);
        if (launch != s_launches.end() && GetTickCount64() - launch->second.at < kLaunchGraceMs)
            return true;
    }
    const auto backends = probe_backends();
    for (const auto& backend : backends)
    {
        if (backend.pid != pid)
            continue;
        // a backend that is up but not attached is stuck on the current game: try the injection again
        if (!backend.attached && backend.inGame)
            nexec_bridge::reinject(backend.port, pid);
        return true;
    }

    const std::wstring exe = nexec_path();
    if (exe.empty())
    {
        error = "nexec.exe was not found next to the editor";
        return false;
    }

    // lowest free port
    unsigned short port = 0;
    for (unsigned short i = 0; i < nexec_bridge::kPortCount && port == 0; ++i)
    {
        const unsigned short candidate = static_cast<unsigned short>(nexec_bridge::kFirstPort + i);
        bool taken = false;
        for (const auto& backend : backends)
            taken = taken || backend.port == candidate;
        std::lock_guard<std::mutex> lock(s_mutex);
        for (const auto& launch : s_launches)
            taken = taken || (launch.second.port == candidate && GetTickCount64() - launch.second.at < kLaunchGraceMs);
        if (!taken)
            port = candidate;
    }
    if (port == 0)
    {
        error = "Too many clients are attached";
        return false;
    }

    std::wstring cmdline = L"\"" + exe + L"\" " + std::to_wstring(pid) + L" " + std::to_wstring(port);
    std::vector<wchar_t> buffer(cmdline.begin(), cmdline.end());
    buffer.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), buffer.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
    {
        error = "Failed to launch nexec.exe (err=" + std::to_string(GetLastError()) + ")";
        return false;
    }
    CloseHandle(pi.hThread);

    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (!s_job)
        {
            s_job = CreateJobObjectW(nullptr, nullptr);
            if (s_job)
            {
                JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
                limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
                SetInformationJobObject(s_job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
            }
        }
        if (s_job)
            AssignProcessToJobObject(s_job, pi.hProcess);
        s_launches[pid] = Launch{ port, GetTickCount64(), {} };
        if (s_selected == 0)
            s_selected = pid;
    }
    CloseHandle(pi.hProcess);

    editor_diagnostics::write("info", "loader.launched", {{"pid", static_cast<int>(pid)}, {"port", port}, {"nexec_pid", static_cast<int>(pi.dwProcessId)}});
    return true;
}

bool detach(unsigned long pid, std::string& error)
{
    error.clear();
    for (const auto& backend : probe_backends())
    {
        if (backend.pid != pid)
            continue;
        const HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, backend.backendPid);
        if (!process)
        {
            error = "The backend could not be stopped (err=" + std::to_string(GetLastError()) + "). Close it from Task Manager.";
            return false;
        }
        TerminateProcess(process, 0);
        CloseHandle(process);
        std::lock_guard<std::mutex> lock(s_mutex);
        s_launches.erase(pid);
        return true;
    }
    std::lock_guard<std::mutex> lock(s_mutex);
    s_launches.erase(pid);
    return true;
}

unsigned long selected()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_selected;
}

void select(unsigned long pid)
{
    std::lock_guard<std::mutex> lock(s_mutex);
    s_selected = pid;
}

unsigned long resolve_primary(const std::vector<Client>& clients)
{
    const unsigned long chosen = selected();
    for (const auto& client : clients)
        if (client.pid == chosen)
            return chosen;
    for (const auto& client : clients)
        if (client.status.attached)
            return client.pid;
    return clients.empty() ? 0 : clients.front().pid;
}

bool is_attached(unsigned long pid)
{
    for (unsigned short i = 0; i < nexec_bridge::kPortCount; ++i)
    {
        nexec_bridge::Status status;
        if (nexec_bridge::status(static_cast<unsigned short>(nexec_bridge::kFirstPort + i), status, 150) && status.pid == pid)
            return status.attached;
    }
    return false;
}

}
