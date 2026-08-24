#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>

#include "json.hpp"

namespace antchain_node {

// Opaque cross-platform socket handle -- avoids leaking <winsock2.h>/
// <sys/socket.h> into every includer. Matches the width of Windows' SOCKET
// (a UINT_PTR) so it round-trips exactly on either platform.
using socket_t = uintptr_t;
constexpr socket_t kInvalidSocket = static_cast<socket_t>(-1);

// Minimal P2P layer: TCP sockets, newline-delimited JSON messages, flood
// gossip. No peer discovery beyond the static peer list given to
// connect_to, no NAT traversal, no encryption. Mirrors
// node/antchain_node/network.py.
class Network {
public:
    using Handler = std::function<void(socket_t sock, const Json& msg)>;
    using LogFn = std::function<void(const std::string&)>;

    Network(std::string host, int port, Handler on_message, LogFn log = nullptr);
    ~Network();

    void start();
    bool connect_to(const std::string& host, int port);
    bool send(socket_t sock, const Json& msg);
    void broadcast(const Json& msg, socket_t exclude = kInvalidSocket);

    std::map<std::string, socket_t> peers(); // snapshot, keyed "host:port"
    socket_t peer_socket(const std::string& key); // kInvalidSocket if absent

    const std::string host;
    const int port;

private:
    void accept_loop();
    void handle_conn(socket_t sock, std::string key); // key empty until a "hello" names it
    void remove_peer(socket_t sock);
    void log(const std::string& msg) const;

    Handler on_message_;
    LogFn log_;
    socket_t server_sock_ = kInvalidSocket;
    std::mutex mutex_;
    std::map<std::string, socket_t> peers_;
};

} // namespace antchain_node
