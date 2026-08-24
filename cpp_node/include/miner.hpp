#pragma once
#include <atomic>
#include <functional>
#include <optional>
#include <string>
#include <thread>

#include "block.hpp"
#include "chain.hpp"
#include "mempool.hpp"

namespace antchain_node {

// Mining loop: search phase (ACO) -> quality-weighted target -> hash phase
// (nonce grinding). Runs in a background thread and restarts whenever the
// chain tip changes underneath it. Mirrors node/antchain_node/miner.py.
class Miner {
public:
    using BlockFoundCallback = std::function<void(const Block&)>;
    using LogFn = std::function<void(const std::string&)>;

    Miner(Blockchain& chain, Mempool& mempool, std::string miner_address,
          BlockFoundCallback on_block_found, LogFn log = nullptr);
    ~Miner();

    void start();
    void stop();

private:
    bool tip_changed(const std::string& prev_hash) const;
    std::optional<Block> mine_one_block();
    void loop();
    void log(const std::string& msg) const;

    Blockchain& chain_;
    Mempool& mempool_;
    std::string miner_address_;
    BlockFoundCallback on_block_found_;
    LogFn log_;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

} // namespace antchain_node
