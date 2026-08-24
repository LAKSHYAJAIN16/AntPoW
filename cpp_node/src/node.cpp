#include "node.hpp"

#include <chrono>
#include <stdexcept>
#include <thread>

namespace antchain_node {

namespace {
const Json kNullJson = Json::null();
}

Node::Node(std::string host, int port, ConsensusParams params, std::string data_path,
           std::string miner_addr, bool mine, Network::LogFn log)
    : chain(std::move(params), std::move(data_path)),
      network(std::move(host), port, [this](socket_t s, const Json& m) { on_message(s, m); }, log),
      miner_address(std::move(miner_addr)),
      log_(std::move(log)) {
    chain.load();
    if (mine && !miner_address.empty()) {
        miner = std::make_unique<Miner>(chain, mempool, miner_address,
                                         [this](const Block& b) { on_block_found(b); }, log_);
    }
}

void Node::log(const std::string& msg) const {
    if (log_) log_(msg); else std::puts(msg.c_str());
}

void Node::start(const std::vector<std::pair<std::string, int>>& peers) {
    network.start();
    for (const auto& hp : peers) {
        if (network.connect_to(hp.first, hp.second)) {
            socket_t s = network.peer_socket(hp.first + ":" + std::to_string(hp.second));
            if (s != kInvalidSocket) {
                Json hello = Json::object();
                hello.set("type", Json::string("hello"));
                hello.set("host", Json::string(network.host));
                hello.set("port", Json::integer(network.port));
                hello.set("height", Json::integer(static_cast<int64_t>(chain.height())));
                network.send(s, hello);
                Json get_chain = Json::object();
                get_chain.set("type", Json::string("get_chain"));
                network.send(s, get_chain);
            }
        }
    }
    if (miner) {
        miner->start();
        log("[node] mining enabled, rewards -> " + miner_address);
    }
}

void Node::on_block_found(const Block& block) {
    try {
        chain.try_extend(block);
        mempool.remove_all(block.transactions);
        Json msg = Json::object();
        msg.set("type", Json::string("new_block"));
        msg.set("block", block.to_json());
        network.broadcast(msg);
        log("[chain] height=" + std::to_string(chain.height()) + " tip=" + chain.tip().hash.substr(0, 16) + "...");
    } catch (const ChainError& e) {
        log(std::string("[miner] mined a block that failed self-validation: ") + e.what());
    }
}

void Node::handle_new_block(const Json* block_json, socket_t origin) {
    if (!block_json) return;
    Block block = Block::from_json(*block_json);
    if (block.hash == chain.tip().hash) return;
    if (block.prev_hash != chain.tip().hash) {
        log("[chain] received block " + std::to_string(block.index) + " that doesn't extend our tip; requesting full chain");
        Json get_chain = Json::object();
        get_chain.set("type", Json::string("get_chain"));
        network.broadcast(get_chain);
        return;
    }
    try {
        chain.try_extend(block);
        mempool.remove_all(block.transactions);
        Json msg = Json::object();
        msg.set("type", Json::string("new_block"));
        msg.set("block", block.to_json());
        network.broadcast(msg, origin);
        log("[chain] accepted block " + std::to_string(block.index) + " from network, height=" + std::to_string(chain.height()));
    } catch (const ChainError& e) {
        log("[chain] rejected block " + std::to_string(block.index) + ": " + e.what());
    }
}

void Node::handle_full_chain(const Json& blocks_json) {
    if (!blocks_json.is_array() || blocks_json.size() == 0) return;
    std::vector<Block> blocks;
    try {
        for (const auto& bj : blocks_json.as_array()) blocks.push_back(Block::from_json(bj));
    } catch (const std::exception&) {
        return;
    }
    try {
        if (chain.try_replace(blocks)) log("[chain] adopted longer/heavier chain, height=" + std::to_string(chain.height()));
    } catch (const ChainError& e) {
        log(std::string("[chain] rejected candidate chain: ") + e.what());
    }
}

void Node::handle_new_tx(const Json* tx_json, socket_t origin) {
    if (!tx_json) return;
    Transaction tx = Transaction::from_json(*tx_json);
    if (mempool.add(tx, chain)) {
        Json msg = Json::object();
        msg.set("type", Json::string("new_tx"));
        msg.set("tx", tx.to_json());
        network.broadcast(msg, origin);
    }
}

void Node::on_message(socket_t sock, const Json& msg) {
    if (!msg.is_object() || !msg.has("type")) return;
    std::string type = msg.at("type").as_string();

    if (type == "hello") return;

    if (type == "get_chain") {
        Json reply = Json::object();
        reply.set("type", Json::string("chain"));
        reply.set("blocks", chain.to_json_list());
        network.send(sock, reply);
    } else if (type == "chain") {
        handle_full_chain(msg.get("blocks", kNullJson));
    } else if (type == "new_block") {
        if (msg.has("block")) { const Json& b = msg.at("block"); handle_new_block(&b, sock); }
    } else if (type == "new_tx") {
        if (msg.has("tx")) { const Json& t = msg.at("tx"); handle_new_tx(&t, sock); }
    } else if (type == "get_balance") {
        std::string addr = msg.at("address").as_string();
        Json reply = Json::object();
        reply.set("type", Json::string("balance"));
        reply.set("address", Json::string(addr));
        reply.set("balance", Json::number(chain.balance_of(addr)));
        reply.set("nonce", Json::integer(static_cast<int64_t>(chain.next_nonce_for(addr))));
        network.send(sock, reply);
    } else if (type == "get_status") {
        Json reply = Json::object();
        reply.set("type", Json::string("status"));
        reply.set("height", Json::integer(static_cast<int64_t>(chain.height())));
        reply.set("tip", Json::string(chain.tip().hash));
        reply.set("mempool_size", Json::integer(static_cast<int64_t>(mempool.size())));
        Json peer_list = Json::array();
        for (const auto& kv : network.peers()) peer_list.push_back(Json::string(kv.first));
        reply.set("peers", peer_list);
        network.send(sock, reply);
    } else if (type == "submit_tx") {
        Transaction tx;
        bool parsed = false;
        std::string error;
        try {
            tx = Transaction::from_json(msg.at("tx"));
            parsed = true;
        } catch (const std::exception& e) {
            error = e.what();
        }
        Json reply = Json::object();
        reply.set("type", Json::string("submit_tx_result"));
        if (!parsed) {
            reply.set("accepted", Json::boolean(false));
            reply.set("error", Json::string(error));
        } else {
            bool ok = mempool.add(tx, chain);
            if (ok) {
                Json bcast = Json::object();
                bcast.set("type", Json::string("new_tx"));
                bcast.set("tx", tx.to_json());
                network.broadcast(bcast, sock);
            }
            reply.set("accepted", Json::boolean(ok));
            reply.set("tx_hash", Json::string(tx.tx_hash()));
        }
        network.send(sock, reply);
    }
}

void Node::run_forever() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

} // namespace antchain_node
