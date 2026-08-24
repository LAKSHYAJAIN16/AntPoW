#include "chain.hpp"

#include <cmath>
#include <fstream>
#include <sstream>

#include "tsp.hpp"

namespace antchain_node {

namespace {
constexpr double kEps = 1e-6;
}

Blockchain::Blockchain(ConsensusParams p, std::string data_path)
    : params(std::move(p)), data_path_(std::move(data_path)) {
    blocks.push_back(genesis_block());
}

uint64_t Blockchain::height() const { return blocks.back().index; }
const Block& Blockchain::tip() const { return blocks.back(); }

std::pair<std::vector<U256>, U256> Blockchain::compute_targets(const std::vector<Block>& bs) const {
    std::vector<U256> targets;
    U256 t = params.initial_target();
    for (size_t i = 1; i < bs.size(); ++i) {
        targets.push_back(t);
        if (i % static_cast<size_t>(params.retarget_interval) == 0) {
            const Block& start = bs[i - static_cast<size_t>(params.retarget_interval)];
            const Block& end = bs[i];
            double actual = end.timestamp - start.timestamp;
            double expected = static_cast<double>(params.retarget_interval) * params.target_block_time;
            t = retarget(t, actual, expected);
        }
    }
    return {targets, t};
}

U256 Blockchain::next_target() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return compute_targets(blocks).second;
}

U256 Blockchain::cumulative_work(const std::vector<U256>& targets) {
    U256 total;
    for (const auto& t : targets) {
        if (t.is_zero()) continue;
        total = total.add(U256::divide(max_target(), t));
    }
    return total;
}

void Blockchain::validate_block(const Block& block, const Block& prev, const U256& target,
                                 std::map<std::string, double>& bal, std::map<std::string, uint64_t>& nce) const {
    if (block.index != prev.index + 1) throw ChainError("bad index");
    if (block.prev_hash != prev.hash) throw ChainError("bad prev_hash");
    if (block.timestamp < prev.timestamp) throw ChainError("timestamp not monotonic");
    if (block.compute_hash() != block.hash) throw ChainError("hash does not match header contents");
    if (std::abs(block.reward - params.block_reward) > kEps) throw ChainError("bad block reward");
    if (block.transactions.size() > static_cast<size_t>(params.max_txs_per_block))
        throw ChainError("too many transactions");

    TSPInstance instance = generate_instance(seed_from_hash_hex(block.prev_hash), params.n_cities);
    if (!instance.is_valid_tour(block.tour)) throw ChainError("invalid tour: not a permutation of all cities");
    double f_x = instance.tour_length(block.tour);
    if (std::abs(f_x - block.f_x) > kEps) throw ChainError("declared f(x) does not match recomputed tour length");
    double f_ref = reference_length(instance);
    if (std::abs(f_ref - block.f_ref) > kEps) throw ChainError("declared f_ref does not match recomputed reference");
    if (f_x > params.t_opt_frac * f_ref + kEps) throw ChainError("solution does not meet quality gate T_opt");

    U256 required_target = quality_target(target, params.lam, f_ref, f_x);
    U256 block_hash_value = U256::from_hex(block.hash);
    if (!(block_hash_value < required_target)) throw ChainError("hash does not meet quality-weighted target");

    std::map<std::string, uint64_t> touched_nonces = nce;
    std::map<std::string, double> touched_balances = bal;
    for (const auto& tx : block.transactions) {
        if (!tx.verify()) throw ChainError("invalid signature/address on tx " + tx.tx_hash());
        uint64_t expected_nonce = touched_nonces.count(tx.sender) ? touched_nonces[tx.sender] : 0;
        if (tx.nonce != expected_nonce) throw ChainError("bad nonce for " + tx.sender);
        double senderBal = touched_balances.count(tx.sender) ? touched_balances[tx.sender] : 0.0;
        if (senderBal < tx.amount) throw ChainError("insufficient balance for " + tx.sender);
        touched_balances[tx.sender] = senderBal - tx.amount;
        touched_balances[tx.recipient] = (touched_balances.count(tx.recipient) ? touched_balances[tx.recipient] : 0.0) + tx.amount;
        touched_nonces[tx.sender] = expected_nonce + 1;
    }
    touched_balances[block.miner_address] =
        (touched_balances.count(block.miner_address) ? touched_balances[block.miner_address] : 0.0) + block.reward;

    bal = std::move(touched_balances);
    nce = std::move(touched_nonces);
}

std::tuple<std::map<std::string, double>, std::map<std::string, uint64_t>, std::vector<U256>>
Blockchain::validate_full_chain(const std::vector<Block>& bs) const {
    if (bs.empty() || bs.front().hash != genesis_block().hash) throw ChainError("bad or missing genesis block");
    std::map<std::string, double> bal;
    std::map<std::string, uint64_t> nce;
    auto targets_pair = compute_targets(bs);
    const auto& targets = targets_pair.first;
    for (size_t i = 1; i < bs.size(); ++i) {
        validate_block(bs[i], bs[i - 1], targets[i - 1], bal, nce);
    }
    return {bal, nce, targets};
}

bool Blockchain::try_extend(const Block& block) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    U256 target = next_target();
    std::map<std::string, double> bal = balances;
    std::map<std::string, uint64_t> nce = nonces;
    validate_block(block, tip(), target, bal, nce);
    blocks.push_back(block);
    balances = std::move(bal);
    nonces = std::move(nce);
    persist();
    return true;
}

bool Blockchain::try_replace(const std::vector<Block>& new_blocks) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto [bal, nce, targets] = validate_full_chain(new_blocks);
    auto current_targets = compute_targets(blocks).first;
    if (!(cumulative_work(current_targets) < cumulative_work(targets))) return false;
    blocks = new_blocks;
    balances = std::move(bal);
    nonces = std::move(nce);
    persist();
    return true;
}

double Blockchain::balance_of(const std::string& address) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = balances.find(address);
    return it == balances.end() ? 0.0 : it->second;
}

uint64_t Blockchain::next_nonce_for(const std::string& address) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = nonces.find(address);
    return it == nonces.end() ? 0 : it->second;
}

void Blockchain::persist() {
    if (data_path_.empty()) return;
    Json arr = Json::array();
    for (const auto& b : blocks) arr.push_back(b.to_json());
    std::ofstream out(data_path_, std::ios::trunc);
    if (!out) throw ChainError("could not open data path for writing: " + data_path_);
    out << arr.dump();
}

bool Blockchain::load() {
    if (data_path_.empty()) return false;
    std::ifstream in(data_path_);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    Json arr = Json::parse(ss.str());
    std::vector<Block> loaded;
    for (const auto& bj : arr.as_array()) loaded.push_back(Block::from_json(bj));
    auto [bal, nce, targets] = validate_full_chain(loaded);
    (void)targets;
    blocks = std::move(loaded);
    balances = std::move(bal);
    nonces = std::move(nce);
    return true;
}

Json Blockchain::to_json_list() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    Json out = Json::array();
    for (const auto& b : blocks) out.push_back(b.to_json());
    return out;
}

} // namespace antchain_node
