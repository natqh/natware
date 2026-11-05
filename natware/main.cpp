#include "include.hxx"
#include <timeapi.h>
#include <shellapi.h>
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "urlmon.lib")
#include <Offsets/OffsetLoader.hpp>
#include <features/config/config.h>
#include <menu/preview/preview.h>
#include <features/chams/engine_chams.h>
#include <updater/updater.h>
#include <version.h>
#include <paths.h>
#include <layout.h>
#include <memory/driver_loader.h>
// The "n" (icon resource 1, src\natware.ico) on a window, for the taskbar and Alt+Tab.
static void set_window_icon(HWND hwnd)
{
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    if (const HANDLE big = LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0))
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(big));
    if (const HANDLE little = LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0))
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(little));
}

// natware has no console: problems are shown in a message box, and while it waits on Roblox
// a small card says so and keeps a way to close.
static int fail(const char* what)
{
    MessageBoxA(nullptr, what, "natware", MB_OK | MB_ICONERROR | MB_TOPMOST);
    return 1;
}

// Configs and settings are saved as they change, so nothing is pending here. ExitProcess stops
// the cache, triggerbot and wall check threads before anything is torn down; std::exit would
// run the static destructors while those threads still read them.
// The overlay never needs more frames than the screen shows. Without a cap the loop ran as fast
// as it could, redoing every memory read for frames nobody saw. A high-resolution waitable timer
// holds it to the refresh rate (60 in performance mode); with V-Sync on, Present already waits.
static void pace_frame()
{
    if (settings::menu::vsync)
        return;

    static HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    static const int refresh = []
    {
        DEVMODEW dm{};
        dm.dmSize = sizeof(dm);
        return EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1 ? static_cast<int>(dm.dmDisplayFrequency) : 60;
    }();
    const int hz = settings::menu::performance_mode ? (std::min)(refresh, 60) : refresh;

    using clock = std::chrono::steady_clock;
    static clock::time_point next = clock::now();
    next += std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(1.0 / hz));
    const clock::time_point now = clock::now();
    if (next < now - std::chrono::milliseconds(50))
        next = now;   // fell far behind (a hitch): start over rather than rush
    if (next <= now)
        return;

    const auto wait = std::chrono::duration_cast<std::chrono::nanoseconds>(next - now).count();
    if (timer)
    {
        LARGE_INTEGER due{};
        due.QuadPart = -static_cast<LONGLONG>(wait / 100);   // relative, in 100 ns units
        if (SetWaitableTimerEx(timer, &due, 0, nullptr, nullptr, nullptr, 0))
        {
            WaitForSingleObject(timer, INFINITE);
            return;
        }
    }
    std::this_thread::sleep_for(std::chrono::nanoseconds(wait));
}

static bool g_settings_restored = false;   // set once External has put your settings in place

static void quit_now()
{
    if (g_settings_restored)
        config::save_last_settings();
    updater::finish_on_quit();   // an update you chose to take on close goes in now
    Menu::Shutdown();
    ExitProcess(0);
}

// One frame of the waiting card; false once the user closed natware.
static bool wait_frame(const char* status)
{
    render->start_render();
    Menu::DrawWaiting(status);
    render->end_render();
    return !settings::globals::quit_requested;
}

// Waits about `ms` while keeping the card drawn and closable.
static void wait_for(const char* status, int ms)
{
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < until)
    {
        if (!wait_frame(status))
            quit_now();
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
}

// ---- the editor (editor.exe) ---------------------------------------------------------------
// The editor is its own program, in the natware folder's bin\. natware starts it and stays in the background
// while it's open: it keeps checking for updates, offers them in a Windows dialog (the editor's
// page has no overlay to draw a prompt on), restarts the editor into a new version, and puts a
// staged natware in place once the editor closes. The editor starts nexec.exe itself.

static PROCESS_INFORMATION g_editor{};
// The editor was closed so natware can restart into its new version: the coordinator waits for
// the new natware to start instead of quitting with the editor (that would cut the start off).
static std::atomic<bool> g_editor_closed_for_restart{ false };

static std::wstring widen(const std::string& s)
{
    return std::wstring(s.begin(), s.end());   // versions and component names: ASCII
}

static updater::component_t find_component(const char* name)
{
    for (const updater::component_t& c : updater::components())
        if (c.name == name)
            return c;
    return {};
}

