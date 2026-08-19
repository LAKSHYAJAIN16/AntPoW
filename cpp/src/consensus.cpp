#include "consensus.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace antchain {

double quality_target(double p0, double lam, double f_old, double f_x) {
    if (f_old <= 0.0) return p0;
    double improvement = std::max(0.0, (f_old - f_x) / f_old);
    double p = p0 * (1.0 + lam * improvement);
    return std::min(p, p0 * (1.0 + lam));
}

double win_probability_this_tick(double p_per_attempt, double n_attempts) {
    if (n_attempts <= 0.0 || p_per_attempt <= 0.0) return 0.0;
    double p = std::min(p_per_attempt, 0.999999);
    // 1 - (1-p)^n via log1p/expm1 for numerical stability at tiny p, exactly
    // as the Python reference implementation does.
    return -std::expm1(n_attempts * std::log1p(-p));
}

DifficultyAdjuster::DifficultyAdjuster(double target_block_time, int window, double p0_init)
    : p0(p0_init), target_block_time_(target_block_time), window_(window) {}

void DifficultyAdjuster::record_block_time(double block_time) {
    recent_times_.push_back(block_time);
    if (static_cast<int>(recent_times_.size()) >= window_) {
        double avg = std::accumulate(recent_times_.begin(), recent_times_.end(), 0.0) / recent_times_.size();
        double ratio = avg / target_block_time_;
        ratio = std::max(0.25, std::min(4.0, ratio));
        p0 = p0 * ratio;
        recent_times_.clear();
    }
}

} // namespace antchain
