#pragma once
#include <vector>

namespace antchain {

// Standard Gini coefficient for a non-negative array of values (e.g. blocks
// won per miner, or hashrate share per miner).
double gini(const std::vector<double>& values);

} // namespace antchain
