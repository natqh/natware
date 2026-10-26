// editor.exe: the script editor on its own. It hosts the WebView2 page (served from the exe's
// resources), starts nexec.exe when you attach, and talks to it over localhost HTTP. natware.exe
// starts it from the launcher and keeps it up to date; it also runs on its own.
//
//   editor.exe [--auto-attach]   --auto-attach: attach to Roblox 3 s after the window is up
#include "version.h"
#include "../natware/src/paths.h"
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

// The "n" (icon resource 1, ..\natware\src\natware.ico) on the editor window, for the taskbar and
// Alt+Tab. The window is this thread's, so it's found without touching editor_window.cpp.
static void set_window_icon()
{
    EnumThreadWindows(GetCurrentThreadId(), [](HWND hwnd, LPARAM) -> BOOL
    {
        wchar_t cls[64]{};
        GetClassNameW(hwnd, cls, 64);
        if (lstrcmpW(cls, L"natware_editor_wnd") != 0)
            return TRUE;
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        if (const HANDLE big = LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0))
            SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(big));
        if (const HANDLE little = LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0))
            SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(little));
        return FALSE;
    }, 0);
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
    // The editor runs from the natware folder (NATWARE_ROOT when natware started it): data\webview,
    // workspace\ and autoexec\ are there; nexec.exe sits beside this exe in bin\.
    SetCurrentDirectoryW(natware_paths::root().c_str());

    // A windowed app has no console; route stray log lines to NUL so printf stays safe.
    FILE* fp = nullptr;
    freopen_s(&fp, "NUL", "w", stdout);
    freopen_s(&fp, "NUL", "w", stderr);

    const HRESULT com_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com_result)) return 1;
    editor_diagnostics::write("info", "editor.version", {{"version", EDITOR_VERSION}});

    const bool created = natware_editor::create();
    if (created) {
        set_window_icon();
        const char* cmdline = GetCommandLineA();
        const bool auto_attach = strstr(cmdline, "--auto-attach") != nullptr;
        ULONGLONG auto_attach_at = auto_attach ? (GetTickCount64() + 3000) : 0;
        debug_log("w", "cmdline: %s\nauto_attach: %d\nauto_attach_at: %llu\ncreated: %d\n",
            cmdline, auto_attach ? 1 : 0, auto_attach_at, created ? 1 : 0);
        while (natware_editor::is_alive()) {
            natware_editor::pump();
            if (auto_attach_at && GetTickCount64() >= auto_attach_at) {
                auto_attach_at = 0;
                bool already = s_inject_started.exchange(true);
                debug_log("a", "auto_attach fired: already=%d\n", already ? 1 : 0);
                // attaches the selected (else first) Roblox client unless it already is
                if (!already)
                    natware_editor::trigger_attach();
            }
            Sleep(16);
        }
    }
    natware_editor::destroy();
    CoUninitialize();
    // ExitProcess, like natware: detached attach and execute workers may still be running.
    ExitProcess(created ? 0 : 1);
}
