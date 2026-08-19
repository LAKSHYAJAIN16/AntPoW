#pragma once
#include <string>
#include <vector>

namespace antchain {

// Targets are expressed as per-attempt win probabilities p in (0,1], exactly
// mirroring sim/antchain_sim/consensus.py -- p = T / 2**256 is the quantity
// that matters, so working with p directly avoids big-integer bookkeeping in
// the simulator. (The real 256-bit-integer version lives in
// node/antchain_node/consensus.py, for the actual toy blockchain.)
struct ConsensusParams {
    std::string mode;          // "sha_pow" | "pure_poao" | "hybrid"
    double p0 = 2e-4;          // base per-attempt win probability (T0)
    double lam = 1.0;          // lambda: max multiplier is (1 + lambda)
    double t_opt_frac = 1.10;  // quality gate: f(x) <= t_opt_frac * f_reference
};

// T(x) = T0 * (1 + lambda * (f_old - f(x)) / f_old), clipped to
// [T0, T0*(1+lambda)].
double quality_target(double p0, double lam, double f_old, double f_x);

// P(>=1 success in n_attempts iid Bernoulli(p) trials); n_attempts may be
// fractional (treated as an expected-attempts rate for a small time slice).
double win_probability_this_tick(double p_per_attempt, double n_attempts);

// Bitcoin-style retargeting of p0 to hold expected block time constant,
// independent of the optimization component.
class DifficultyAdjuster {
public:
    explicit DifficultyAdjuster(double target_block_time, int window = 10, double p0_init = 2e-4);
    void record_block_time(double block_time);
    double p0;

private:
    double target_block_time_;
    int window_;
    std::vector<double> recent_times_;
};

} // namespace antchain
