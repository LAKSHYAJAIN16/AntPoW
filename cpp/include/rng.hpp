#pragma once
#include <cstdint>

namespace antchain {

// SplitMix64: fast, deterministic, well-distributed PRNG used throughout
// this simulator for instance generation, ACO decisions, and chaining a
// block's pseudo-hash into the next block's instance seed.
//
// NOT cryptographic. This mirrors the role of hashlib.sha256() in the
// Python simulator (sim/antchain_sim/blockchain.py) purely for
// deterministic, well-mixed pseudorandom chaining -- the actual toy
// blockchain's real SHA-256/ECDSA consensus lives in node/antchain_node/,
// a separate implementation. Do not use this for anything security-sensitive.
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

// Deterministic avalanche mix, used to derive the next block's instance
// seed from the current block's (winner_id, f_winner, block_index) --
// the simulator's stand-in for H(prev_block).
inline uint64_t mix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

} // namespace antchain
