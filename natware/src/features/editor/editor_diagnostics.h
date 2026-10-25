#pragma once
#include <Windows.h>
#include "../../../ext/json/json.hpp"
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>
#include <utility>

// Structured editor-only diagnostics. Each line is independently readable after a crash.
namespace editor_diagnostics {
inline std::mutex mutex;
inline const ULONGLONG started = GetTickCount64();
inline unsigned long long sequence = 0;
inline thread_local std::string request_id;
struct RequestScope {
    std::string previous;
    explicit RequestScope(const std::string& id) : previous(std::move(request_id)) { request_id = id; }
    ~RequestScope() { request_id = std::move(previous); }
};
inline HANDLE crash_file = INVALID_HANDLE_VALUE;
inline LPTOP_LEVEL_EXCEPTION_FILTER previous_filter = nullptr;
inline std::wstring path()
{
    wchar_t folder[MAX_PATH]{};
    const DWORD size = GetTempPathW(MAX_PATH, folder);
    return size && size < MAX_PATH ? std::wstring(folder) + L"natware_editor.log" : L"natware_editor.log";
}
inline std::wstring crash_path()
{
    return (std::filesystem::path(path()).parent_path() / L"natware_editor_crash.log").wstring();
}
inline std::string hex(unsigned long value)
{
    char text[16]{}; sprintf_s(text, "0x%08lX", value); return text;
}
inline void write(const char* level, const char* event, const nlohmann::json& details = {}) noexcept
{
    try {
        std::lock_guard<std::mutex> guard(mutex);
        SYSTEMTIME time{}; GetSystemTime(&time);
        char timestamp[40]{};
        sprintf_s(timestamp, "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ", time.wYear, time.wMonth,
            time.wDay, time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
        const std::string line = nlohmann::json{
            {"at", timestamp}, {"level", level}, {"event", event}, {"details", details},
            {"pid", GetCurrentProcessId()}, {"tid", GetCurrentThreadId()}, {"seq", ++sequence},
            {"session", std::to_string(GetCurrentProcessId()) + "-" + std::to_string(started)}, {"requestId", request_id},
            {"elapsedMs", GetTickCount64() - started}
        }.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace) + "\n";
        OutputDebugStringA(line.c_str());
        const auto destination = path();
        std::error_code error;
        if (std::filesystem::file_size(destination, error) >= 2 * 1024 * 1024 && !error) {
            // Keep the previous three logs; never allow diagnostics to fill the disk.
            for (int index = 3; index >= 1; --index) {
                const auto from = index == 1 ? destination : destination + L"." + std::to_wstring(index - 1);
                const auto to = destination + L"." + std::to_wstring(index);
                MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING);
            }
        }
        FILE* file = nullptr;
        if (_wfopen_s(&file, destination.c_str(), L"ab") == 0 && file) {
            fwrite(line.data(), 1, line.size(), file);
            fclose(file);
        } else OutputDebugStringA("natware: could not write native editor diagnostics\n");
    } catch (...) { OutputDebugStringA("natware: diagnostic serialization failed\n"); }
}
inline void result(const char* event, HRESULT hr) noexcept
{
    try { write(FAILED(hr) ? "error" : "debug", event, {{"hresult", hex(static_cast<unsigned long>(hr))}}); }
    catch (...) { }
}
// Last-resort logging avoids the normal JSON allocator and logger mutex, which
// may themselves be involved in a fatal exception. Normal crash behavior is preserved.
inline LONG WINAPI fatal_exception(EXCEPTION_POINTERS* exception)
{
    SYSTEMTIME time{}; GetSystemTime(&time);
    char line[512]{};
    const int size = sprintf_s(line,
        "{\"at\":\"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ\",\"level\":\"fatal\",\"event\":\"native.unhandled_exception\",\"pid\":%lu,\"tid\":%lu,\"code\":\"0x%08lX\",\"address\":\"%p\"}\n",
        time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, time.wMilliseconds,
        GetCurrentProcessId(), GetCurrentThreadId(), exception && exception->ExceptionRecord ? exception->ExceptionRecord->ExceptionCode : 0,
        exception && exception->ExceptionRecord ? exception->ExceptionRecord->ExceptionAddress : nullptr);
    if (size > 0) {
        OutputDebugStringA(line);
        if (crash_file != INVALID_HANDLE_VALUE) {
            DWORD written = 0; WriteFile(crash_file, line, static_cast<DWORD>(size), &written, nullptr);
            FlushFileBuffers(crash_file);
        }
    }
    return previous_filter && previous_filter != fatal_exception ? previous_filter(exception) : EXCEPTION_CONTINUE_SEARCH;
}
inline void start_crash_capture()
{
    if (crash_file != INVALID_HANDLE_VALUE) return;
    // A separate pre-opened file cannot block the CRT log writer or become an
    // unlinked rotation backup during a long-running session.
    crash_file = CreateFileW(crash_path().c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    previous_filter = SetUnhandledExceptionFilter(fatal_exception);
}
inline void stop_crash_capture()
{
    SetUnhandledExceptionFilter(previous_filter);
    if (crash_file != INVALID_HANDLE_VALUE) { CloseHandle(crash_file); crash_file = INVALID_HANDLE_VALUE; }
    previous_filter = nullptr;
}
}
