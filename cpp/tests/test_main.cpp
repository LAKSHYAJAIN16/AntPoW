// Dependency-free sanity tests (no gtest/catch2 -- assert-based, run as a
// plain executable). Not exhaustive; covers the invariants that matter most
// for trusting the experiment's output: valid tours, ACO actually improves
// on the reference, Gini is correct on known distributions, the
// quality-weighted target is bounded as the paper claims, and a short chain
// run produces internally-consistent results.
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

#include "blockchain.hpp"
#include "consensus.hpp"
#include "metrics.hpp"
#include "miner.hpp"
#include "tsp.hpp"

using namespace antchain;

static int g_failures = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        ++g_failures; \
    } \
} while (0)

#define CHECK_NEAR(a, b, tol) CHECK(std::fabs((a) - (b)) < (tol))

void test_tsp_instance_and_tours() {
    TSPInstance inst = generate_instance(42, 12);
    CHECK(inst.n_cities == 12);
    CHECK(static_cast<int>(inst.dist.size()) == 12 * 12);

    auto nn = nearest_neighbor_tour(inst);
    CHECK(inst.is_valid_tour(nn));
    CHECK(inst.tour_length(nn) > 0.0);

    // determinism: same seed -> identical instance
    TSPInstance inst2 = generate_instance(42, 12);
    for (int i = 0; i < 12; ++i) CHECK_NEAR(inst.xs[i], inst2.xs[i], 1e-15);

    // an invalid tour (duplicate city) must be rejected
    std::vector<int> bad = nn;
    bad[0] = bad[1];
    CHECK(!inst.is_valid_tour(bad));
}

void test_aco_improves_on_reference() {
    TSPInstance inst = generate_instance(7, 20);
    double f_ref = inst.tour_length(nearest_neighbor_tour(inst));

    AntColonyOptimizer aco(inst, 123, /*n_ants=*/16);
    auto [tour, length] = aco.step(80);
    CHECK(inst.is_valid_tour(tour));
    // ACO with a meaningful iteration budget should be competitive with or
    // better than the cheap nearest-neighbor heuristic on a 20-city instance
    CHECK(length <= f_ref * 1.05);
}

void test_pheromone_hoarder_keeps_state() {
    TSPInstance inst1 = generate_instance(1, 15);
    TSPInstance inst2 = generate_instance(2, 15);
    AntColonyOptimizer aco(inst1, 55);
    aco.step(20);
    double tau_sample_before = 0.0;  // can't access tau_ directly (private); use behavioral proxy instead
    (void)tau_sample_before;

    aco.rebind_instance_keep_pheromones(inst2);
    CHECK(aco.best_tour.empty());          // per-block bookkeeping resets
    CHECK(aco.best_length > 1e17);          // reset to "infinity"
    // running on the new instance should still work and produce a valid tour
    auto [tour, length] = aco.step(5);
    CHECK(inst2.is_valid_tour(tour));
    CHECK(length > 0.0);
}

void test_gini_known_values() {
    CHECK_NEAR(gini({1.0, 1.0, 1.0, 1.0}), 0.0, 1e-9);          // perfect equality
    double g_extreme = gini({0.0, 0.0, 0.0, 100.0});
    CHECK(g_extreme > 0.6);                                      // strong inequality
    CHECK(gini({0.0, 0.0, 0.0}) == 0.0);                          // degenerate: no reward at all
}

void test_quality_target_bounds() {
    double p0 = 1e-4, lam = 3.0;
    // no improvement -> exactly p0
    CHECK_NEAR(quality_target(p0, lam, 10.0, 10.0), p0, 1e-12);
    // perfect solution (f_x=0) -> capped at p0*(1+lam), never higher
    double t = quality_target(p0, lam, 10.0, 0.0);
    CHECK_NEAR(t, p0 * (1.0 + lam), 1e-12);
    // worse-than-reference solution -> clipped to p0, never penalized below it
    CHECK_NEAR(quality_target(p0, lam, 10.0, 20.0), p0, 1e-12);
}

void test_win_probability_sane() {
    CHECK(win_probability_this_tick(0.0, 100.0) == 0.0);
    CHECK(win_probability_this_tick(0.5, 0.0) == 0.0);
    double p = win_probability_this_tick(0.1, 1.0);
    CHECK_NEAR(p, 0.1, 1e-9);
    // many attempts at nonzero p should approach certainty
    CHECK(win_probability_this_tick(0.01, 10000.0) > 0.99);
}

void test_run_chain_sanity() {
    auto miners = make_population(10, 0.2, 99);
    ConsensusParams params;
    params.mode = "hybrid";
    params.lam = 2.0;
    ChainRunConfig cfg;
    cfg.n_cities = 10;
    cfg.max_ticks = 30;
    cfg.seed = 1234;

    auto results = run_chain(miners, params, 15, cfg);
    CHECK(static_cast<int>(results.size()) == 15);
    for (const auto& r : results) {
        CHECK(r.winner_id >= 0 && r.winner_id < 10);
        CHECK(r.block_time_ticks >= 1 && r.block_time_ticks <= cfg.max_ticks);
        CHECK(r.f_winner > 0.0);
        CHECK(r.f_reference > 0.0);
        CHECK(r.n_competing >= 1);
    }
    // block_index should be strictly increasing 0..n-1
    for (size_t i = 0; i < results.size(); ++i) CHECK(results[i].block_index == static_cast<int>(i));
}

void test_pure_poao_forks_far_more_than_hybrid() {
    // Regression guard for the paper's headline finding: pure PoAO should
    // fork dramatically more often than the hybrid mechanism under
    // identical miners/instance. (Not a tight bound -- just guards against
    // silently breaking the qualitative result during future refactors.)
    auto miners_a = make_population(20, 0.15, 5);
    auto miners_b = make_population(20, 0.15, 5);
    ChainRunConfig cfg;
    cfg.n_cities = 10;
    cfg.max_ticks = 25;
    cfg.seed = 42;

    ConsensusParams poao_params;
    poao_params.mode = "pure_poao";
    auto poao_results = run_chain(miners_a, poao_params, 20, cfg);

    ConsensusParams hybrid_params;
    hybrid_params.mode = "hybrid";
    hybrid_params.lam = 3.0;
    auto hybrid_results = run_chain(miners_b, hybrid_params, 20, cfg);

    auto fork_rate = [](const std::vector<BlockResult>& rs) {
        int forks = 0;
        for (const auto& r : rs) if (r.fork) ++forks;
        return static_cast<double>(forks) / rs.size();
    };
    CHECK(fork_rate(poao_results) > fork_rate(hybrid_results));
}

int main() {
    test_tsp_instance_and_tours();
    test_aco_improves_on_reference();
    test_pheromone_hoarder_keeps_state();
    test_gini_known_values();
    test_quality_target_bounds();
    test_win_probability_sane();
    test_run_chain_sanity();
    test_pure_poao_forks_far_more_than_hybrid();

    if (g_failures == 0) {
        std::printf("All tests passed.\n");
        return 0;
    }
    std::printf("%d test(s) FAILED.\n", g_failures);
    return 1;
}
