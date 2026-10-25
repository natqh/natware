#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <WinSock2.h>
#include <WS2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")

#include "nexec_bridge.h"
#include "editor_diagnostics.h"
#include <cstring>
#include <string>
#include <vector>
#include <mutex>

namespace nexec_bridge {

static constexpr const char* kHost = "127.0.0.1";
static constexpr u_short kPort = 6969;

static std::mutex s_pid_mutex;
static unsigned long s_roblox_pid = 0;

void set_pid(unsigned long pid)
{
    std::lock_guard<std::mutex> lock(s_pid_mutex);
    s_roblox_pid = pid;
}

static unsigned long get_pid()
{
    std::lock_guard<std::mutex> lock(s_pid_mutex);
    return s_roblox_pid;
}

static bool socket_ready(bool& wsa_up)
{
    if (!wsa_up)
    {
        WSADATA wsa{};
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
            return false;
        wsa_up = true;
    }
    return true;
}

static SOCKET connect_bounded(int timeout_ms, std::string& error)
{
    static bool wsa_up = false;
    if (!socket_ready(wsa_up))
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
    address.sin_port = htons(kPort);
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
        const int selected = select(0, nullptr, &writable, nullptr, &wait);
        if (selected <= 0)
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
    const int receive_timeout_ms = 5000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&receive_timeout_ms), sizeof(receive_timeout_ms));
    const int send_timeout_ms = 10000;
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&send_timeout_ms), sizeof(send_timeout_ms));
    return sock;
}

bool available()
{
    std::string ignored;
    const SOCKET probe = connect_bounded(300, ignored);
    if (probe == INVALID_SOCKET)
        return false;
    closesocket(probe);
    return true;
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

static std::string recv_response(SOCKET sock)
{
    std::string buf;
    char tmp[4096];
    for (;;)
    {
        int n = recv(sock, tmp, sizeof(tmp), 0);
        if (n <= 0)
            break;
        buf.append(tmp, static_cast<size_t>(n));
        if (buf.find("\r\n\r\n") != std::string::npos)
            break;
    }
    return buf;
}

bool execute(const std::string& source, std::string& error, const char* /*pipe_name*/)
{
    error.clear();
    if (source.empty() || source.size() > 2 * 1024 * 1024) {
        error = source.empty() ? "This tab is empty" : "Script exceeds the 2 MB request limit";
        return false;
    }

    const unsigned long pid = get_pid();
    if (pid == 0) {
        error = "Not attached — click Attach first";
        return false;
    }

    std::string connect_error;
    const SOCKET sock = connect_bounded(1500, connect_error);
    if (sock == INVALID_SOCKET)
    {
        editor_diagnostics::write("error", "http.connect_failed", {});
        error = connect_error.empty() ? "Backend is unavailable" : connect_error;
        return false;
    }

    const std::string path = "/execute?pid=" + std::to_string(pid);
    const std::string header =
        "POST " + path + " HTTP/1.1\r\n"
        "Host: 127.0.0.1:6969\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: " + std::to_string(source.size()) + "\r\n"
        "Connection: close\r\n"
        "\r\n";

    editor_diagnostics::write("info", "http.sending", {{"bytes", source.size()}});

    if (!send_all(sock, header.c_str(), header.size()) ||
        !send_all(sock, source.c_str(), source.size()))
    {
        closesocket(sock);
        error = "Backend dropped the connection while sending";
        editor_diagnostics::write("error", "http.send_failed", {});
        return false;
    }

    const std::string response = recv_response(sock);
    closesocket(sock);

    if (response.find("200") != std::string::npos)
    {
        editor_diagnostics::write("info", "http.sent", {{"bytes", source.size()}});
        return true;
    }

    const auto body_pos = response.find("\r\n\r\n");
    if (body_pos != std::string::npos)
    {
        std::string body = response.substr(body_pos + 4);
        error = body.empty() ? "Backend rejected the request" : body;
    }
    else
        error = "No response from backend";

    editor_diagnostics::write("error", "http.rejected", {{"response", response.substr(0, 200)}});
    return false;
}

}
