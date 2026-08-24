#include "miner.hpp"

#include <chrono>
#include <cstdio>
#include <limits>

#include "tsp.hpp"

namespace antchain_node {

namespace {
constexpr int kMaxAcoIterations = 300;
constexpr int kNonceCheckInterval = 4000;

double now_seconds() {
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}
} // namespace

Miner::Miner(Blockchain& chain, Mempool& mempool, std::string miner_address,
             BlockFoundCallback on_block_found, LogFn log)
    : chain_(chain), mempool_(mempool), miner_address_(std::move(miner_address)),
      on_block_found_(std::move(on_block_found)), log_(std::move(log)) {}

Miner::~Miner() { stop(); }

void Miner::log(const std::string& msg) const {
    if (log_) log_(msg); else std::puts(msg.c_str());
}

void Miner::start() {
    stop_ = false;
    thread_ = std::thread(&Miner::loop, this);
}

void Miner::stop() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
}

bool Miner::tip_changed(const std::string& prev_hash) const {
    return chain_.tip().hash != prev_hash || stop_.load();
}

void Miner::loop() {
    while (!stop_.load()) {
        auto block = mine_one_block();
        if (block.has_value() && on_block_found_) on_block_found_(*block);
    }
}

std::optional<Block> Miner::mine_one_block() {
    const ConsensusParams& params = chain_.params;
    Block prev = chain_.tip();
    std::string prev_hash = prev.hash;
    U256 target = chain_.next_target();

    TSPInstance instance = generate_instance(seed_from_hash_hex(prev_hash), params.n_cities);
    double f_ref = reference_length(instance);
    double t_opt = params.t_opt_frac * f_ref;

    // Miner-local search randomness -- deliberately NOT derived from the
    // block hash (that would make every node search identically), mirroring
    // node/antchain_node/miner.py's unseeded np.random.default_rng(): not
    // part of consensus, only affects how fast a given node happens to find
    // a good tour.
    uint64_t aco_seed = 0;
    secure_random_bytes(reinterpret_cast<uint8_t*>(&aco_seed), sizeof(aco_seed));
    AntColonyOptimizer aco(instance, aco_seed);
    std::vector<int> best_tour;
    double best_length = std::numeric_limits<double>::infinity();
    for (int i = 0; i < kMaxAcoIterations; ++i) {
        if (tip_changed(prev_hash)) return std::nullopt;
        auto result = aco.step(1);
        best_tour = result.first;
        best_length = result.second;
        if (!best_tour.empty() && best_length <= t_opt) break;
    }

    if (best_tour.empty() || best_length > t_opt) {
        best_tour = nearest_neighbor_tour(instance);
        best_length = instance.tour_length(best_tour);
    }

    U256 required_target = quality_target(target, params.lam, f_ref, best_length);
    auto txs = mempool_.select(chain_, static_cast<size_t>(params.max_txs_per_block));

    uint64_t nonce = 0;
    uint64_t checked = 0;
    while (true) {
        ++checked;
        if (checked % static_cast<uint64_t>(kNonceCheckInterval) == 0 && tip_changed(prev_hash)) return std::nullopt;
        Block block;
        block.index = prev.index + 1;
        block.prev_hash = prev_hash;
        block.timestamp = now_seconds();
        block.transactions = txs;
        block.tour = best_tour;
        block.f_x = best_length;
        block.f_ref = f_ref;
        block.miner_address = miner_address_;
        block.reward = params.block_reward;
        block.nonce = nonce;
        block.finalize();
        if (U256::from_hex(block.hash) < required_target) {
            char buf[256];
            std::snprintf(buf, sizeof(buf), "[miner] found block %llu f(x)=%.4f f_ref=%.4f nonce=%llu hash=%.16s...",
                          static_cast<unsigned long long>(block.index), best_length, f_ref,
                          static_cast<unsigned long long>(nonce), block.hash.c_str());
            log(buf);
            return block;
        }
        ++nonce;
    }
}

} // namespace antchain_node
