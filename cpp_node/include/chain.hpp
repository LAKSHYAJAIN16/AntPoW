#pragma once
#include <cstdint>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "block.hpp"
#include "consensus.hpp"
#include "u256.hpp"

namespace antchain_node {

class ChainError : public std::runtime_error {
public:
    explicit ChainError(const std::string& msg) : std::runtime_error(msg) {}
};

// Validation, ledger state (balances/nonces), difficulty history, and JSON
// persistence. Single canonical chain in memory (no side-branch tree); a
// competing chain is only adopted if it has strictly higher cumulative
// work. Mirrors node/antchain_node/chain.py.
class Blockchain {
public:
    explicit Blockchain(ConsensusParams params, std::string data_path = "");

    uint64_t height() const;
    const Block& tip() const;

    // Replays retargeting from genesis over `blocks`. Returns
    // (targets used for blocks[1:], target for the block after the last).
    // The ONLY place difficulty is computed -- used identically by the live
    // single-block mining path (next_target()) and the full-chain catch-up
    // path (validate_full_chain), so they can never silently disagree.
    std::pair<std::vector<U256>, U256> compute_targets(const std::vector<Block>& blocks) const;
    U256 next_target();

    static U256 cumulative_work(const std::vector<U256>& targets);

    void validate_block(const Block& block, const Block& prev, const U256& target,
                         std::map<std::string, double>& balances, std::map<std::string, uint64_t>& nonces) const;
    // Returns (balances, nonces, targets).
    std::tuple<std::map<std::string, double>, std::map<std::string, uint64_t>, std::vector<U256>>
    validate_full_chain(const std::vector<Block>& blocks) const;

    bool try_extend(const Block& block);
    bool try_replace(const std::vector<Block>& blocks);

    double balance_of(const std::string& address);
    uint64_t next_nonce_for(const std::string& address);

    bool load();
    Json to_json_list(); // array of block JSON objects

    ConsensusParams params;
    std::vector<Block> blocks;
    std::map<std::string, double> balances;
    std::map<std::string, uint64_t> nonces;

private:
    void persist();

    std::string data_path_;
    mutable std::recursive_mutex mutex_;
};

} // namespace antchain_node
