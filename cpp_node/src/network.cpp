#include "network.hpp"

#include <cstdio>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
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

void close_socket(socket_t s) {
#ifdef _WIN32
    closesocket(static_cast<SOCKET>(s));
#else
    close(static_cast<int>(s));
#endif
}

// Sends the full buffer, looping over partial writes. Returns false on error.
bool send_all(socket_t s, const std::string& data) {
    const char* p = data.data();
    size_t remaining = data.size();
    while (remaining > 0) {
#ifdef _WIN32
        int n = ::send(static_cast<SOCKET>(s), p, static_cast<int>(remaining), 0);
#else
        ssize_t n = ::send(static_cast<int>(s), p, remaining, 0);
#endif
        if (n <= 0) return false;
        p += n;
        remaining -= static_cast<size_t>(n);
    }
    return true;
}

// Blocking read of one byte-chunk. Returns bytes read, 0 on orderly close,
// negative on error.
long recv_chunk(socket_t s, char* buf, size_t buflen) {
#ifdef _WIN32
    return ::recv(static_cast<SOCKET>(s), buf, static_cast<int>(buflen), 0);
#else
    return static_cast<long>(::recv(static_cast<int>(s), buf, buflen, 0));
#endif
}

socket_t connect_socket(const std::string& host, int port) {
#ifdef _WIN32
    ensure_winsock_initialized();
#endif
    struct addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    std::string port_str = std::to_string(port);
    if (getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res) != 0 || !res) return kInvalidSocket;
    socket_t s = kInvalidSocket;
    for (auto* rp = res; rp != nullptr; rp = rp->ai_next) {
#ifdef _WIN32
        SOCKET fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd == INVALID_SOCKET) continue;
        if (connect(fd, rp->ai_addr, static_cast<int>(rp->ai_addrlen)) == 0) { s = static_cast<socket_t>(fd); break; }
        closesocket(fd);
#else
        int fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) { s = static_cast<socket_t>(fd); break; }
        close(fd);
#endif
    }
    freeaddrinfo(res);
    return s;
}

} // namespace

Network::Network(std::string h, int p, Handler on_message, LogFn log)
    : host(std::move(h)), port(p), on_message_(std::move(on_message)), log_(std::move(log)) {}

Network::~Network() {
    if (server_sock_ != kInvalidSocket) close_socket(server_sock_);
}

void Network::log(const std::string& msg) const {
    if (log_) log_(msg); else std::puts(msg.c_str());
}

void Network::start() {
#ifdef _WIN32
    ensure_winsock_initialized();
    SOCKET fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCKET) throw std::runtime_error("socket() failed");
    BOOL opt = TRUE;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) throw std::runtime_error("bind() failed");
    if (listen(fd, 16) != 0) throw std::runtime_error("listen() failed");
    server_sock_ = static_cast<socket_t>(fd);
#else
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) throw std::runtime_error("socket() failed");
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) throw std::runtime_error("bind() failed");
    if (listen(fd, 16) != 0) throw std::runtime_error("listen() failed");
    server_sock_ = static_cast<socket_t>(fd);
#endif
    std::thread(&Network::accept_loop, this).detach();
    log("[net] listening on " + host + ":" + std::to_string(port));
}

void Network::accept_loop() {
    while (true) {
#ifdef _WIN32
        SOCKET fd = accept(static_cast<SOCKET>(server_sock_), nullptr, nullptr);
        if (fd == INVALID_SOCKET) return;
#else
        int fd = accept(static_cast<int>(server_sock_), nullptr, nullptr);
        if (fd < 0) return;
#endif
        std::thread(&Network::handle_conn, this, static_cast<socket_t>(fd), std::string()).detach();
    }
}

bool Network::connect_to(const std::string& h, int p) {
    std::string key = h + ":" + std::to_string(p);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (peers_.count(key)) return true;
    }
    socket_t s = connect_socket(h, p);
    if (s == kInvalidSocket) {
        log("[net] connect to " + key + " failed");
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        peers_[key] = s;
    }
    std::thread(&Network::handle_conn, this, s, key).detach();
    return true;
}

void Network::remove_peer(socket_t s) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = peers_.begin(); it != peers_.end();) {
            if (it->second == s) it = peers_.erase(it); else ++it;
        }
    }
    close_socket(s);
}

void Network::handle_conn(socket_t s, std::string key) {
    std::string buffer;
    char chunk[4096];
    while (true) {
        long n = recv_chunk(s, chunk, sizeof(chunk));
        if (n <= 0) break;
        buffer.append(chunk, static_cast<size_t>(n));
        size_t pos;
        while ((pos = buffer.find('\n')) != std::string::npos) {
            std::string line = buffer.substr(0, pos);
            buffer.erase(0, pos + 1);
            if (line.empty()) continue;
            Json msg;
            try {
                msg = Json::parse(line);
            } catch (const std::exception&) {
                continue;
            }
            try {
                static const Json kEmptyStr = Json::string("");
                if (key.empty() && msg.is_object() && msg.has("type") &&
                    msg.at("type").as_string() == "hello" && msg.has("port")) {
                    std::string peer_host = msg.get("host", kEmptyStr).as_string();
                    key = peer_host + ":" + std::to_string(msg.at("port").as_int());
                    std::lock_guard<std::mutex> lock(mutex_);
                    peers_[key] = s;
                }
                if (on_message_) on_message_(s, msg);
            } catch (const std::exception& e) {
                log(std::string("[net] error handling message from ") + (key.empty() ? "?" : key) + ": " + e.what());
            }
        }
    }
    remove_peer(s);
}

bool Network::send(socket_t s, const Json& msg) {
    std::string payload = msg.dump() + "\n";
    if (!send_all(s, payload)) {
        remove_peer(s);
        return false;
    }
    return true;
}

void Network::broadcast(const Json& msg, socket_t exclude) {
    std::vector<socket_t> targets;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& kv : peers_) {
            if (kv.second != exclude) targets.push_back(kv.second);
        }
    }
    for (socket_t s : targets) send(s, msg);
}

std::map<std::string, socket_t> Network::peers() {
    std::lock_guard<std::mutex> lock(mutex_);
    return peers_;
}

socket_t Network::peer_socket(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = peers_.find(key);
    return it == peers_.end() ? kInvalidSocket : it->second;
}

} // namespace antchain_node
