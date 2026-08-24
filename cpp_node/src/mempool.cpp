#include "mempool.hpp"

#include <algorithm>

namespace antchain_node {

bool Mempool::add(const Transaction& tx, Blockchain& chain) {
    std::string h = tx.tx_hash();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (by_hash_.count(h)) return false;
    }
    if (!tx.verify()) return false;
    if (chain.balance_of(tx.sender) < tx.amount) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    by_hash_[h] = tx;
    return true;
}

void Mempool::remove_all(const std::vector<Transaction>& txs) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& tx : txs) by_hash_.erase(tx.tx_hash());
}

size_t Mempool::size() {
    std::lock_guard<std::mutex> lock(mutex_);
    return by_hash_.size();
}

std::vector<Transaction> Mempool::select(Blockchain& chain, size_t max_count) {
    std::vector<Transaction> pending;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& kv : by_hash_) pending.push_back(kv.second);
    }

    std::map<std::string, std::vector<Transaction>> by_sender;
    for (const auto& tx : pending) by_sender[tx.sender].push_back(tx);
    for (auto& kv : by_sender) {
        std::sort(kv.second.begin(), kv.second.end(),
                  [](const Transaction& a, const Transaction& b) { return a.nonce < b.nonce; });
    }

    std::vector<Transaction> selected;
    std::map<std::string, uint64_t> next_nonce;
    std::map<std::string, double> balances;
    std::map<std::string, size_t> cursor;
    for (const auto& kv : by_sender) {
        next_nonce[kv.first] = chain.next_nonce_for(kv.first);
        balances[kv.first] = chain.balance_of(kv.first);
        cursor[kv.first] = 0;
    }

    bool progressed = true;
    while (progressed && selected.size() < max_count) {
        progressed = false;
        for (auto& kv : by_sender) {
            const std::string& sender = kv.first;
            auto& txs = kv.second;
            size_t& idx = cursor[sender];
            if (idx >= txs.size()) continue;
            const Transaction& tx = txs[idx];
            if (tx.nonce == next_nonce[sender] && balances[sender] >= tx.amount) {
                selected.push_back(tx);
                next_nonce[sender] += 1;
                balances[sender] -= tx.amount;
                idx += 1;
                progressed = true;
                if (selected.size() >= max_count) return selected;
            }
        }
    }
    return selected;
}

} // namespace antchain_node
