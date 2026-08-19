// AntChain C++ simulator: SHA-PoW vs Pure PoAO vs Hybrid PoW+PoAO.
//
// A from-scratch, dependency-free C++17 reimplementation of
// sim/antchain_sim/*.py's mechanism and metrics, built for throughput: it
// lets the same 3-way experiment run at far larger miner counts / block
// counts / trial counts than the Python simulator can in reasonable time,
// which matters for reducing the sampling noise visible in the Python
// results (see sim/README.md and paper/antchain.tex Section 5).
//
// It is NOT the real toy blockchain -- that's node/antchain_node/ (real
// SHA-256, real ECDSA, real TCP gossip). This is the fast research
// simulator's twin, for large-scale parameter sweeps.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

#include "blockchain.hpp"
#include "consensus.hpp"
#include "metrics.hpp"
#include "miner.hpp"

namespace antchain {

// Measured on this machine (see sim/antchain_sim/experiment.py's identical
// constant and the calibration methodology in its docstring): one
// hashlib.sha256() evaluation vs. one ACO iteration at n_cities=16,
// n_ants=12. This is a fact about real mining hardware operations, not
// about this simulator's implementation language, so the same ratio
// applies here.
constexpr double kAcoIterationCostInHashAttempts = 1671.1;

struct Summary {
    std::string label;
    std::string mode;
    double lam = 0.0;
    int n_blocks = 0;
    double fork_rate = 0.0;
    double mean_block_time = 0.0;
    double block_time_cv = 0.0;
    double gini_rewards = 0.0;
    double gini_hashrate = 0.0;
    double gini_excess = 0.0;
    double strategic_hashrate_share = 0.0;
    double strategic_reward_share = 0.0;
    double strategic_advantage = 0.0;
    double total_hash_attempts = 0.0;
    double total_aco_iterations = 0.0;
    double total_energy_proxy = 0.0;
    double useful_improvement_total = 0.0;
    double useful_improvement_per_joule = 0.0;
};

Summary summarize_run(const std::vector<BlockResult>& blocks, const std::vector<Miner>& miners) {
    Summary s;
    s.n_blocks = static_cast<int>(blocks.size());
    s.mode = blocks.front().mode;
    s.lam = blocks.front().lam;

    int n_miners = static_cast<int>(miners.size());
    std::vector<double> hashrate(n_miners);
    double hr_sum = 0.0;
    for (int i = 0; i < n_miners; ++i) { hashrate[i] = miners[i].hashrate_share; hr_sum += hashrate[i]; }
    for (auto& h : hashrate) h /= hr_sum;

    std::vector<double> wins(n_miners, 0.0);
    for (const auto& b : blocks) {
        for (int i = 0; i < n_miners; ++i) if (miners[i].miner_id == b.winner_id) { wins[i] += 1.0; break; }
    }
    double total_wins = std::accumulate(wins.begin(), wins.end(), 0.0);

    double strategic_hash = 0.0, strategic_wins = 0.0;
    for (int i = 0; i < n_miners; ++i) {
        if (miners[i].is_strategic) { strategic_hash += hashrate[i]; strategic_wins += wins[i]; }
    }
    double strategic_reward_share = total_wins > 0 ? strategic_wins / total_wins : 0.0;
    s.strategic_hashrate_share = strategic_hash;
    s.strategic_reward_share = strategic_reward_share;
    s.strategic_advantage = strategic_hash > 0 ? strategic_reward_share / strategic_hash
                                                : std::numeric_limits<double>::quiet_NaN();

    std::vector<double> block_times;
    double fork_count = 0.0;
    for (const auto& b : blocks) {
        block_times.push_back(static_cast<double>(b.block_time_ticks));
        if (b.fork) fork_count += 1.0;
        s.total_hash_attempts += b.total_hash_attempts;
        s.total_aco_iterations += b.total_aco_iterations;
        s.useful_improvement_total += std::max(0.0, b.f_reference - b.f_winner);
    }
    double mean_bt = std::accumulate(block_times.begin(), block_times.end(), 0.0) / block_times.size();
    double var = 0.0;
    for (double t : block_times) var += (t - mean_bt) * (t - mean_bt);
    var /= block_times.size();

    s.fork_rate = fork_count / s.n_blocks;
    s.mean_block_time = mean_bt;
    s.block_time_cv = std::sqrt(var) / mean_bt;
    s.gini_rewards = gini(wins);
    s.gini_hashrate = gini(hashrate);
    s.gini_excess = s.gini_rewards - s.gini_hashrate;

    s.total_energy_proxy = s.total_hash_attempts + s.total_aco_iterations * kAcoIterationCostInHashAttempts;
    s.useful_improvement_per_joule = s.total_energy_proxy > 0
        ? s.useful_improvement_total / s.total_energy_proxy : 0.0;

    return s;
}

} // namespace antchain

using namespace antchain;

