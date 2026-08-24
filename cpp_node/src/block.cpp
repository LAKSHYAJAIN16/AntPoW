#include "block.hpp"

namespace antchain_node {

std::string Block::tx_root() const {
    Json arr = Json::array();
    for (const auto& tx : transactions) arr.push_back(Json::string(tx.tx_hash()));
    Json wrapper = Json::object();
    wrapper.set("tx_hashes", arr);
    return blake2b256_hex(wrapper.dump_canonical());
}

Json Block::header_payload() const {
    Json j = Json::object();
    j.set("index", Json::integer(static_cast<int64_t>(index)));
    j.set("prev_hash", Json::string(prev_hash));
    j.set("timestamp", Json::number(timestamp));
    j.set("tx_root", Json::string(tx_root()));
    Json tour_arr = Json::array();
    for (int c : tour) tour_arr.push_back(Json::integer(c));
    j.set("tour", tour_arr);
    j.set("f_x", Json::number(f_x));
    j.set("miner_address", Json::string(miner_address));
    j.set("reward", Json::number(reward));
    j.set("nonce", Json::integer(static_cast<int64_t>(nonce)));
    return j;
}

std::string Block::compute_hash() const {
    return blake2b256_hex(header_payload().dump_canonical());
}

void Block::finalize() {
    hash = compute_hash();
}

Json Block::to_json() const {
    Json j = Json::object();
    j.set("index", Json::integer(static_cast<int64_t>(index)));
    j.set("prev_hash", Json::string(prev_hash));
    j.set("timestamp", Json::number(timestamp));
    Json txs = Json::array();
    for (const auto& tx : transactions) txs.push_back(tx.to_json());
    j.set("transactions", txs);
    Json tour_arr = Json::array();
    for (int c : tour) tour_arr.push_back(Json::integer(c));
    j.set("tour", tour_arr);
    j.set("f_x", Json::number(f_x));
    j.set("f_ref", Json::number(f_ref));
    j.set("miner_address", Json::string(miner_address));
    j.set("reward", Json::number(reward));
    j.set("nonce", Json::integer(static_cast<int64_t>(nonce)));
    j.set("hash", Json::string(hash));
    return j;
}

Block Block::from_json(const Json& j) {
    Block b;
    b.index = static_cast<uint64_t>(j.at("index").as_int());
    b.prev_hash = j.at("prev_hash").as_string();
    b.timestamp = j.at("timestamp").as_double();
    for (const auto& tx_json : j.at("transactions").as_array()) {
        b.transactions.push_back(Transaction::from_json(tx_json));
    }
    for (const auto& c : j.at("tour").as_array()) b.tour.push_back(static_cast<int>(c.as_int()));
    b.f_x = j.at("f_x").as_double();
    b.f_ref = j.at("f_ref").as_double();
    b.miner_address = j.at("miner_address").as_string();
    b.reward = j.at("reward").as_double();
    b.nonce = static_cast<uint64_t>(j.at("nonce").as_int());
    b.hash = j.at("hash").as_string();
    return b;
}

Block genesis_block() {
    Block b;
    b.index = 0;
    b.prev_hash = std::string(64, '0');
    b.timestamp = 0.0;
    b.f_x = 0.0;
    b.f_ref = 0.0;
    b.miner_address = kCoinbaseSender;
    b.reward = 0.0;
    b.nonce = 0;
    b.finalize();
    return b;
}

} // namespace antchain_node
