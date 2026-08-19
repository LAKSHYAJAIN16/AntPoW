#pragma once
#include <cstdint>
#include <utility>
#include <vector>

#include "rng.hpp"

namespace antchain {

struct TSPInstance {
    int n_cities = 0;
    std::vector<double> xs, ys;   // city coordinates
    std::vector<double> dist;     // flattened n x n distance matrix

    double d(int i, int j) const { return dist[static_cast<size_t>(i) * n_cities + j]; }
    double tour_length(const std::vector<int>& tour) const;
    bool is_valid_tour(const std::vector<int>& tour) const;
};

// Gen(seed) -- deterministic per-block instance generator. In the real
// mechanism, seed = H(prev_block); here it's whatever the caller derives
// via mix64() chaining (see blockchain.cpp).
TSPInstance generate_instance(uint64_t seed, int n_cities);

std::vector<int> nearest_neighbor_tour(const TSPInstance& inst, int start = 0);

// Ant Colony Optimization solver. step(n) runs n iterations and returns the
// best (tour, length) found since the last reset(); pheromone state (tau_)
// persists across calls, which is exactly the "memory" the paper's security
// analysis is about -- honest miners reset() every block, a
// pheromone-hoarder strategic miner deliberately does not.
class AntColonyOptimizer {
public:
    AntColonyOptimizer(const TSPInstance& inst, uint64_t seed, int n_ants = 12,
                        double alpha = 1.0, double beta = 3.0, double rho = 0.1, double q = 1.0);

    std::pair<std::vector<int>, double> step(int n_iterations);
    void reset(const TSPInstance& inst);

    // Rebinds to a new instance (recomputing eta_/visibility) WITHOUT
    // clearing the pheromone matrix tau_ -- used by the pheromone-hoarder
    // strategic-miner model, which deliberately carries search "memory"
    // across blocks instead of resetting it like an honest miner does.
    // Requires the new instance to have the same n_cities.
    void rebind_instance_keep_pheromones(const TSPInstance& inst);

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

} // namespace antchain
