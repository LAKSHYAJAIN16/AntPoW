#include "consensus.hpp"

#include <algorithm>

namespace antchain_node {

U256 quality_target(const U256& t0, double lam, double f_old, double f_x) {
    if (f_old <= 0.0) return t0;
    double improvement = std::max(0.0, (f_old - f_x) / f_old);
    double multiplier = 1.0 + lam * improvement;
    multiplier = std::min(multiplier, 1.0 + lam);
    U256 target = U256::from_double_clamped(t0.to_double() * multiplier);
    return std::min(target, max_target());
}

U256 retarget(const U256& prev_target, double actual_seconds, double expected_seconds) {
    if (actual_seconds <= 0.0) actual_seconds = 0.001;
    double ratio = actual_seconds / expected_seconds;
    ratio = std::max(0.25, std::min(4.0, ratio));
    U256 new_target = U256::from_double_clamped(prev_target.to_double() * ratio);
    if (new_target.is_zero()) new_target = U256::from_u64(1);
    return std::min(new_target, max_target());
}

} // namespace antchain_node
