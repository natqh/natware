#include "editor_window.h"
#include "editor_diagnostics.h"
#include "nexec_bridge.h"
#include "nexec_loader.h"
#include "../../../ext/json/json.hpp"
#include <atomic>
#include <memory>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <cstdio>
#include <windowsx.h>
#include <wrl/client.h>
#include <wrl/event.h>
#include <WebView2.h>
#include <dwmapi.h>
#include <shlwapi.h>
#include <shellapi.h>
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;

static const wchar_t* kClassName = L"natware_editor_wnd";
static const wchar_t* kOrigin = L"https://natware.local/";
static HWND s_hwnd = nullptr;
static ComPtr<ICoreWebView2Controller> s_controller;
static ComPtr<ICoreWebView2> s_webview;
static ComPtr<ICoreWebView2Environment> s_env;
static bool s_alive = false;
static bool s_closing = false;

static constexpr UINT_PTR kCloseTimer = 1;
static constexpr UINT_PTR kNexecTimer = 2;
static constexpr UINT kPostToPage = WM_APP + 1;   // wake the current session's owned mailbox
static constexpr UINT kNexecPoll = WM_APP + 2;    // background availability result (wParam = 0/1)
static constexpr int kTopResize = 5;   // the page covers the top edge; this strip still resizes
static int s_nexec_seen = -1;          // last nexec state sent to the page; -1 sends the next one
static std::atomic<bool> s_checking{false};
struct EditorSession {
    std::mutex mutex;
    std::deque<std::string> messages;
    HWND hwnd = nullptr;
    bool open = true;
    std::atomic<bool> attaching{false};
    std::atomic<bool> executing{false};
};
static std::shared_ptr<EditorSession> s_session;
static unsigned long long s_generation = 0;

static void close_mailbox()
{
    if (!s_session) return;
    std::lock_guard<std::mutex> guard(s_session->mutex);
    s_session->open = false;
    s_session->messages.clear();
}
static bool worker_post(const std::shared_ptr<EditorSession>& session, std::string payload)
{
    std::lock_guard<std::mutex> guard(session->mutex);
    if (!session->open) {
        editor_diagnostics::write("debug", "worker.reply_dropped", {{"reason", "session closed"}});
        return false;
    }
    session->messages.push_back(std::move(payload));
    if (!PostMessageW(session->hwnd, kPostToPage, 0, 0)) {
        session->messages.pop_back();
        editor_diagnostics::write("error", "worker.post_failed", {{"win32", GetLastError()}});
        return false;
    }
    return true;
}
static bool current_window(HWND hwnd, unsigned long long generation)
{
    // All WebView callbacks run on the UI thread. A previous window may complete
    // creation or flushing after the HWND has closed or even been reused.
    return s_alive && !s_closing && s_hwnd == hwnd && s_generation == generation;
}

static bool from_page(LPCWSTR uri)
{
    return uri && wcsncmp(uri, kOrigin, wcslen(kOrigin)) == 0;
}

static std::string to_utf8(const std::wstring& w)
{
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), n, nullptr, nullptr);
    return out;
}