// What natware was started with, minus its own flags, for the editor (--auto-attach).
static std::wstring editor_args()
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring out;
    for (int i = 1; argv && i < argc; ++i)
    {
        const std::wstring a = argv[i];
        if (a == L"--editor" || a == L"--external")
            continue;
        out += (out.empty() ? L"" : L" ") + (a.find(L' ') != std::wstring::npos ? L"\"" + a + L"\"" : a);
    }
    LocalFree(argv);
    return out;
}

static bool start_editor(std::string& error)
{
    const std::wstring path = updater::path_of("editor");
    const std::wstring args = editor_args();
    std::wstring command = L"\"" + path + L"\"" + (args.empty() ? L"" : L" " + args);
    const std::wstring dir = path.substr(0, path.find_last_of(L'\\'));
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi))
    {
        error = "couldn't start editor.exe (error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    CloseHandle(pi.hThread);
    if (g_editor.hProcess)
        CloseHandle(g_editor.hProcess);
    g_editor = pi;
    AllowSetForegroundWindow(pi.dwProcessId);
    return true;
}

static bool editor_running()
{
    return g_editor.hProcess && WaitForSingleObject(g_editor.hProcess, 0) == WAIT_TIMEOUT;
}

// Asks the editor to close the way its own close button does (it saves your tabs first) and
// waits for it. Also what runs before natware restarts into a new version, so the new natware
// doesn't open a second editor beside this one.
static void close_editor()
{
    if (!editor_running())
        return;
    EnumWindows([](HWND hwnd, LPARAM) -> BOOL
    {
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == g_editor.dwProcessId && IsWindowVisible(hwnd))
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        return TRUE;
    }, 0);
    WaitForSingleObject(g_editor.hProcess, 10000);
}

