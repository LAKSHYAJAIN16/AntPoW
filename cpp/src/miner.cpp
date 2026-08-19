#include "miner.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace antchain {

void Miner::start_block(const TSPInstance& inst, uint64_t block_seed) {
    search_budget_accum = 0.0;
    if (!optimizer || strategy != Strategy::PheromoneHoarder) {
        opt_seed = mix64(block_seed ^ (static_cast<uint64_t>(miner_id) * 0x9E3779B97F4A7C15ULL));
        optimizer = std::make_unique<AntColonyOptimizer>(inst, opt_seed);
    } else {
        // pheromone-hoarder: keep tau_ from the previous block, only rebind
        // the instance (distances change, "memory" doesn't reset)
        optimizer->rebind_instance_keep_pheromones(inst);
    }
    best_length = 1e18;
    best_tour.clear();
}

double Miner::search_step(double search_iterations) {
    search_budget_accum += search_iterations * aco_skill;
    int n_iter = static_cast<int>(search_budget_accum);
    search_budget_accum -= n_iter;
    if (n_iter > 0 && optimizer) {
        auto [tour, length] = optimizer->step(n_iter);
        if (!tour.empty()) {
            best_tour = tour;
            best_length = length;
        }
    }
    return best_length;
}

std::vector<Miner> make_population(int n_miners, double strategic_fraction, uint64_t seed,
                                    double hashrate_alpha, double skill_low, double skill_high) {
    SplitMix64 rng(seed);

    // numpy Generator.pareto(a): Lomax draw via inverse-CDF, X = (1-U)^(-1/a) - 1
    std::vector<double> raw(n_miners);
    double raw_sum = 0.0;
    for (int i = 0; i < n_miners; ++i) {
        double u = rng.next_double();
        double x = std::pow(1.0 - u, -1.0 / hashrate_alpha) - 1.0;
        raw[i] = x + 1.0;
        raw_sum += raw[i];
    }

    std::vector<double> skills(n_miners);
    for (int i = 0; i < n_miners; ++i) {
        skills[i] = skill_low + rng.next_double() * (skill_high - skill_low);
    }

    int n_strategic = static_cast<int>(std::lround(n_miners * strategic_fraction));
    // reservoir-free sample without replacement: partial Fisher-Yates over indices
    std::vector<int> idx(n_miners);
    std::iota(idx.begin(), idx.end(), 0);
    for (int i = 0; i < n_strategic && i < n_miners; ++i) {
        int j = i + rng.next_int(n_miners - i);
        std::swap(idx[i], idx[j]);
    }
    std::vector<bool> is_strategic(n_miners, false);
    for (int i = 0; i < n_strategic; ++i) is_strategic[idx[i]] = true;

    static const Strategy kStrategicStrategies[2] = {Strategy::PheromoneHoarder, Strategy::Withholder};

    std::vector<Miner> miners(n_miners);
    for (int i = 0; i < n_miners; ++i) {
        Miner& m = miners[i];
        m.miner_id = i;
        m.hashrate_share = raw[i] / raw_sum;
        m.aco_skill = skills[i];
        m.is_strategic = is_strategic[i];
        m.strategy = is_strategic[i] ? kStrategicStrategies[i % 2] : Strategy::Honest;
    }
    return miners;
}

} // namespace antchain
