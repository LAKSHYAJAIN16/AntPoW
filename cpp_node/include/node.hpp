#pragma once
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "chain.hpp"
#include "mempool.hpp"
#include "miner.hpp"
#include "network.hpp"

namespace antchain_node {

// Ties together chain, mempool, miner, and network into one running node.
// Mirrors node/antchain_node/node.py.
class Node {
public:
    Node(std::string host, int port, ConsensusParams params, std::string data_path,
         std::string miner_address, bool mine, Network::LogFn log = nullptr);

    void start(const std::vector<std::pair<std::string, int>>& peers);
    void run_forever(); // blocks until the process is killed

    Blockchain chain;
    Mempool mempool;
    Network network;
    std::string miner_address;
    std::unique_ptr<Miner> miner; // null if not mining

private:
    void on_block_found(const Block& block);
    void on_message(socket_t sock, const Json& msg);
    void handle_new_block(const Json* block_json, socket_t origin);
    void handle_full_chain(const Json& blocks_json);
    void handle_new_tx(const Json* tx_json, socket_t origin);
    void log(const std::string& msg) const;

    Network::LogFn log_;
};

} // namespace antchain_node