// A component the editor needs and that isn't there yet (the first start after natware was
// updated from a single-exe version): downloaded now, with the waiting card showing how far.
static bool ensure_component(const char* name, bool report = true)
{
    updater::component_t c = find_component(name);
    if (c.present)
        return true;
    while (updater::status().state == updater::state_t::checking)
        wait_for("Looking for updates", 100);
    c = find_component(name);
    if (!c.available)
        return false;

    std::atomic<bool> finished{ false };
    bool ok = false;
    std::string error;
    std::thread worker([&] { ok = updater::install_now(name, error); finished = true; });
    while (!finished)
    {
        c = find_component(name);
        char text[96];
        snprintf(text, sizeof(text), "Downloading %s %s  \xC2\xB7  %d%%", name, c.latest.c_str(), (int)(c.progress * 100.0f + 0.5f));
        if (!wait_frame(text))
        {
            worker.detach();   // the download is cut off when natware exits; it's removed next start
            quit_now();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
    worker.join();
    if (!ok && report)
    {
        const std::string what = std::string("natware couldn't download ") + name + ".exe: " + error;
        MessageBoxA(nullptr, what.c_str(), "natware", MB_OK | MB_ICONERROR | MB_TOPMOST);
    }
    return ok;
}

// ---- everything natware needs, fetched on the first start ----------------------------------
// natware is meant to be a single exe that sets the rest up itself: the editor and the nexec
// backend come from its GitHub releases, and the editor's page needs the Microsoft Edge
// WebView2 runtime (already part of Windows 11 and of Edge), which is installed from Microsoft's
// own bootstrapper when it's missing. Offline, or when nothing is released, this does nothing
// and natware starts as before; the editor reports what it's missing when it's opened.
static bool webview2_present()
{
    static const wchar_t* keys[] = {
        L"SOFTWARE\\WOW6432Node\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}",
        L"SOFTWARE\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}",
    };
    for (HKEY root : { HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER })
        for (const wchar_t* key : keys)
        {
            wchar_t version[64]{};
            DWORD size = sizeof(version);
            if (RegGetValueW(root, key, L"pv", RRF_RT_REG_SZ, nullptr, version, &size) == ERROR_SUCCESS
                && version[0] && wcscmp(version, L"0.0.0.0") != 0)
                return true;
        }
    return false;
}

static bool ensure_webview2()
{
    if (webview2_present())
        return true;

    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    const std::wstring setup = std::wstring(temp) + L"natware_webview2_setup.exe";

    std::atomic<bool> finished{ false };
    bool ok = false;
    std::thread worker([&]
    {
        // Microsoft's evergreen bootstrapper; it downloads and installs the runtime itself.
        if (URLDownloadToFileW(nullptr, L"https://go.microsoft.com/fwlink/p/?LinkId=2124703", setup.c_str(), 0, nullptr) == S_OK)
        {
            std::wstring command = L"\"" + setup + L"\" /silent /install";
            STARTUPINFOW si{ sizeof(si) };
            PROCESS_INFORMATION pi{};
            if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
            {
                WaitForSingleObject(pi.hProcess, 5 * 60 * 1000);
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
            }
            else
            {
                // needs elevation: let Windows ask for it
                SHELLEXECUTEINFOW sei{ sizeof(sei) };
                sei.fMask = SEE_MASK_NOCLOSEPROCESS;
                sei.lpVerb = L"runas";
                sei.lpFile = setup.c_str();
                sei.lpParameters = L"/silent /install";
                sei.nShow = SW_HIDE;
                if (ShellExecuteExW(&sei) && sei.hProcess)
                {
                    WaitForSingleObject(sei.hProcess, 5 * 60 * 1000);
                    CloseHandle(sei.hProcess);
                }
            }
            DeleteFileW(setup.c_str());
        }
        ok = webview2_present();
        finished = true;
    });
    while (!finished)
    {
        if (!wait_frame("Installing the WebView2 runtime"))
        {
            worker.detach();
            quit_now();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
    worker.join();
    return ok;
}

static void ensure_dependencies()
{
    // Missing components first (only what's missing costs anything), then the runtime.
    updater::component_t editor = find_component("editor");
    updater::component_t backend = find_component("nexec");
    if (!editor.present || !backend.present)
    {
        if (!editor.present)
            ensure_component("editor", false);
        if (!backend.present)
            ensure_component("nexec", false);
    }
    ensure_webview2();
}

// A release that came out while the editor is open: asked once. "Yes" installs it now and
// restarts what it replaced; "No" downloads it now and lets natware go in when the editor closes
// (the editor and nexec go in right away and are used from their next start).
static void editor_offer_update(bool& restart_pending)
{
    static bool asked = false;
    const updater::status_t st = updater::status();
    if (asked || st.state != updater::state_t::available)
        return;
    asked = true;

    bool editor_in = false;
    for (const updater::component_t& c : updater::components())
        editor_in = editor_in || (c.name == "editor" && c.available && c.selected);
    const std::wstring restarts = st.restarts ? L" (natware and the editor restart)" : editor_in ? L" (the editor restarts)" : L"";
    const std::wstring text = widen(st.latest) + (st.count == 1 ? L" is out." : L" are out.") +
        L"\n\nYes: update now" + restarts + L".\n"
        L"No: download now and install when you close the editor.\n"
        L"Cancel: skip this update for now.";
    const int choice = MessageBoxW(nullptr, text.c_str(), L"natware",
        MB_YESNOCANCEL | MB_ICONINFORMATION | MB_TOPMOST | MB_DEFBUTTON2);
    if (choice == IDCANCEL)
        return;
    updater::install_async(choice == IDYES);
    restart_pending = choice == IDYES && editor_in && !st.restarts;
}

static int run_editor()
{
    if (!ensure_component("editor"))
    {
        if (!find_component("editor").present)
            return fail("editor.exe isn't in natware's folder (%LOCALAPPDATA%\\natware\\bin), and no release of it was found to download.");
        return 1;
    }
    // The backend too, when it's missing and released (the editor also looks for a local build).
    if (!find_component("nexec").present && find_component("nexec").available)
        ensure_component("nexec");

    render->running = false;
    ShowWindow(render->detail->window, SW_HIDE);

    std::string error;
    if (!start_editor(error))
        return fail(("natware couldn't open the editor: " + error).c_str());
    // A natware update restarts natware straight back into the editor, closing this one first.
    updater::set_restart(L"--editor" + (editor_args().empty() ? L"" : L" " + editor_args()), []
    {
        g_editor_closed_for_restart = true;
        close_editor();
    });

    bool restart_pending = false;
    for (;;)
    {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (settings::globals::quit_requested)
            break;   // natware restarted into its new version
        if (!editor_running())
        {
            if (!g_editor_closed_for_restart)
                break;   // you closed the editor
            const updater::status_t st = updater::status();
            if (st.state != updater::state_t::installing && st.state != updater::state_t::downloading && !settings::globals::quit_requested)
            {
                // The new natware went in but didn't start.
                if (st.state == updater::state_t::failed)
                    fail(("natware updated, but: " + st.message).c_str());
                break;
            }
            Sleep(50);
            continue;
        }
        editor_offer_update(restart_pending);
        if (restart_pending)
        {
            const updater::state_t s = updater::status().state;
            if (s != updater::state_t::available && s != updater::state_t::downloading && s != updater::state_t::installing)
            {
                restart_pending = false;
                if (updater::updated("editor"))
                {
                    close_editor();
                    if (!start_editor(error))
                        return fail(("natware couldn't reopen the editor: " + error).c_str());
                }
            }
        }
        Sleep(50);
    }
    return 0;
}

int main()
{
    // natware.exe can sit anywhere; everything else lives in the natware folder (paths.h), and the
    // relative paths (data\, configs\) resolve there. The preview tests keep to the folder beside
    // the exe so they never touch your real settings.
    wchar_t executable_path[32768]{};
    const DWORD executable_length = GetModuleFileNameW(nullptr, executable_path, 32768);
    if (executable_length > 0 && executable_length < 32768)
    {
        wchar_t* separator = wcsrchr(executable_path, L'\\');
        if (separator)
            *separator = L'\0';
    }
    if (strstr(GetCommandLineA(), "--preview"))
        SetEnvironmentVariableW(L"NATWARE_ROOT", executable_path);
    else
        layout::adopt_from(executable_path);   // what an older natware kept beside the exe
    const std::wstring natware_root = natware_paths::root();
    SetCurrentDirectoryW(natware_root.c_str());
    // The editor and nexec, started from here, use the same folder.
    SetEnvironmentVariableW(L"NATWARE_ROOT", natware_root.c_str());

    // 1 ms timer resolution for the feature threads' short sleeps.
    timeBeginPeriod(1);

    // A windowed app has no console; route the old log lines to NUL so printf stays safe.
    FILE* fp = nullptr;
    freopen_s(&fp, "NUL", "w", stdout);
    freopen_s(&fp, "NUL", "w", stderr);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Load external config (rebranding) first
    external_config::load();

    if (strstr(GetCommandLineA(), "--preview"))
        return menu_preview::run();

    const bool cli_external = strstr(GetCommandLineA(), "--external") != nullptr;
    const bool cli_editor = strstr(GetCommandLineA(), "--editor") != nullptr;   // straight to editor.exe

    updater::cleanup();       // the exe an earlier update moved aside
    updater::check_async();   // the picker shows what it finds; checked again every 10 minutes

    printf("=== %s ===\n\n", external_config::cheat_name.c_str());

    // Create the overlay window + D3D11 + ImGui early so the mode picker can render
    if (!render->create_window())
    {
        return fail("natware couldn't start: failed to create the window.");
    }
    set_window_icon(render->detail->window);

    if (!render->create_device())
    {
        return fail("natware couldn't start: failed to create the device.");
    }

    if (!render->create_imgui())
    {
        return fail("natware couldn't start: failed to create the imgui.");
    }

    if (!Menu::Initialize(render->detail->window, render->detail->device, render->detail->device_context))
    {
        return fail("natware couldn't start: failed to initialize the menu.");
    }

    // Show the mode picker before attaching to Roblox
    render->running = true;
    ensure_dependencies();
    SetWindowLong(render->detail->window, GWL_EXSTYLE, WS_EX_APPWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED);
    SetForegroundWindow(render->detail->window);

    if (cli_external) {
        settings::app_mode = settings::MODE_EXTERNAL;
        printf("--external flag: skipping mode picker\n");
    } else if (cli_editor) {
        settings::app_mode = settings::MODE_EXECUTOR;
    } else {
        SetWindowTextW(render->detail->window, L"natware - Select mode");
        printf("Waiting for mode selection...\n");
        while (settings::app_mode == settings::MODE_PICKER && !settings::globals::quit_requested)
        {
            render->start_render();
            Menu::DrawModePicker();
            render->end_render();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (settings::globals::quit_requested)
            quit_now();
    }
    printf("Mode selected: %s\n", settings::app_mode == settings::MODE_EXECUTOR ? "Executor" : "External");

    // Intel driver loaded here so nexec can use \\.\Nal for kernel-level access
    if (settings::app_mode == settings::MODE_EXECUTOR) {
        if (physmem::g_device == INVALID_HANDLE_VALUE)
        {
            std::wstring vuln = physmem::find_vuln_sys();
            if (vuln.empty())
            {
                fail("Intel driver not found (should be embedded).");
                ExitProcess(1);
            }
            std::string pm_err;
            bool loaded = false;
            std::thread loader([&] {
                loaded = physmem::load(vuln, pm_err);
                if (loaded && !physmem::self_test()) {
                    pm_err = "kernel R/W self-test failed";
                    physmem::unload();
                    loaded = false;
                }
            });
            while (!loaded && pm_err.empty())
            {
                if (!wait_frame("Loading driver"))
                {
                    loader.detach();
                    quit_now();
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(15));
            }
            loader.join();
            if (!loaded)
            {
                fail(("Intel driver failed: " + pm_err).c_str());
                ExitProcess(1);
            }
            printf("Intel driver loaded, kernel R/W verified.\n");
        }
        else
        {
            printf("Intel driver already loaded.\n");
        }

        if (run_editor() != 0)
            ExitProcess(1);
        physmem::unload();
        quit_now();
    }
    // An update restarts natware straight back into the overlay.
    updater::set_restart(L"--external");

    static const char* PROCESS_NAMES[] = {
        "RobloxPlayerBeta.exe",
        "Roblox.exe",
        "Windows10Universal.exe",
        nullptr
    };

    printf("Waiting for Roblox...\n");
    const char* BINARY_NAME = nullptr;
    while (true) {
        for (int i = 0; PROCESS_NAMES[i]; i++) {
            if (memory->find_process_id(PROCESS_NAMES[i])) {
                BINARY_NAME = PROCESS_NAMES[i];
                break;
            }
        }
        if (BINARY_NAME) break;
        wait_for("Waiting for Roblox to open", 500);
    }
    printf("Roblox found! (%s)\n", BINARY_NAME);

    while (!memory->attach_to_process(BINARY_NAME))
    {
        wait_for("Connecting to Roblox", 500);
    }
    printf("Attached to Roblox!\n");

    while (!memory->find_module_address(BINARY_NAME))
    {
        wait_for("Connecting to Roblox", 500);
    }
    printf("Module found!\n");

    // Detect Roblox version from process path
    std::string robloxVersion = OffsetLoader::GetRobloxVersionFromProcess(memory->get_process_handle(), memory->get_process_id());

    // Try auto-updating offsets from web first
    bool offsetsLoaded = false;
    std::string fetchVersion = robloxVersion.empty() ? Offsets::ClientVersion : robloxVersion;
    if (OffsetLoader::FetchOffsetsFromWeb(fetchVersion)) {
        offsetsLoaded = true;
        printf("Offsets auto-updated from web!\n");
    }

    // Auto-update FFlag offsets from web
    if (!OffsetLoader::FetchFFlagsFromWeb(fetchVersion)) {
        printf("Using built-in FFlag offsets (may be stale)\n");
    }

    // Fall back to local JSON if web failed
    if (!offsetsLoaded) {
        int offsetResult = OffsetLoader::LoadOffsets();
        if (offsetResult == 1) {
            offsetsLoaded = true;
        } else if (offsetResult == -1) {
            printf("WARNING: offsets.json has errors\n");
        }
    }

    if (!offsetsLoaded) {
        printf("Using built-in fallback offsets (%s)\n", Offsets::ClientVersion.c_str());
    }

    // Version mismatch warning
    bool version_mismatch = false;
    if (!robloxVersion.empty() && robloxVersion != Offsets::ClientVersion) {
        version_mismatch = true;
        printf("\n");
        printf("========================================\n");
        printf("  WARNING: Offset version mismatch!\n");
        printf("========================================\n");
        printf("  Offsets version: %s\n", Offsets::ClientVersion.c_str());
        printf("  Roblox version:  %s\n", robloxVersion.c_str());
        printf("========================================\n");
        printf("\n");
        const std::string question = "natware's offsets are for Roblox " + Offsets::ClientVersion + ", but Roblox "
            + robloxVersion + " is running. natware may not work or may crash.\n\nContinue anyway?";
        if (MessageBoxA(nullptr, question.c_str(), "natware", MB_YESNO | MB_ICONWARNING | MB_TOPMOST) != IDYES)
            quit_now();
    }

    // Wait for game to fully load (DataModel + Players must be valid)
    printf("Detected Roblox: %s\n", robloxVersion.empty() ? "(unknown)" : robloxVersion.c_str());
    printf("Using offsets:   %s (source: %s)\n", Offsets::ClientVersion.c_str(),
        OffsetLoader::WasLoadedFromWeb() ? "web" : OffsetLoader::WasLoadedFromJson() ? "json" : "built-in");
    printf("FakeDataModel::Pointer = 0x%llx\n", (unsigned long long)Offsets::FakeDataModel::Pointer);
    printf("Module base = 0x%llx\n", (unsigned long long)memory->get_module_address());
    printf("Waiting for Roblox to initialize...\n");
    std::uint64_t fake_datamodel = 0;
    while (true)
    {
        fake_datamodel = memory->read<std::uint64_t>(memory->get_module_address() + Offsets::FakeDataModel::Pointer);
        if (fake_datamodel != 0)
        {
            game::datamodel = rbx::instance_t(memory->read<std::uint64_t>(fake_datamodel + Offsets::FakeDataModel::RealDataModel));
            if (game::datamodel.address != 0)
                break;
        }
        wait_for("Waiting for Roblox to load", 500);
    }
    game::visengine = {memory->read<std::uint64_t>(memory->get_module_address() + Offsets::VisualEngine::Pointer)};
    game::players = game::datamodel.find_first_child_by_class("Players");
    printf("Roblox initialized! (Players: %s)\n", game::players.address ? "found" : "not in a place yet");

    // Validate all offsets by reading through them
    {
        int total = 0, valid = 0;
        std::string fails;
        #define VFAIL(name) do { fails += "  [FAIL] " name "\n"; } while(0)
        #define VCHECK(name, val) do { total++; if ((val) != 0) valid++; else VFAIL(name); } while(0)
        #define VREAD(name, base, off) [&]() -> std::uint64_t { total++; if ((base) == 0) { VFAIL(name); return 0; } try { auto v = memory->read<std::uint64_t>((base) + (off)); if (v != 0) { valid++; return v; } } catch (...) {} VFAIL(name); return 0; }()
        #define VFLOAT(name, base, off, lo, hi) do { total++; try { float v = memory->read<float>((base) + (off)); if (v >= (lo) && v <= (hi)) valid++; else VFAIL(name); } catch (...) { VFAIL(name); } } while(0)

        // Core
        VCHECK("FakeDataModel::Pointer", fake_datamodel);
        VCHECK("DataModel", game::datamodel.address);
        VCHECK("VisualEngine::Pointer", game::visengine.address);
        VCHECK("Players", game::players.address);

        // Services
        std::uint64_t workspace = game::datamodel.address ? game::datamodel.find_first_child_by_class("Workspace").address : 0;
        VCHECK("Workspace", workspace);
        std::uint64_t lighting = game::datamodel.address ? game::datamodel.find_first_child_by_class("Lighting").address : 0;
        VCHECK("Lighting", lighting);
        VCHECK("TextChatService", game::datamodel.address ? game::datamodel.find_first_child_by_class("TextChatService").address : 0);
        VCHECK("RunService", game::datamodel.address ? game::datamodel.find_first_child_by_class("RunService").address : 0);
        VCHECK("UserInputService", game::datamodel.address ? game::datamodel.find_first_child_by_class("UserInputService").address : 0);
        VCHECK("CoreGui", game::datamodel.address ? game::datamodel.find_first_child_by_class("CoreGui").address : 0);

        // Workspace
        std::uint64_t camera = VREAD("Workspace::CurrentCamera", workspace, Offsets::Workspace::CurrentCamera);
        std::uint64_t world = VREAD("Workspace::World", workspace, Offsets::Workspace::World);

        // Camera
        if (camera) { VREAD("Camera::CameraSubject", camera, Offsets::Camera::CameraSubject); VFLOAT("Camera::FieldOfView", camera, Offsets::Camera::FieldOfView, 1.0f, 180.0f); }

        // Player
        std::uint64_t local_player = 0;
        if (game::players.address) { local_player = VREAD("Player::LocalPlayer", game::players.address, Offsets::Player::LocalPlayer); if (local_player) { VREAD("Player::ModelInstance", local_player, Offsets::Player::ModelInstance); } }

        // VisualEngine
        std::uint64_t render_view = VREAD("VisualEngine::RenderView", game::visengine.address, Offsets::VisualEngine::RenderView);
        if (render_view) { VREAD("RenderView::VisualEngine", render_view, Offsets::RenderView::VisualEngine); VREAD("RenderView::DeviceD3D11", render_view, Offsets::RenderView::DeviceD3D11); }

        // World
        if (world) { VFLOAT("World::Gravity", world, Offsets::World::Gravity, 0.1f, 10000.0f); VREAD("World::Primitives", world, Offsets::World::Primitives); }

        // Lighting
        if (lighting) { VFLOAT("Lighting::Brightness", lighting, Offsets::Lighting::Brightness, 0.0f, 100.0f); VFLOAT("Lighting::ClockTime", lighting, Offsets::Lighting::ClockTime, -1.0f, 48.0f); }

        // DataModel props
        if (game::datamodel.address) { VREAD("DataModel::GameId", game::datamodel.address, Offsets::DataModel::GameId); VREAD("DataModel::PlaceId", game::datamodel.address, Offsets::DataModel::PlaceId); VREAD("DataModel::ScriptContext", game::datamodel.address, Offsets::DataModel::ScriptContext); }

        // Instance structure
        if (game::players.address) {
            try { auto cs = memory->read<std::uint64_t>(game::players.address + Offsets::Instance::ChildrenStart); auto s = memory->read<std::uint64_t>(cs); auto e = memory->read<std::uint64_t>(cs + Offsets::Instance::ChildrenEnd); total++; if (s && e && e > s) valid++; else VFAIL("Instance::Children"); } catch (...) { total++; VFAIL("Instance::Children"); }
            try { auto np = memory->read<std::uint64_t>(game::players.address + Offsets::Instance::Name); auto n = memory->read_string(np); total++; if (n == "Players") valid++; else VFAIL("Instance::Name"); } catch (...) { total++; VFAIL("Instance::Name"); }
            try { auto cd = memory->read<std::uint64_t>(game::players.address + Offsets::Instance::ClassDescriptor); auto cn = memory->read<std::uint64_t>(cd + Offsets::Instance::ClassName); auto c = memory->read_string(cn); total++; if (c == "Players") valid++; else VFAIL("Instance::ClassName"); } catch (...) { total++; VFAIL("Instance::ClassName"); }
        }

        // Humanoid
        if (local_player) { try { auto m = rbx::player_t(local_player).get_model_instance(); if (m.address) { auto h = rbx::instance_t(m.address).find_first_child("Humanoid"); if (h.address) { VFLOAT("Humanoid::Health", h.address, Offsets::Humanoid::Health, 0.0f, 1000000.0f); VFLOAT("Humanoid::MaxHealth", h.address, Offsets::Humanoid::MaxHealth, 0.1f, 1000000.0f); VFLOAT("Humanoid::Walkspeed", h.address, Offsets::Humanoid::Walkspeed, 0.0f, 10000.0f); VFLOAT("Humanoid::JumpPower", h.address, Offsets::Humanoid::JumpPower, 0.0f, 10000.0f); } } } catch (...) {} }

        // TaskScheduler
        try { auto ts = memory->read<std::uint64_t>(memory->get_module_address() + Offsets::TaskScheduler::Pointer); total++; if (ts) valid++; else VFAIL("TaskScheduler::Pointer"); } catch (...) { total++; VFAIL("TaskScheduler::Pointer"); }

        #undef VFAIL
        #undef VCHECK
        #undef VREAD
        #undef VFLOAT

        char summary[64];
        snprintf(summary, sizeof(summary), "=== %d/%d offsets valid ===\n", valid, total);
        settings::globals::offset_validation_result = "\n=== Offset Validation ===\n" + fails + summary;
        printf("%s", settings::globals::offset_validation_result.c_str());
    }

    std::thread(cache::run).detach();
    std::this_thread::sleep_for(std::chrono::milliseconds(25));

    if (!InitializeStorage())
        return fail("natware couldn't start: failed to initialize storage.");

    // Roblox is ready: the card goes, and the overlay is click-through until the menu opens.
    render->running = false;
    SetWindowLong(render->detail->window, GWL_EXSTYLE, WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_LAYERED);

    if (settings::app_mode == settings::MODE_EXTERNAL)
    {
        std::thread(AutoRescanHandler).detach();
        std::thread(aimbot::run).detach();
        std::thread(silentaim::run).detach();
        std::thread(triggerbot::run_aimbot).detach();
        std::thread(triggerbot::run_silentaim).detach();
        std::thread(rage::hitsounds_detector_thread).detach();
        std::thread(rage::hittracers_detector_thread).detach();
        std::thread(rage::orbit::run).detach();
        std::thread(rage::rapidfire::run).detach();
        std::thread(rage::hitbox_expander::run).detach();
        std::thread(rage::spin360::run).detach();
        std::thread(rage::noclip::run).detach();
        std::thread(rage::hipheight::run).detach();
        std::thread(movement::run).detach();
        std::thread(movement::gravity::run).detach();
        std::thread(lighting::fog::run).detach();
        std::thread(lighting::exposure::run).detach();
        std::thread(lighting::clocktime::run).detach();
        std::thread(lighting::shadows::run).detach();
        std::thread(lighting::skybox::run).detach();
        std::thread(exploits::headless::run).detach();
        std::thread(exploits::korblox::run).detach();
        std::thread(exploits::antiafk::run).detach();
        std::thread(exploits::fpscaps::run).detach();
        std::thread(exploits::freezeplayer::run).detach();
        engine_chams::initialize();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(25));

    // Your settings from last time come back first. The "on startup" config is only used
    // when there's nothing saved yet (the first run).
    if (settings::app_mode == settings::MODE_EXTERNAL)
    {
        if (!config::restore_last_settings() && !external_config::autoload_config.empty()
            && config::config_exists(external_config::autoload_config))
            config::load_config(external_config::autoload_config);
        g_settings_restored = true;
    }

    static auto last_pid_check = std::chrono::steady_clock::now();

    while (true)
    {
        if (settings::globals::quit_requested)
            quit_now();

        // Check if Roblox is still running — if it closed, wait for it to come back
        if (std::chrono::steady_clock::now() - last_pid_check > std::chrono::milliseconds(500))
        {
            DWORD exit_code = 0;
            HANDLE proc = memory->get_process_handle();
            if (proc == nullptr || proc == INVALID_HANDLE_VALUE ||
                (GetExitCodeProcess(proc, &exit_code) && exit_code != STILL_ACTIVE))
            {
                if (!settings::menu::stay_loaded)
                {
                    printf("Roblox closed. Unloading.\n");
                    quit_now();
                }
                printf("Roblox closed. Waiting for it to reopen...\n");
                memory->detach();

                game::datamodel = {};
                game::visengine = {};
                game::workspace = {};
                game::players = {};
                game::local_player = {};
                game::local_character = {};
                game::camera = 0;
                game::roblox_window = nullptr;
                game::window_offset_x = 0.0f;
                game::window_offset_y = 0.0f;
                game::is_phantom_forces = false;
                game::is_murder_mystery_2 = false;
                game::is_lumber_tycoon_2 = false;

                {
                    std::lock_guard<std::mutex> lock(cache::mtx);
                    cache::cached_players.clear();
                    cache::cached_local_player = {};
                }

                // The overlay is click-through while the game runs; the waiting card needs the
                // mouse (to drag it and to close natware), so it takes clicks until Roblox is back.
                const LONG_PTR overlay_style = GetWindowLongPtr(render->detail->window, GWL_EXSTYLE);
                SetWindowLongPtr(render->detail->window, GWL_EXSTYLE, WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_LAYERED);

                BINARY_NAME = nullptr;
                while (true) {
                    for (int i = 0; PROCESS_NAMES[i]; i++) {
                        if (memory->find_process_id(PROCESS_NAMES[i])) {
                            BINARY_NAME = PROCESS_NAMES[i];
                            break;
                        }
                    }
                    if (BINARY_NAME) break;
                    wait_for("Waiting for Roblox to reopen", 500);
                }
                printf("Roblox found! (%s)\n", BINARY_NAME);

                while (!memory->attach_to_process(BINARY_NAME))
                    wait_for("Reconnecting to Roblox", 500);
                printf("Re-attached to Roblox!\n");

                while (!memory->find_module_address(BINARY_NAME))
                    wait_for("Reconnecting to Roblox", 500);
                printf("Module found!\n");

                robloxVersion = OffsetLoader::GetRobloxVersionFromProcess(memory->get_process_handle(), memory->get_process_id());
                fetchVersion = robloxVersion.empty() ? Offsets::ClientVersion : robloxVersion;
                OffsetLoader::FetchOffsetsFromWeb(fetchVersion);
                OffsetLoader::FetchFFlagsFromWeb(fetchVersion);

                fake_datamodel = 0;
                while (true) {
                    fake_datamodel = memory->read<std::uint64_t>(memory->get_module_address() + Offsets::FakeDataModel::Pointer);
                    if (fake_datamodel != 0) {
                        game::datamodel = rbx::instance_t(memory->read<std::uint64_t>(fake_datamodel + Offsets::FakeDataModel::RealDataModel));
                        if (game::datamodel.address != 0)
                            break;
                    }
                    wait_for("Waiting for Roblox to load", 500);
                }
                game::visengine = {memory->read<std::uint64_t>(memory->get_module_address() + Offsets::VisualEngine::Pointer)};
                game::players = game::datamodel.find_first_child_by_class("Players");
                SetWindowLongPtr(render->detail->window, GWL_EXSTYLE, overlay_style);
                printf("Roblox re-initialized!\n");
            }
            last_pid_check = std::chrono::steady_clock::now();
        }

        // Configs dropped into or replaced in the configs folder apply without a restart, menu
        // open or not.
        config::watch();
        config::autosave();

        render->start_render();

        if (!should_render_ui())
        {
            render->end_render();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        render->render_visuals();

        if (render->running || Menu::IsClosing())
        {
            render->render_menu();
        }

        Menu::DrawUpdatePrompt();   // a new version came out while natware was open

        render->end_render();
        pace_frame();
    }

    return 0;
}
