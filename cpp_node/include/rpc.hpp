#pragma once
#include <string>

#include "json.hpp"

namespace antchain_node {

// One-shot request/response helper for CLI commands (send/balance/status)
// talking to an already-running node over the same JSON-lines protocol used
// for peer gossip. Mirrors node/antchain_node/rpc.py. Throws std::runtime_error
// on connection failure or if the node closes without responding.
Json rpc_request(const std::string& host, int port, const Json& msg, double timeout_seconds = 5.0);

} // namespace antchain_node
