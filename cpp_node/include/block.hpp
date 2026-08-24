#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "json.hpp"
#include "transaction.hpp"

namespace antchain_node {

constexpr const char* kCoinbaseSender = "COINBASE";

// Block hash commits to prev_hash, transactions (via tx_root), the miner's
// TSP tour and its length, the miner address, and the nonce -- the
// h = H(prev || x || f(x) || nonce) construction from the paper. Mirrors
// node/antchain_node/block.py.
struct Block {
    uint64_t index = 0;
    std::string prev_hash;              // hex
    double timestamp = 0.0;
    std::vector<Transaction> transactions;
    std::vector<int> tour;
    double f_x = 0.0;
    double f_ref = 0.0;
    std::string miner_address;
    double reward = 0.0;
    uint64_t nonce = 0;
    std::string hash;                   // hex, empty until finalize()

    std::string tx_root() const;
    Json header_payload() const;
    std::string compute_hash() const;
    void finalize();

    Json to_json() const;
    static Block from_json(const Json& j);
};

Block genesis_block();

} // namespace antchain_node
