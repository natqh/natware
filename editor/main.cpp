// editor.exe: the script editor on its own. It hosts the WebView2 page (served from the exe's
// resources), starts nexec.exe when you attach, and talks to it over localhost HTTP. natware.exe
// starts it from the launcher and keeps it up to date; it also runs on its own.
//
//   editor.exe [--auto-attach]   --auto-attach: attach to Roblox 3 s after the window is up
#include "version.h"
#include "../natware/src/features/editor/editor_window.h"
#include "../natware/src/features/editor/editor_diagnostics.h"
#include "../natware/src/features/editor/nexec_loader.h"
#include "../natware/src/features/editor/nexec_bridge.h"
#include <Windows.h>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

static std::atomic<bool> s_inject_started{false};
static std::atomic<bool> s_inject_done{false};
static void safe_inject_thread()
{
    std::string error;
    nexec_loader::inject(error);
    s_inject_done.store(true);
}

// %TEMP%\natware_main_dbg.log: what --auto-attach saw.
static void debug_log(const char* mode, const char* format, ...)
{
    char dbgPath[MAX_PATH]{};
    char tmp[MAX_PATH];
    DWORD n = GetTempPathA(MAX_PATH, tmp);
    if (n > 0 && n < MAX_PATH - 30) { lstrcpyA(dbgPath, tmp); lstrcatA(dbgPath, "natware_main_dbg.log"); }
    else lstrcpyA(dbgPath, "natware_main_dbg.log");
    FILE* dbgF = nullptr;
    if (fopen_s(&dbgF, dbgPath, mode) == 0 && dbgF) {
        va_list args;
        va_start(args, format);
        vfprintf(dbgF, format, args);
        va_end(args);
        fclose(dbgF);
    }
}

int main()
{
    // Shells and shortcuts can start us with an unrelated working directory; data\webview and
    // nexec.exe are found beside the exe.
    wchar_t executable_path[32768]{};
    const DWORD executable_length = GetModuleFileNameW(nullptr, executable_path, 32768);
    if (executable_length > 0 && executable_length < 32768)
    {
        wchar_t* separator = wcsrchr(executable_path, L'\\');
        if (separator)
        {
            *separator = L'\0';
            SetCurrentDirectoryW(executable_path);
        }
    }

    // A windowed app has no console; route stray log lines to NUL so printf stays safe.
    FILE* fp = nullptr;
    freopen_s(&fp, "NUL", "w", stdout);
    freopen_s(&fp, "NUL", "w", stderr);

    const HRESULT com_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com_result)) return 1;
    editor_diagnostics::write("info", "editor.version", {{"version", EDITOR_VERSION}});

    const bool created = natware_editor::create();
    if (created) {
        const char* cmdline = GetCommandLineA();
        const bool auto_attach = strstr(cmdline, "--auto-attach") != nullptr;
        ULONGLONG auto_attach_at = auto_attach ? (GetTickCount64() + 3000) : 0;
        debug_log("w", "cmdline: %s\nauto_attach: %d\nauto_attach_at: %llu\ncreated: %d\n",
            cmdline, auto_attach ? 1 : 0, auto_attach_at, created ? 1 : 0);
        while (natware_editor::is_alive()) {
            natware_editor::pump();
            if (auto_attach_at && GetTickCount64() >= auto_attach_at) {
                auto_attach_at = 0;
                bool bridge = nexec_bridge::available();
                bool already = s_inject_started.load();
                debug_log("a", "auto_attach fired: bridge=%d already=%d\n", bridge ? 1 : 0, already ? 1 : 0);
                if (!bridge && !s_inject_started.exchange(true))
                    std::thread(safe_inject_thread).detach();
            }
            Sleep(16);
        }
    }
    natware_editor::destroy();
    CoUninitialize();
    // ExitProcess, like natware: detached attach and execute workers may still be running.
    ExitProcess(created ? 0 : 1);
}
