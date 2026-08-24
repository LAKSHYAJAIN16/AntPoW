#pragma once
#include <cstdint>
#include <utility>
#include <vector>

#include "crypto.hpp"
#include "rng.hpp"

namespace antchain_node {

struct TSPInstance {
    int n_cities = 0;
    std::vector<double> xs, ys;   // city coordinates
    std::vector<double> dist;     // flattened n x n distance matrix

    double d(int i, int j) const { return dist[static_cast<size_t>(i) * n_cities + j]; }
    double tour_length(const std::vector<int>& tour) const;
    bool is_valid_tour(const std::vector<int>& tour) const;
};

// Derives the deterministic instance seed from a real block hash: first 8
// bytes big-endian, top bit masked off -- mirrors
// node/antchain_node/tsp.py::seed_from_hash exactly (same construction,
// different underlying hash function; see cpp_node/README.md).
uint64_t seed_from_hash(const Hash32& prev_hash);
// Convenience overload: `prev_hash_hex` is the 64-hex-char block hash string
// (as stored on Block::prev_hash / Block::hash).
uint64_t seed_from_hash_hex(const std::string& prev_hash_hex);

// Gen(H(prev_block)) -- every honest node derives the identical instance
// from the previous block's hash, so no miner has a precomputation edge.
TSPInstance generate_instance(uint64_t seed, int n_cities);

std::vector<int> nearest_neighbor_tour(const TSPInstance& inst, int start = 0);

// Cheap deterministic reference every node can recompute during validation --
// initializes f_old for the quality-weighted target.
double reference_length(const TSPInstance& inst);

// Ant Colony Optimization solver. step(n) runs n iterations and returns the
// best (tour, length) found since the last reset(); pheromone state (tau_)
// persists across calls.
class AntColonyOptimizer {
public:
    AntColonyOptimizer(const TSPInstance& inst, uint64_t seed, int n_ants = 16,
                        double alpha = 1.0, double beta = 3.0, double rho = 0.1, double q = 1.0);

    std::pair<std::vector<int>, double> step(int n_iterations);
    void reset(const TSPInstance& inst);

    std::vector<int> best_tour;
    double best_length = 1e18;

private:
    const TSPInstance* inst_;
    int n_ants_;
    double alpha_, beta_, rho_, q_;
    std::vector<double> tau_;  // n x n pheromone matrix
    std::vector<double> eta_;  // n x n visibility (1/distance) matrix
    SplitMix64 rng_;

    std::vector<int> construct_tour();
};

} // namespace antchain_node
