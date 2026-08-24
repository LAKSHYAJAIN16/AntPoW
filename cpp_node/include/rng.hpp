#pragma once
#include <cstdint>

namespace antchain_node {

// SplitMix64: fast, deterministic PRNG used to drive ACO's tour construction
// once a TSP instance's coordinates have already been derived from a real
// block hash (see tsp.hpp::generate_instance). Not cryptographic -- only
// used for miner-local search, never for consensus-critical values.
class SplitMix64 {
public:
    explicit SplitMix64(uint64_t seed) : state_(seed) {}

    uint64_t next_u64() {
        uint64_t z = (state_ += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    // uniform double in [0, 1)
    double next_double() {
        return static_cast<double>(next_u64() >> 11) * (1.0 / 9007199254740992.0); // 2^53
    }

    // uniform int in [0, n)
    int next_int(int n) {
        return static_cast<int>(next_u64() % static_cast<uint64_t>(n));
    }

private:
    uint64_t state_;
};

} // namespace antchain_node
