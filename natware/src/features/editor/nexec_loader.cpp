#include "nexec_loader.h"
#include "nexec_bridge.h"
#include "editor_diagnostics.h"
#include <Windows.h>
#include <TlHelp32.h>
#include <string>
#include <vector>

namespace nexec_loader {

static bool s_launched = false;
static HANDLE s_process = nullptr;

static std::wstring executable_dir()
{
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir(exe);
    dir.resize(dir.find_last_of(L'\\') + 1);
    return dir;
}

static std::wstring nexec_path()
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

static DWORD find_roblox_pid()
{
    const wchar_t* names[] = {
        L"RobloxPlayerBeta.exe", L"Roblox.exe",
        L"Windows10Universal.exe", nullptr
    };
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
    DWORD pid = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            for (int i = 0; names[i]; ++i)
                if (!lstrcmpiW(pe.szExeFile, names[i])) { pid = pe.th32ProcessID; break; }
            if (pid) break;
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

bool inject(std::string& error)
{
    error.clear();
    if (s_launched) return true;

    if (nexec_bridge::available())
    {
        s_launched = true;
        return true;
    }

    const std::wstring exe = nexec_path();
    if (exe.empty())
    {
        error = "nexec.exe was not found next to natware.exe";
        return false;
    }

    const DWORD roblox_pid = find_roblox_pid();
    if (!roblox_pid)
    {
        error = "Roblox is not running";
        return false;
    }

    nexec_bridge::set_pid(roblox_pid);

    editor_diagnostics::write("info", "loader.starting",
        {{"pid", static_cast<int>(roblox_pid)}});

    std::wstring cmdline = L"\"" + exe + L"\" " + std::to_wstring(roblox_pid);
    std::vector<wchar_t> cmdline_buf(cmdline.begin(), cmdline.end());
    cmdline_buf.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), cmdline_buf.data(), nullptr, nullptr,
                        FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
    {
        error = "Failed to launch nexec.exe (err=" + std::to_string(GetLastError()) + ")";
        return false;
    }

    CloseHandle(pi.hThread);
    s_process = pi.hProcess;
    s_launched = true;

    editor_diagnostics::write("info", "loader.launched",
        {{"pid", static_cast<int>(roblox_pid)},
         {"nexec_pid", static_cast<int>(pi.dwProcessId)}});
    return true;
}

}
