#include "rpc.hpp"

#include <mutex>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace antchain_node {

namespace {
#ifdef _WIN32
void ensure_winsock_initialized() {
    static std::once_flag flag;
    std::call_once(flag, []() {
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
    });
}
#endif
} // namespace

Json rpc_request(const std::string& host, int port, const Json& msg, double timeout_seconds) {
#ifdef _WIN32
    ensure_winsock_initialized();
#endif
    struct addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) {
        throw std::runtime_error("could not resolve " + host);
    }

#ifdef _WIN32
    SOCKET fd = INVALID_SOCKET;
    for (auto* rp = res; rp != nullptr; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd == INVALID_SOCKET) continue;
        if (connect(fd, rp->ai_addr, static_cast<int>(rp->ai_addrlen)) == 0) break;
        closesocket(fd);
        fd = INVALID_SOCKET;
    }
    freeaddrinfo(res);
    if (fd == INVALID_SOCKET) throw std::runtime_error("connection to " + host + ":" + std::to_string(port) + " failed");
    DWORD tv = static_cast<DWORD>(timeout_seconds * 1000);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
#else
    int fd = -1;
    for (auto* rp = res; rp != nullptr; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) throw std::runtime_error("connection to " + host + ":" + std::to_string(port) + " failed");
    struct timeval tv{};
    tv.tv_sec = static_cast<long>(timeout_seconds);
    tv.tv_usec = static_cast<long>((timeout_seconds - tv.tv_sec) * 1000000);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    std::string payload = msg.dump() + "\n";
    const char* p = payload.data();
    size_t remaining = payload.size();
    while (remaining > 0) {
#ifdef _WIN32
        int n = ::send(fd, p, static_cast<int>(remaining), 0);
#else
        ssize_t n = ::send(fd, p, remaining, 0);
#endif
        if (n <= 0) {
#ifdef _WIN32
            closesocket(fd);
#else
            close(fd);
#endif
            throw std::runtime_error("failed to send request");
        }
        p += n;
        remaining -= static_cast<size_t>(n);
    }

    std::string line;
    char buf[4096];
    while (line.find('\n') == std::string::npos) {
#ifdef _WIN32
        int n = ::recv(fd, buf, sizeof(buf), 0);
#else
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
#endif
        if (n <= 0) break;
        line.append(buf, static_cast<size_t>(n));
    }
#ifdef _WIN32
    closesocket(fd);
#else
    close(fd);
#endif
    if (line.empty()) throw std::runtime_error("no response from node");
    size_t nl = line.find('\n');
    if (nl != std::string::npos) line.erase(nl);
    return Json::parse(line);
}

} // namespace antchain_node
