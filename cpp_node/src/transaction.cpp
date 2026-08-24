#include "transaction.hpp"

#include <stdexcept>

namespace antchain_node {

Json Transaction::signing_payload() const {
    Json j = Json::object();
    j.set("sender", Json::string(sender));
    j.set("recipient", Json::string(recipient));
    j.set("amount", Json::number(amount));
    j.set("nonce", Json::integer(static_cast<int64_t>(nonce)));
    return j;
}

std::string Transaction::signing_bytes() const {
    return signing_payload().dump_canonical();
}

std::string Transaction::tx_hash() const {
    Json payload = signing_payload();
    payload.set("signature", Json::string(signature));
    return blake2b256_hex(payload.dump_canonical());
}

void Transaction::sign(const Wallet& wallet) {
    signature = wallet.sign_hex(signing_bytes());
}

bool Transaction::verify() const {
    if (amount <= 0.0) return false;
    try {
        if (address_from_pubkey_hex(sender_pubkey) != sender) return false;
    } catch (const std::exception&) {
        return false;
    }
    return verify_signature(sender_pubkey, signing_bytes(), signature);
}

Json Transaction::to_json() const {
    Json j = signing_payload();
    j.set("sender_pubkey", Json::string(sender_pubkey));
    j.set("signature", Json::string(signature));
    return j;
}

Transaction Transaction::from_json(const Json& j) {
    Transaction tx;
    tx.sender = j.at("sender").as_string();
    tx.sender_pubkey = j.at("sender_pubkey").as_string();
    tx.recipient = j.at("recipient").as_string();
    tx.amount = j.at("amount").as_double();
    tx.nonce = static_cast<uint64_t>(j.at("nonce").as_int());
    static const Json kEmpty = Json::string("");
    tx.signature = j.get("signature", kEmpty).as_string();
    return tx;
}

} // namespace antchain_node
