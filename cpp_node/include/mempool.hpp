#pragma once
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "chain.hpp"
#include "transaction.hpp"

namespace antchain_node {

// Pending-transaction pool. Admission here is a courtesy pre-check (valid
// signature, plausible balance against the current tip); the real check
// happens again at block-validation time. Mirrors node/antchain_node/mempool.py.
class Mempool {
public:
    bool add(const Transaction& tx, Blockchain& chain);
    void remove_all(const std::vector<Transaction>& txs);
    // Greedily selects pending txs whose sender-nonce sequence is currently
    // satisfiable, respecting per-sender ordering.
    std::vector<Transaction> select(Blockchain& chain, size_t max_count);
    size_t size();

private:
    std::mutex mutex_;
    std::map<std::string, Transaction> by_hash_;
};

} // namespace antchain_node
