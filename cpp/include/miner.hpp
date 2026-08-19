#pragma once
#include <memory>
#include <vector>

#include "rng.hpp"
#include "tsp.hpp"

namespace antchain {

enum class Strategy { Honest, PheromoneHoarder, Withholder };

// Heterogeneous miner agent: hashrate share, ACO skill multiplier, and
// strategy (honest / pheromone-hoarder / withholder), mirroring
// sim/antchain_sim/miner.py.
struct Miner {
    int miner_id = 0;
    double hashrate_share = 0.0;
    double aco_skill = 1.0;
    Strategy strategy = Strategy::Honest;
    bool is_strategic = false;

    std::unique_ptr<AntColonyOptimizer> optimizer;
    double best_length = 1e18;
    std::vector<int> best_tour;
    double search_budget_accum = 0.0;  // carries fractional ACO-iteration budget across ticks
    uint64_t opt_seed = 0;

    void start_block(const TSPInstance& inst, uint64_t block_seed);
    double search_step(double search_iterations);
};

std::vector<Miner> make_population(int n_miners, double strategic_fraction, uint64_t seed,
                                    double hashrate_alpha = 1.5, double skill_low = 0.6,
                                    double skill_high = 1.8);

} // namespace antchain
