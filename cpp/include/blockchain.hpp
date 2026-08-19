#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "consensus.hpp"
#include "miner.hpp"

namespace antchain {

struct BlockResult {
    int block_index = 0;
    std::string mode;
    double lam = 0.0;
    int winner_id = -1;
    int block_time_ticks = 0;
    bool fork = false;
    int n_competing = 1;
    double f_winner = 0.0;
    double f_reference = 0.0;
    double total_hash_attempts = 0.0;
    double total_aco_iterations = 0.0;
    double p0 = 0.0;
};

struct ChainRunConfig {
    int n_cities = 12;
    int max_ticks = 40;
    double target_block_time = 15.0;
    double hash_attempts_per_tick_per_share = 400.0;
    double search_iters_per_tick = 20.0;
    double hybrid_search_fraction = 0.35;
    int propagation_delay_ticks = 1;
    double withhold_frac = 0.8;
    uint64_t seed = 0;
};

// Simulates n_blocks of a chain under the given consensus mode/params.
// Mirrors sim/antchain_sim/blockchain.py's run_chain() exactly in structure
// (search_fraction per mode, eligibility gating, hash-lottery tick loop,
// propagation-delay fork detection, difficulty retargeting).
std::vector<BlockResult> run_chain(std::vector<Miner>& miners, const ConsensusParams& params,
                                    int n_blocks, const ChainRunConfig& cfg);

} // namespace antchain
