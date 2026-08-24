#pragma once
#include "u256.hpp"

namespace antchain_node {

// MAX_TARGET = 2^256 - 1, mirroring node/antchain_node/consensus.py exactly.
inline U256 max_target() { return U256::max(); }

struct ConsensusParams {
    int n_cities = 10;
    double t_opt_frac = 1.15;      // quality gate: f(x) <= t_opt_frac * f_reference
    double lam = 3.0;              // lambda: max target multiplier is (1 + lambda)
    int difficulty_bits = 20;      // initial target = MAX_TARGET >> difficulty_bits
    int retarget_interval = 10;    // blocks between difficulty retargets
    double target_block_time = 8.0; // seconds
    double block_reward = 50.0;
    int max_txs_per_block = 50;

    U256 initial_target() const { return max_target().shr(static_cast<unsigned>(difficulty_bits)); }
};

// T(x) = T0 * (1 + lambda * (f_old - f(x)) / f_old), clipped to
// [T0, T0*(1+lambda)]. Bounded advantage: a perfect solution never buys
// more than a (1+lambda)x easier target than plain hashing would give.
U256 quality_target(const U256& t0, double lam, double f_old, double f_x);

// Bitcoin-style retarget: if blocks came slower than expected, ease the
// target (raise it); if faster, harden it. Clamped to a 4x move per period.
U256 retarget(const U256& prev_target, double actual_seconds, double expected_seconds);

} // namespace antchain_node