namespace {

void write_summary_csv(const std::string& path, const std::vector<Summary>& summaries) {
    std::ofstream f(path);
    f << "label,mode,lambda,n_blocks,fork_rate,mean_block_time,block_time_cv,"
         "gini_rewards,gini_hashrate,gini_excess,strategic_hashrate_share,"
         "strategic_reward_share,strategic_advantage,total_hash_attempts,"
         "total_aco_iterations,total_energy_proxy,useful_improvement_total,"
         "useful_improvement_per_joule\n";
    f << std::setprecision(10);
    for (const auto& s : summaries) {
        f << s.label << "," << s.mode << "," << s.lam << "," << s.n_blocks << ","
          << s.fork_rate << "," << s.mean_block_time << "," << s.block_time_cv << ","
          << s.gini_rewards << "," << s.gini_hashrate << "," << s.gini_excess << ","
          << s.strategic_hashrate_share << "," << s.strategic_reward_share << ","
          << s.strategic_advantage << "," << s.total_hash_attempts << ","
          << s.total_aco_iterations << "," << s.total_energy_proxy << ","
          << s.useful_improvement_total << "," << s.useful_improvement_per_joule << "\n";
    }
}

void write_blocks_csv(const std::string& path,
                       const std::vector<std::pair<std::string, std::vector<BlockResult>>>& all_blocks) {
    std::ofstream f(path);
    f << "label,block_index,mode,lambda,winner_id,block_time_ticks,fork,n_competing,"
         "f_winner,f_reference,total_hash_attempts,total_aco_iterations,p0\n";
    f << std::setprecision(10);
    for (const auto& [label, blocks] : all_blocks) {
        for (const auto& b : blocks) {
            f << label << "," << b.block_index << "," << b.mode << "," << b.lam << ","
              << b.winner_id << "," << b.block_time_ticks << "," << (b.fork ? 1 : 0) << ","
              << b.n_competing << "," << b.f_winner << "," << b.f_reference << ","
              << b.total_hash_attempts << "," << b.total_aco_iterations << "," << b.p0 << "\n";
        }
    }
}

struct Args {
    int n_miners = 30;
    int n_blocks = 200;
    int n_cities = 16;
    int max_ticks = 40;
    double strategic_fraction = 0.15;
    std::vector<double> lambdas = {0.5, 1.0, 3.0, 10.0};
    uint64_t seed = 7;
    std::string outdir = "../results";
};

Args parse_args(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&]() { return std::string(argv[++i]); };
        if (arg == "--miners") a.n_miners = std::stoi(next());
        else if (arg == "--blocks") a.n_blocks = std::stoi(next());
        else if (arg == "--cities") a.n_cities = std::stoi(next());
        else if (arg == "--max-ticks") a.max_ticks = std::stoi(next());
        else if (arg == "--strategic-fraction") a.strategic_fraction = std::stod(next());
        else if (arg == "--seed") a.seed = static_cast<uint64_t>(std::stoull(next()));
        else if (arg == "--outdir") a.outdir = next();
        else if (arg == "--lambdas") {
            a.lambdas.clear();
            while (i + 1 < argc && argv[i + 1][0] != '-') a.lambdas.push_back(std::stod(next()));
        }
    }
    return a;
}

} // namespace

int main(int argc, char** argv) {
    Args args = parse_args(argc, argv);

    std::vector<std::pair<std::string, double>> conditions = {{"sha_pow", 0.0}, {"pure_poao", 0.0}};
    for (double lam : args.lambdas) conditions.push_back({"hybrid", lam});

    ChainRunConfig cfg;
    cfg.n_cities = args.n_cities;
    cfg.max_ticks = args.max_ticks;
    cfg.target_block_time = 15.0;

    std::vector<Summary> summaries;
    std::vector<std::pair<std::string, std::vector<BlockResult>>> all_blocks;
    auto t_start = std::chrono::steady_clock::now();

    for (auto& [mode, lam] : conditions) {
        auto miners = make_population(args.n_miners, args.strategic_fraction, args.seed);
        ConsensusParams params;
        params.mode = mode;
        params.lam = lam;
        cfg.seed = args.seed + 1;

        auto t0 = std::chrono::steady_clock::now();
        auto blocks = run_chain(miners, params, args.n_blocks, cfg);
        auto t1 = std::chrono::steady_clock::now();
        double secs = std::chrono::duration<double>(t1 - t0).count();

        Summary s = summarize_run(blocks, miners);
        s.label = (mode == "hybrid") ? ("hybrid(lambda=" + std::to_string(lam) + ")") : mode;
        summaries.push_back(s);
        all_blocks.push_back({s.label, blocks});

        std::cout << s.label
                  << ": fork=" << std::fixed << std::setprecision(3) << s.fork_rate
                  << " mean_block_time=" << s.mean_block_time
                  << " gini_rewards=" << s.gini_rewards
                  << " strat_adv=" << s.strategic_advantage
                  << " useful/joule=" << std::scientific << std::setprecision(3) << s.useful_improvement_per_joule
                  << std::fixed << "  (" << secs << "s for " << args.n_blocks << " blocks, "
                  << args.n_miners << " miners)\n";
    }

    auto t_end = std::chrono::steady_clock::now();
    double total_secs = std::chrono::duration<double>(t_end - t_start).count();
    std::cout << "\nTotal: " << total_secs << "s across " << conditions.size() << " conditions.\n";

    std::string summary_path = args.outdir + "/cpp_summary.csv";
    std::string blocks_path = args.outdir + "/cpp_blocks.csv";
    write_summary_csv(summary_path, summaries);
    write_blocks_csv(blocks_path, all_blocks);
    std::cout << "Wrote " << summary_path << "\n";
    std::cout << "Wrote " << blocks_path << "\n";
    return 0;
}
