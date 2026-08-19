#include "blockchain.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>
#include <set>

#include "tsp.hpp"

namespace antchain {
namespace {

double mode_search_fraction(const std::string& mode, double hybrid_search_fraction) {
    if (mode == "sha_pow") return 0.0;
    if (mode == "pure_poao") return 1.0;
    return hybrid_search_fraction;  // "hybrid"
}

uint64_t double_bits(double x) {
    uint64_t bits;
    std::memcpy(&bits, &x, sizeof(bits));
    return bits;
}

struct WinEvent {
    int tick;
    int miner_id;
    double f_x;
};

} // namespace

std::vector<BlockResult> run_chain(std::vector<Miner>& miners, const ConsensusParams& params,
                                    int n_blocks, const ChainRunConfig& cfg) {
    SplitMix64 rng(cfg.seed);
    DifficultyAdjuster difficulty(cfg.target_block_time, 10, 2e-4);

    uint64_t prev_seed = mix64(cfg.seed ^ 0xA5A5A5A5A5A5A5A5ULL);  // genesis
    const double search_fraction = mode_search_fraction(params.mode, cfg.hybrid_search_fraction);
    const bool use_hash_lottery = (params.mode != "pure_poao");

    std::vector<BlockResult> results;
    results.reserve(n_blocks);
    std::vector<int> order(miners.size());
    std::iota(order.begin(), order.end(), 0);

    for (int b = 0; b < n_blocks; ++b) {
        TSPInstance instance = generate_instance(prev_seed, cfg.n_cities);
        double f_ref = instance.tour_length(nearest_neighbor_tour(instance));
        double t_opt = params.t_opt_frac * f_ref;

        for (auto& m : miners) m.start_block(instance, prev_seed);

        std::vector<WinEvent> win_events;
        double total_hash_attempts = 0.0;
        double total_aco_iterations = 0.0;
        int stop_tick = -1;

        for (int tick = 0; tick < cfg.max_ticks; ++tick) {
            // Fisher-Yates shuffle of miner order this tick
            for (int i = static_cast<int>(order.size()) - 1; i > 0; --i) {
                int j = rng.next_int(i + 1);
                std::swap(order[i], order[j]);
            }

            for (int idx : order) {
                Miner& m = miners[idx];
                double f_x = std::numeric_limits<double>::quiet_NaN();
                bool eligible = true;

                if (search_fraction > 0.0) {
                    double search_budget = m.hashrate_share * cfg.search_iters_per_tick * search_fraction;
                    if (search_budget > 0.0) {
                        f_x = m.search_step(search_budget);
                        total_aco_iterations += search_budget;
                    } else {
                        f_x = m.best_length;
                    }
                    eligible = f_x <= t_opt;
                }

                if (!use_hash_lottery) {
                    if (eligible) win_events.push_back({tick, m.miner_id, f_x});
                    continue;
                }

                if (!eligible) continue;
                if (m.strategy == Strategy::Withholder && tick < static_cast<int>(cfg.withhold_frac * cfg.max_ticks)) {
                    continue;  // deliberately not hashing yet
                }

                double p = (search_fraction > 0.0)
                    ? quality_target(difficulty.p0, params.lam, f_ref, f_x)
                    : difficulty.p0;

                double n_attempts = m.hashrate_share * cfg.hash_attempts_per_tick_per_share *
                    (search_fraction > 0.0 ? (1.0 - search_fraction) : 1.0);
                total_hash_attempts += n_attempts;

                if (n_attempts > 0.0 && rng.next_double() < win_probability_this_tick(p, n_attempts)) {
                    win_events.push_back({tick, m.miner_id, std::isnan(f_x) ? f_ref : f_x});
                }
            }

            if (!win_events.empty() && stop_tick < 0) {
                stop_tick = win_events.front().tick + cfg.propagation_delay_ticks;
            }
            if (stop_tick >= 0 && tick >= stop_tick) break;
        }

        if (win_events.empty()) {
            if (search_fraction > 0.0) {
                auto best = std::min_element(miners.begin(), miners.end(),
                    [](const Miner& a, const Miner& b) { return a.best_length < b.best_length; });
                win_events.push_back({cfg.max_ticks - 1, best->miner_id, best->best_length});
            } else {
                auto best = std::max_element(miners.begin(), miners.end(),
                    [](const Miner& a, const Miner& b) { return a.hashrate_share < b.hashrate_share; });
                win_events.push_back({cfg.max_ticks - 1, best->miner_id, f_ref});
            }
        }

        int first_tick = win_events.front().tick;
        std::vector<WinEvent> window_events;
        for (const auto& e : win_events) {
            if (e.tick <= first_tick + cfg.propagation_delay_ticks) window_events.push_back(e);
        }
        WinEvent winner = (window_events.size() > 1)
            ? window_events[rng.next_int(static_cast<int>(window_events.size()))]
            : window_events.front();

        std::set<int> competing;
        for (const auto& e : window_events) if (e.miner_id != winner.miner_id) competing.insert(e.miner_id);

        int block_time = winner.tick + 1;
        difficulty.record_block_time(block_time);

        BlockResult result;
        result.block_index = b;
        result.mode = params.mode;
        result.lam = params.lam;
        result.winner_id = winner.miner_id;
        result.block_time_ticks = block_time;
        result.fork = !competing.empty();
        result.n_competing = static_cast<int>(competing.size()) + 1;
        result.f_winner = winner.f_x;
        result.f_reference = f_ref;
        result.total_hash_attempts = total_hash_attempts;
        result.total_aco_iterations = total_aco_iterations;
        result.p0 = difficulty.p0;
        results.push_back(result);

        prev_seed = mix64(prev_seed ^ mix64(static_cast<uint64_t>(b) * 2654435761u) ^
                           mix64(static_cast<uint64_t>(winner.miner_id) + 0x9E3779B9u) ^
                           mix64(double_bits(winner.f_x)));
    }

    return results;
}

} // namespace antchain
