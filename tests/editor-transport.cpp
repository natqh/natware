#include "../natware/src/features/editor/nexec_bridge.h"
#include "../natware/src/features/editor/editor_diagnostics.h"
#include <Windows.h>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

static unsigned counter = 0;
static int failures = 0;
static void check(bool condition, const char* label)
{
    printf("%s %s\n", condition ? "PASS" : "FAIL", label);
    if (!condition) ++failures;
}
static std::string unique_pipe()
{
    return "\\\\.\\pipe\\natware_editor_transport_test_" + std::to_string(GetCurrentProcessId()) + "_" + std::to_string(++counter);
}
static bool exchange(const std::string& input, const char* ack, bool read_input, unsigned silence_ms, std::string& error, std::string& received)
{
    const std::string name = unique_pipe();
    HANDLE pipe = CreateNamedPipeA(name.c_str(), PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, 1, 65536, 65536, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) { check(false, "test server creates an isolated pipe"); return false; }
    std::thread server([&] {
        if (ConnectNamedPipe(pipe, nullptr) || GetLastError() == ERROR_PIPE_CONNECTED) {
            if (read_input) {
                std::vector<char> buffer(2 * 1024 * 1024);
                DWORD count = 0;
                if (ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr))
                    received.assign(buffer.data(), count);
            }
            if (silence_ms) Sleep(silence_ms);
            if (ack) {
                DWORD written = 0;
                WriteFile(pipe, ack, static_cast<DWORD>(strlen(ack)), &written, nullptr);
                FlushFileBuffers(pipe);
            }
        }
        DisconnectNamedPipe(pipe); CloseHandle(pipe);
    });
    const bool result = nexec_bridge::execute(input, error, name.c_str());
    server.join();
    return result;
}
int main()
{
    editor_diagnostics::start_crash_capture();
    std::error_code file_error;
    auto log_before = std::filesystem::file_size(editor_diagnostics::path(), file_error);
    if (file_error) log_before = 0;
    editor_diagnostics::write("info", "test.logging_with_crash_capture");
    check(std::filesystem::file_size(editor_diagnostics::path(), file_error) > log_before && !file_error, "crash capture cannot block regular diagnostic writes");
    std::string error, received;
    const auto absent = unique_pipe();
    check(!nexec_bridge::execute("print(\"friendly\")", error, absent.c_str()) && error.find("unavailable") != std::string::npos, "missing pipe returns a real connection error");
    check(!nexec_bridge::execute("", error, absent.c_str()) && error.find("empty") != std::string::npos, "empty request rejected before connecting");
    check(!nexec_bridge::execute(std::string(2 * 1024 * 1024 + 1, 'x'), error, absent.c_str()), "oversize request rejected before connecting");
    check(exchange("print(\"friendly\")", "ok", true, 0, error, received) && received == "print(\"friendly\")" && error.empty(), "exact message received and receipt acknowledged");
    check(!exchange("transport-test", "not-ok", true, 0, error, received) && error.find("unrecognized") != std::string::npos, "unrecognized acknowledgement is rejected");
    check(!exchange("transport-test", nullptr, true, 0, error, received) && error.find("failed") != std::string::npos, "disconnect before acknowledgement is reported");
    check(!exchange("transport-test", "oversized-response-without-confirmation", true, 0, error, received), "oversized acknowledgement is rejected");
    auto start = std::chrono::steady_clock::now();
    check(!exchange("transport-test", nullptr, true, 4300, error, received) && error.find("outcome is unknown") != std::string::npos, "silent backend times out with an unknown outcome");
    check(std::chrono::steady_clock::now() - start < std::chrono::seconds(6), "read timeout is bounded");
    start = std::chrono::steady_clock::now();
    check(!exchange(std::string(1024 * 1024, 'x'), nullptr, false, 4300, error, received) && error.find("timed out") != std::string::npos, "blocked write is cancelled and reported");
    check(std::chrono::steady_clock::now() - start < std::chrono::seconds(6), "write timeout is bounded");
    check(exchange("after-timeout", "ok", true, 0, error, received) && error.empty(), "transport recovers after cancelled operations");
    editor_diagnostics::stop_crash_capture();
    return failures ? 1 : 0;
}