static std::wstring to_wide(const std::string& s)
{
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

static void post_to_page(const nlohmann::json& message)
{
    if (s_webview && !s_closing)
        editor_diagnostics::result("webview.post_message", s_webview->PostWebMessageAsJson(to_wide(message.dump()).c_str()));
}

static void send_nexec_state(bool force, const std::string& request_id = {})
{
    const int now = nexec_bridge::available() ? 1 : 0;
    if (!force && now == s_nexec_seen)
        return;
    if (now != s_nexec_seen)
        editor_diagnostics::write("info", "connection.tcp", {{"tcpAvailable", now == 1}, {"executionReady", "unknown"}});
    s_nexec_seen = now;
    post_to_page({ { "type", "nexec" }, { "connected", now == 1 }, {"requestId", request_id}, {"executionReady", "unknown"} });
}

// Execute, attach and status requests from the page. Execute runs on a worker so a slow nexec
// never stalls the window; its answer comes back through kPostToPage.
static void handle_page_request(const std::wstring& text)
{
    if (text.size() > 3 * 1024 * 1024) {
        editor_diagnostics::write("error", "request.too_large"); return;
    }
    const nlohmann::json request = nlohmann::json::parse(to_utf8(text), nullptr, false);
    if (!request.is_object() || !request.contains("type") || !request["type"].is_string()) {
        editor_diagnostics::write("warning", "request.invalid_json");
        return;
    }
    const auto field = [&](const char* key, size_t limit) -> std::string {
        if (!request.contains(key)) return {};
        if (!request[key].is_string()) throw std::invalid_argument(std::string(key) + " must be a string");
        const auto value = request[key].get<std::string>();
        if (value.size() > limit) throw std::invalid_argument(std::string(key) + " exceeds size limit");
        return value;
    };
    const std::string type = field("type", 96);
    const std::string request_id = field("requestId", 64);
    if (type == "editor:diagnostics") {
        if (!request.contains("events") || !request["events"].is_array() || request["events"].size() > 50) return;
        for (const auto& entry : request["events"]) {
            if (entry.is_object() && entry.dump().size() <= 32768) {
                const auto severity = entry.contains("level") && entry["level"].is_string() ? entry["level"].get<std::string>() : "info";
                editor_diagnostics::write(severity.c_str(), "page.diagnostic", entry);
            }
        }
        return;
    }
    editor_diagnostics::write("info", "page.request", {{"type", type}, {"requestId", request_id}});
    if (type == "editor:ready") {
        post_to_page({{"type", "editor:ready"}, {"build", __DATE__ " " __TIME__}, {"logPath", to_utf8(editor_diagnostics::path())}});
    }
    else if (type == "editor:logs") {
        const auto folder = std::filesystem::path(editor_diagnostics::path()).parent_path().wstring();
        const auto opened = reinterpret_cast<INT_PTR>(ShellExecuteW(s_hwnd, L"explore", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
        editor_diagnostics::write(opened > 32 ? "info" : "error", "logs.folder_requested", {{"shellResult", opened}});
        if (opened <= 32) post_to_page({{"type", "editor:error"}, {"error", "Windows could not open the log folder"}});
    }
    else if (type == "nexec:status")
    {
        if (!s_checking.exchange(true)) {
            const HWND target = s_hwnd;
            std::thread([target]() {
                const bool up = nexec_bridge::available();
                s_checking.store(false, std::memory_order_release);
                PostMessageW(target, kNexecPoll, up ? 1 : 0, 0);
            }).detach();
        } else {
            post_to_page({ { "type", "nexec" }, { "connected", s_nexec_seen == 1 }, {"requestId", request_id}, {"executionReady", "unknown"} });
        }
    }
    else if (type == "nexec:attach")
    {
        if (nexec_bridge::available())
        {
            send_nexec_state(true, request_id);
        }
        else if (s_session && !s_session->attaching.exchange(true))
        {
            const auto session = s_session;
            post_to_page({ { "type", "nexec:injecting" }, {"requestId", request_id} });
            try { std::thread([session, request_id]() {
                editor_diagnostics::RequestScope diagnostic_request(request_id);
                std::string error;
                const auto started = GetTickCount64();
                bool injected = false;
                bool connected = false;
                try { injected = nexec_loader::inject(error);
                if (injected)
                {
                    // Same cold-start allowance here: up to a minute for the engine's port.
                    for (int i = 0; i < 120 && !connected; ++i)
                    {
                        Sleep(500);
                        connected = nexec_bridge::available();
                    }
                    if (!connected)
                        error = "Injected but the backend didn't start";
                }
                } catch (const std::exception& exception) { error = exception.what(); }
                catch (...) { error = "Connection worker failed with an unknown exception"; }
                nlohmann::json reply{
                    { "type", "nexec:injected" },
                    { "ok", injected && connected },
                    { "error", error }, {"requestId", request_id}
                };
                editor_diagnostics::write(connected ? "info" : "error", "connection.result", {{"requestId", request_id}, {"tcpAvailable", connected}, {"error", error}, {"elapsedMs", GetTickCount64() - started}});
                session->attaching.store(false, std::memory_order_release);
                worker_post(session, reply.dump());
                nlohmann::json state{ { "type", "nexec" }, { "connected", connected } };
                worker_post(session, state.dump());
            }).detach(); }
            catch (...) { session->attaching = false; throw; }
        }
        else post_to_page({{"type", "editor:error"}, {"requestId", request_id}, {"error", "A connection request is already pending"}});
    }
    else if (type == "nexec:execute")
    {
        std::string source = field("source", 2 * 1024 * 1024);
        const std::string name = field("name", 512);
        if (!s_session || s_session->executing.exchange(true)) {
            post_to_page({{"type", "editor:error"}, {"requestId", request_id}, {"error", "An execute request is already pending"}}); return;
        }
        const auto session = s_session;
        try { std::thread([session, request_id, source = std::move(source), name]() {
            editor_diagnostics::RequestScope diagnostic_request(request_id);
            std::string error;
            const auto started = GetTickCount64();
            editor_diagnostics::write("info", "execute.started", {{"requestId", request_id}, {"bytes", source.size()}});
            bool ok = false;
            try { ok = !source.empty() && nexec_bridge::execute(source, error); }
            catch (const std::exception& exception) { error = exception.what(); }
            catch (...) { error = "Execute worker failed with an unknown exception"; }
            if (source.empty())
                error = "This tab is empty";
            nlohmann::json reply{ { "type", "nexec:executed" }, { "ok", ok }, { "name", name }, { "error", error }, {"requestId", request_id}, {"state", ok ? "tcp_sent" : "failed"}, {"executionConfirmed", false} };
            editor_diagnostics::write(ok ? "warning" : "error", "execute.result", {{"requestId", request_id}, {"state", ok ? "tcp_sent" : "failed"}, {"executionConfirmed", false}, {"error", error}, {"elapsedMs", GetTickCount64() - started}});
            session->executing.store(false, std::memory_order_release);
            worker_post(session, reply.dump());
        }).detach(); }
        catch (...) { session->executing = false; throw; }
    }
    else editor_diagnostics::write("warning", "request.unknown_type", {{"type", type}});
}

// The page's files live inside the exe (editor_assets.rc) and are served from memory.
struct Asset { const wchar_t* path; const wchar_t* resource; const wchar_t* type; };
static const Asset kAssets[] = {
    { L"editor.html",   L"EDITOR_HTML",   L"text/html; charset=utf-8" },
    { L"workspace.css", L"WORKSPACE_CSS", L"text/css; charset=utf-8" },
    { L"workspace.js",  L"WORKSPACE_JS",  L"text/javascript; charset=utf-8" },
    { L"diagnostics.js", L"DIAGNOSTICS_JS", L"text/javascript; charset=utf-8" },
    { L"glass.js",      L"GLASS_JS",      L"text/javascript; charset=utf-8" },
    { L"dark-veil.js",  L"DARK_VEIL_JS",  L"text/javascript; charset=utf-8" },
    { L"brand.ttf",     L"BRAND_TTF",     L"font/ttf" },
};

static void serve_asset(ICoreWebView2WebResourceRequestedEventArgs* args)
{
    ComPtr<ICoreWebView2WebResourceRequest> request;
    LPWSTR uri = nullptr;
    if (FAILED(args->get_Request(&request)) || FAILED(request->get_Uri(&uri)) || !from_page(uri))
    {
        CoTaskMemFree(uri);
        return;
    }
    std::wstring path(uri + wcslen(kOrigin));
    CoTaskMemFree(uri);
    path = path.substr(0, path.find_first_of(L"?#"));

    ComPtr<ICoreWebView2WebResourceResponse> response;
    for (const Asset& a : kAssets)
    {
        if (path != a.path)
            continue;
        HMODULE module = GetModuleHandleW(nullptr);
        HRSRC found = FindResourceW(module, a.resource, MAKEINTRESOURCEW(10));   // RT_RCDATA
        HGLOBAL loaded = found ? LoadResource(module, found) : nullptr;
        const void* bytes = loaded ? LockResource(loaded) : nullptr;
        if (!bytes) {
            editor_diagnostics::write("error", "asset.missing", {{"path", to_utf8(path)}, {"win32", GetLastError()}});
            break;
        }
        ComPtr<IStream> stream;
        stream.Attach(SHCreateMemStream(static_cast<const BYTE*>(bytes), SizeofResource(module, found)));
        const std::wstring headers = std::wstring(L"Content-Type: ") + a.type + L"\r\nCache-Control: no-cache";
        editor_diagnostics::result("asset.response", s_env->CreateWebResourceResponse(stream.Get(), 200, L"OK", headers.c_str(), &response));
        break;
    }
    if (!response) {
        editor_diagnostics::write("error", "asset.not_found", {{"path", to_utf8(path)}});
        s_env->CreateWebResourceResponse(nullptr, 404, L"Not Found", L"", &response);
    }
    args->put_Response(response.Get());
}

// The editor's storage (scripts, tabs, favourites) lives in data\webview beside the exe.
// config::organize_folders moves the old <exe>.WebView2 folder there; if that folder was busy,
// it is used where it is.
static std::wstring user_data_folder()
{
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring dir(exe);
    dir.resize(dir.find_last_of(L'\\') + 1);
    const std::wstring current = dir + L"data\\webview";
    const std::wstring legacy = std::wstring(exe) + L".WebView2";
    if (GetFileAttributesW(current.c_str()) == INVALID_FILE_ATTRIBUTES && GetFileAttributesW(legacy.c_str()) != INVALID_FILE_ATTRIBUTES)
        return legacy;
    if (!CreateDirectoryW((dir + L"data").c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
        editor_diagnostics::write("error", "webview.profile_directory_failed", {{"win32", GetLastError()}});
    editor_diagnostics::write("info", "webview.profile", {{"path", to_utf8(current)}});
    return current;
}

static void finish_close(HWND hwnd)
{
    editor_diagnostics::write("info", "window.finish_close");
    s_closing = true;
    close_mailbox();
    KillTimer(hwnd, kCloseTimer);
    KillTimer(hwnd, kNexecTimer);
    s_alive = false;
    DestroyWindow(hwnd);
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_NCCALCSIZE:
        // No caption: the page draws its own title bar. The frame keeps its side and bottom
        // resize borders (invisible on Windows 10/11) and its shadow; only the caption goes.
        if (wp)
        {
            auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lp);
            const LONG top = params->rgrc[0].top;
            DefWindowProcW(hwnd, msg, wp, lp);
            params->rgrc[0].top = top;
            if (IsZoomed(hwnd))
                params->rgrc[0].top += GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
            return 0;
        }
        break;

    case WM_NCHITTEST:
    {
        // Reached for the frame and for the page's drag region (app-region: drag), which
        // WebView2 hands to the host. The top few pixels resize; the rest of the region drags.
        const LRESULT hit = DefWindowProcW(hwnd, msg, wp, lp);
        if (hit != HTCLIENT)
            return hit;
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(hwnd, &pt);
        RECT rc;
        GetClientRect(hwnd, &rc);
        if (pt.y < kTopResize && !IsZoomed(hwnd))
            return pt.x < kTopResize * 3 ? HTTOPLEFT : (pt.x >= rc.right - kTopResize * 3 ? HTTOPRIGHT : HTTOP);
        return HTCLIENT;
    }

    case WM_GETMINMAXINFO:
    {
        auto* bounds = reinterpret_cast<MINMAXINFO*>(lp);
        bounds->ptMinTrackSize = { 850, 600 };
        return 0;
    }

    case WM_SIZE:
        if (s_controller)
        {
            RECT rc;
            GetClientRect(hwnd, &rc);
            s_controller->put_Bounds(rc);
        }
        break;

    case WM_MOVE:
    case WM_MOVING:
        if (s_controller)
            s_controller->NotifyParentWindowPositionChanged();
        break;

    case WM_CLOSE:
        // Let the page write its last edits to storage before the WebView goes away; the timer
        // closes anyway if the page never answers.
        if (s_closing) return 0;
        editor_diagnostics::write("info", "window.close_requested");
        if (s_webview)
        {
            s_closing = true;
            close_mailbox();
            KillTimer(hwnd, kNexecTimer);
            const auto generation = s_generation;
            SetTimer(hwnd, kCloseTimer, 1500, nullptr);
            const HRESULT flush = s_webview->ExecuteScript(L"window.natwareFlush ? window.natwareFlush() : false",
                Callback<ICoreWebView2ExecuteScriptCompletedHandler>([hwnd, generation](HRESULT hr, LPCWSTR result) -> HRESULT {
                    editor_diagnostics::write(FAILED(hr) || !result || wcscmp(result, L"true") ? "warning" : "info", "window.flush_result",
                        {{"hresult", editor_diagnostics::hex(hr)}, {"saved", result && wcscmp(result, L"true") == 0}});
                    if (s_hwnd == hwnd && s_generation == generation)
                        finish_close(hwnd);
                    return S_OK;
                }).Get());
            if (FAILED(flush)) { editor_diagnostics::result("window.flush_request_failed", flush); finish_close(hwnd); }
            return 0;
        }
        finish_close(hwnd);
        return 0;

    case WM_TIMER:
        if (wp == kCloseTimer)
        {
            editor_diagnostics::write("warning", "window.flush_timeout", {{"workspaceSaved", "unknown"}});
            finish_close(hwnd);
            return 0;
        }
        if (wp == kNexecTimer)
        {
            if (!s_checking.exchange(true)) {
                const HWND target = hwnd;
                std::thread([target]() {
                    const bool up = nexec_bridge::available();
                    s_checking.store(false, std::memory_order_release);
                    PostMessageW(target, kNexecPoll, up ? 1 : 0, 0);
                }).detach();
            }
            return 0;
        }
        break;

    case kPostToPage:
    {
        std::deque<std::string> messages;
        if (s_session) {
            std::lock_guard<std::mutex> guard(s_session->mutex);
            messages.swap(s_session->messages);
        }
        for (const auto& message : messages)
            if (s_webview && !s_closing)
                editor_diagnostics::result("worker.deliver", s_webview->PostWebMessageAsJson(to_wide(message).c_str()));
        return 0;
    }

    case kNexecPoll:
    {
        const int now = static_cast<int>(wp);
        if (now != s_nexec_seen)
            editor_diagnostics::write("info", "connection.tcp", {{"tcpAvailable", now == 1}, {"executionReady", "unknown"}});
        s_nexec_seen = now;
        post_to_page({ { "type", "nexec" }, { "connected", now == 1 }, {"requestId", ""}, {"executionReady", "unknown"} });
        return 0;
    }

    case WM_DESTROY:
        close_mailbox();
        s_alive = false;
        s_closing = true;
        editor_diagnostics::write("info", "window.destroyed");
        s_hwnd = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

namespace natware_editor {

bool create()
{
    if (s_alive && s_hwnd) return true;
    ++s_generation;
    s_closing = false;
    editor_diagnostics::start_crash_capture();
    editor_diagnostics::write("info", "editor.create", {{"build", __DATE__ " " __TIME__}, {"generation", s_generation}, {"crashLogPath", to_utf8(editor_diagnostics::crash_path())}});
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    static HBRUSH background = CreateSolidBrush(RGB(10, 10, 10));
    wc.hbrBackground = background;
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        editor_diagnostics::write("error", "window.register_failed", {{"win32", GetLastError()}}); return false;
    }

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int ww = 1160, wh = 760;
    s_hwnd = CreateWindowExW(WS_EX_APPWINDOW, kClassName, L"natware",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX,
        (sw - ww) / 2, (sh - wh) / 2, ww, wh,
        nullptr, nullptr, wc.hInstance, nullptr);

    if (!s_hwnd) { editor_diagnostics::write("error", "window.create_failed", {{"win32", GetLastError()}}); return false; }
    const BOOL dark_caption = TRUE;
    DwmSetWindowAttribute(s_hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark_caption, sizeof(dark_caption));
    // Windows 11: rounded corners and a quiet rim, like the menu's shell (white at 8% on black).
    const DWORD corners = 2 /* DWMWCP_ROUND */;
    DwmSetWindowAttribute(s_hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &corners, sizeof(corners));
    const COLORREF rim = RGB(30, 30, 33);
    DwmSetWindowAttribute(s_hwnd, 34 /* DWMWA_BORDER_COLOR */, &rim, sizeof(rim));
    // Recompute the frame now that WM_NCCALCSIZE drops the caption.
    SetWindowPos(s_hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    ShowWindow(s_hwnd, SW_SHOW);
    UpdateWindow(s_hwnd);
    s_alive = true;
    s_closing = false;
    s_session = std::make_shared<EditorSession>();
    s_session->hwnd = s_hwnd;
    s_nexec_seen = -1;
    SetTimer(s_hwnd, kNexecTimer, 3000, nullptr);

    const HWND hwnd = s_hwnd;
    const auto generation = s_generation;
    auto env_cb = Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
        [hwnd, generation](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
            editor_diagnostics::result("webview.environment_created", hr);
            if (!current_window(hwnd, generation)) return S_OK;
            if (FAILED(hr) || !env) {
                MessageBoxW(hwnd, L"The editor could not start WebView2. See natware_editor.log in your temporary folder for details.", L"Editor startup failed", MB_OK | MB_ICONERROR);
                finish_close(hwnd); return S_OK;
            }
            s_env = env;
            LPWSTR version = nullptr;
            if (SUCCEEDED(env->get_BrowserVersionString(&version)) && version)
                editor_diagnostics::write("info", "webview.version", {{"version", to_utf8(version)}});
            CoTaskMemFree(version);
            const HRESULT create_controller = env->CreateCoreWebView2Controller(hwnd,
                Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                    [hwnd, generation](HRESULT hr, ICoreWebView2Controller* ctrl) -> HRESULT {
                        editor_diagnostics::result("webview.controller_created", hr);
                        if (!current_window(hwnd, generation)) { if (ctrl) ctrl->Close(); return S_OK; }
                        if (FAILED(hr) || !ctrl) {
                            MessageBoxW(hwnd, L"The editor browser could not be created. See natware_editor.log for details.", L"Editor startup failed", MB_OK | MB_ICONERROR);
                            finish_close(hwnd); return S_OK;
                        }
                        s_controller = ctrl;
                        const HRESULT core = ctrl->get_CoreWebView2(&s_webview);
                        editor_diagnostics::result("webview.get_core", core);
                        if (FAILED(core) || !s_webview) { finish_close(hwnd); return S_OK; }

                        // The shell colour until the page paints, so opening never flashes white.
                        ComPtr<ICoreWebView2Controller2> ctrl2;
                        if (SUCCEEDED(s_controller.As(&ctrl2)))
                            ctrl2->put_DefaultBackgroundColor(COREWEBVIEW2_COLOR{ 255, 10, 10, 10 });

                        RECT rc; GetClientRect(s_hwnd, &rc);
                        ctrl->put_Bounds(rc);

                        ICoreWebView2Settings* settings = nullptr;
                        s_webview->get_Settings(&settings);
                        if (settings) {
                            settings->put_AreDefaultContextMenusEnabled(FALSE);
                            settings->put_IsStatusBarEnabled(FALSE);
                            settings->put_AreDevToolsEnabled(FALSE);
                            // The window controls and nexec requests talk to the host (see WebMessageReceived).
                            settings->put_IsWebMessageEnabled(TRUE);
                            // The page's title bar drags the window (CSS app-region: drag).
                            ComPtr<ICoreWebView2Settings9> settings9;
                            if (SUCCEEDED(settings->QueryInterface(IID_PPV_ARGS(&settings9))))
                                settings9->put_IsNonClientRegionSupportEnabled(TRUE);
                            settings->Release();
                        }

                        // Window controls drawn by the page, plus JSON requests for nexec.
                        editor_diagnostics::result("webview.message_handler", s_webview->add_WebMessageReceived(
                            Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                [](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                    try {
                                    LPWSTR source = nullptr;
                                    args->get_Source(&source);
                                    const bool ours = from_page(source);
                                    CoTaskMemFree(source);
                                    LPWSTR text = nullptr;
                                    if (!ours || FAILED(args->TryGetWebMessageAsString(&text)) || !text)
                                        return S_OK;
                                    if (wcscmp(text, L"window:minimize") == 0)
                                        ShowWindow(s_hwnd, SW_MINIMIZE);
                                    else if (wcscmp(text, L"window:close") == 0)
                                        PostMessageW(s_hwnd, WM_CLOSE, 0, 0);
                                    else if (text[0] == L'{') {
                                        const std::wstring request(text);
                                        CoTaskMemFree(text);
                                        text = nullptr;
                                        handle_page_request(request);
                                    }
                                    CoTaskMemFree(text);
                                    } catch (const std::exception& error) {
                                        editor_diagnostics::write("error", "page.request_exception", {{"error", error.what()}});
                                        post_to_page({{"type", "editor:error"}, {"error", "The editor host rejected an invalid request. See diagnostics."}});
                                    } catch (...) {
                                        editor_diagnostics::write("error", "page.request_exception");
                                    }
                                    return S_OK;
                                }).Get(), nullptr));

                        editor_diagnostics::result("webview.navigation_handler", s_webview->add_NavigationCompleted(
                            Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                [](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                                    BOOL success = FALSE; COREWEBVIEW2_WEB_ERROR_STATUS status{};
                                    args->get_IsSuccess(&success); args->get_WebErrorStatus(&status);
                                    editor_diagnostics::write(success ? "info" : "error", "webview.navigation_completed", {{"success", success != FALSE}, {"webErrorStatus", static_cast<int>(status)}});
                                    return S_OK;
                                }).Get(), nullptr));
                        editor_diagnostics::result("webview.process_handler", s_webview->add_ProcessFailed(
                            Callback<ICoreWebView2ProcessFailedEventHandler>(
                                [](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs* args) -> HRESULT {
                                    COREWEBVIEW2_PROCESS_FAILED_KIND kind{};
                                    args->get_ProcessFailedKind(&kind);
                                    editor_diagnostics::write("error", "webview.process_failed", {{"kind", static_cast<int>(kind)}});
                                    if (kind == COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED || kind == COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED)
                                        MessageBoxW(s_hwnd, L"The editor browser process stopped. Diagnostic details were recorded in natware_editor.log. Reopen the editor to restore the last saved workspace.", L"Editor browser stopped", MB_OK | MB_ICONERROR);
                                    return S_OK;
                                }).Get(), nullptr));

                        // Paste reads the clipboard; allow that for the editor page only.
                        s_webview->add_PermissionRequested(
                            Callback<ICoreWebView2PermissionRequestedEventHandler>(
                                [](ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs* args) -> HRESULT {
                                    COREWEBVIEW2_PERMISSION_KIND kind{};
                                    args->get_PermissionKind(&kind);
                                    LPWSTR uri = nullptr;
                                    args->get_Uri(&uri);
                                    if (kind == COREWEBVIEW2_PERMISSION_KIND_CLIPBOARD_READ && from_page(uri))
                                        args->put_State(COREWEBVIEW2_PERMISSION_STATE_ALLOW);
                                    CoTaskMemFree(uri);
                                    return S_OK;
                                }).Get(), nullptr);

                        s_webview->AddWebResourceRequestedFilter(L"https://natware.local/*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
                        s_webview->add_WebResourceRequested(
                            Callback<ICoreWebView2WebResourceRequestedEventHandler>(
                                [](ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* args) -> HRESULT {
                                    serve_asset(args);
                                    return S_OK;
                                }).Get(), nullptr);
                        editor_diagnostics::result("webview.navigate", s_webview->Navigate(L"https://natware.local/editor.html"));

                        return S_OK;
                    }).Get());
            editor_diagnostics::result("webview.controller_requested", create_controller);
            if (FAILED(create_controller)) finish_close(hwnd);
            return S_OK;
        });

    editor_diagnostics::write("info", "webview.environment_requested");
    const HRESULT environment = CreateCoreWebView2EnvironmentWithOptions(nullptr, user_data_folder().c_str(), nullptr, env_cb.Get());
    editor_diagnostics::result("webview.environment_request", environment);
    if (FAILED(environment)) { finish_close(hwnd); return false; }
    return true;
}

void destroy()
{
    s_alive = false;
    s_closing = true;
    close_mailbox();
    if (s_controller) editor_diagnostics::result("webview.close", s_controller->Close());
    s_controller = nullptr;
    s_webview = nullptr;
    s_env = nullptr;
    if (s_hwnd) { DestroyWindow(s_hwnd); s_hwnd = nullptr; }
    s_session.reset();
    editor_diagnostics::write("info", "editor.destroy_complete");
    editor_diagnostics::stop_crash_capture();
}

void pump()
{
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

bool is_alive() { return s_alive && s_hwnd; }

void trigger_attach()
{
    if (nexec_bridge::available()) return;
    if (!s_session || s_session->attaching.exchange(true)) return;
    const auto session = s_session;
    std::thread([session]() {
        std::string error;
        bool injected = false;
        bool connected = false;
        try {
            injected = nexec_loader::inject(error);
            if (injected) {
                // A cold first launch (Roblox attach + Luau bootstrap) can take a
                // while before the engine binds its port; poll up to a minute.
                for (int i = 0; i < 120 && !connected; ++i) {
                    Sleep(500);
                    connected = nexec_bridge::available();
                }
            }
        } catch (...) {}
        session->attaching.store(false, std::memory_order_release);
    }).detach();
}

}
