#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <WinSock2.h>
#include <WS2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")

#include "nexec_bridge.h"
#include "editor_diagnostics.h"
#include "../../../ext/json/json.hpp"
#include <cstring>
#include <string>

namespace nexec_bridge {

static constexpr const char* kHost = "127.0.0.1";

static bool socket_ready()
{
    static bool wsa_up = false;
    if (!wsa_up)
    {
        WSADATA wsa{};
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
            return false;
        wsa_up = true;
    }
    return true;
}

static SOCKET connect_bounded(unsigned short port, int timeout_ms, int receive_ms, std::string& error)
{
    if (!socket_ready())
    {
        error = "Winsock failed to start";
        return INVALID_SOCKET;
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET)
    {
        error = "Transport socket failed (err=" + std::to_string(WSAGetLastError()) + ")";
        return INVALID_SOCKET;
    }

    u_long non_blocking = 1;
    ioctlsocket(sock, FIONBIO, &non_blocking);

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (InetPtonA(AF_INET, kHost, &address.sin_addr) != 1)
    {
        closesocket(sock);
        error = "Transport address is invalid";
        return INVALID_SOCKET;
    }

    if (connect(sock, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR)
    {
        const int pending = WSAGetLastError();
        if (pending != WSAEWOULDBLOCK)
        {
            closesocket(sock);
            return INVALID_SOCKET;
        }

        fd_set writable;
        FD_ZERO(&writable);
        FD_SET(sock, &writable);
        timeval wait{ timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
        if (select(0, nullptr, &writable, nullptr, &wait) <= 0)
        {
            closesocket(sock);
            return INVALID_SOCKET;
        }

        int failure = 0;
        int size = sizeof(failure);
        if (getsockopt(sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&failure), &size) == SOCKET_ERROR || failure != 0)
        {
            closesocket(sock);
            return INVALID_SOCKET;
        }
    }

    u_long blocking = 0;
    ioctlsocket(sock, FIONBIO, &blocking);
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&receive_ms), sizeof(receive_ms));
    const int send_timeout_ms = 10000;
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&send_timeout_ms), sizeof(send_timeout_ms));
    return sock;
}

static bool send_all(SOCKET sock, const char* data, size_t len)
{
    size_t sent = 0;
    while (sent < len)
    {
        const int chunk = send(sock, data + sent, static_cast<int>(len - sent), 0);
        if (chunk == SOCKET_ERROR)
            return false;
        sent += static_cast<size_t>(chunk);
    }
    return true;
}

// One request, one response (the backend closes the connection). false: nothing answered.
static bool http(unsigned short port, const char* method, const std::string& path, const std::string& body,
                 int connect_ms, int receive_ms, int& code, std::string& response_body, std::string& error)
{
    code = 0;
    response_body.clear();

    const SOCKET sock = connect_bounded(port, connect_ms, receive_ms, error);
    if (sock == INVALID_SOCKET)
        return false;

    const std::string header =
        std::string(method) + " " + path + " HTTP/1.1\r\n"
        "Host: 127.0.0.1:" + std::to_string(port) + "\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "Connection: close\r\n"
        "\r\n";

    if (!send_all(sock, header.c_str(), header.size()) || !send_all(sock, body.c_str(), body.size()))
    {
        closesocket(sock);
        error = "Backend dropped the connection while sending";
        return false;
    }

    std::string raw;
    char tmp[4096];
    for (;;)
    {
        const int n = recv(sock, tmp, sizeof(tmp), 0);
        if (n <= 0)
            break;
        raw.append(tmp, static_cast<size_t>(n));
    }
    closesocket(sock);

    if (raw.size() < 12 || raw.compare(0, 5, "HTTP/") != 0)
    {
        error = "No response from backend";
        return false;
    }
    code = std::atoi(raw.c_str() + 9);
    const auto split = raw.find("\r\n\r\n");
    if (split != std::string::npos)
        response_body = raw.substr(split + 4);
    return true;
}

bool status(unsigned short port, Status& out, int timeout_ms)
{
    out = Status{};
    out.port = port;

    int code = 0;
    std::string body, error;
    if (!http(port, "GET", "/status", "", timeout_ms, 1500, code, body, error) || code != 200)
        return false;

    const nlohmann::json json = nlohmann::json::parse(body, nullptr, false);
    if (!json.is_object())
        return false;

    const auto text = [&](const char* key) -> std::string {
        const auto it = json.find(key);
        return it != json.end() && it->is_string() ? it->get<std::string>() : std::string();
    };
    const auto flag = [&](const char* key) {
        const auto it = json.find(key);
        return it != json.end() && it->is_boolean() && it->get<bool>();
    };
    const auto number = [&](const char* key) -> unsigned long {
        const auto it = json.find(key);
        return it != json.end() && it->is_number_unsigned() ? it->get<unsigned long>() : 0;
    };

    out.up = true;
    out.attached = flag("attached");
    out.inGame = flag("inGame");
    out.injectFailed = flag("injectFailed");
    out.pid = number("pid");
    out.backendPid = number("backendPid");
    out.name = text("name");
    out.userId = text("userId");
    out.display = text("display");
    out.avatar = text("avatar");
    out.placeId = text("placeId");
    out.jobId = text("jobId");
    return true;
}

bool reinject(unsigned short port, unsigned long pid)
{
    int code = 0;
    std::string body, error;
    return http(port, "POST", "/reinject?pid=" + std::to_string(pid), "", 1500, 3000, code, body, error) && code == 200;
}

bool execute(unsigned short port, unsigned long pid, const std::string& source, std::string& error)
{
    error.clear();
    if (source.empty() || source.size() > 2 * 1024 * 1024)
    {
        error = source.empty() ? "This tab is empty" : "Script exceeds the 2 MB request limit";
        return false;
    }
    if (pid == 0)
    {
        error = "Not attached";
        return false;
    }

    editor_diagnostics::write("info", "http.sending", {{"bytes", source.size()}, {"port", port}});

    int code = 0;
    std::string body;
    if (!http(port, "POST", "/execute?pid=" + std::to_string(pid), source, 1500, 5000, code, body, error))
    {
        editor_diagnostics::write("error", "http.failed", {{"port", port}, {"error", error}});
        if (error.empty())
            error = "Backend is unavailable";
        return false;
    }

    if (code == 200)
    {
        editor_diagnostics::write("info", "http.sent", {{"bytes", source.size()}, {"port", port}});
        return true;
    }

    error = body.empty() ? "Backend rejected the request" : body;
    editor_diagnostics::write("error", "http.rejected", {{"port", port}, {"code", code}, {"response", body.substr(0, 200)}});
    return false;
}

}
