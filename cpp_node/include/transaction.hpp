#pragma once
#include <cstdint>
#include <string>

#include "crypto.hpp"
#include "json.hpp"

namespace antchain_node {

// Account-model transaction: sender, recipient, amount, sender nonce (replay
// protection), Ed25519 signature. No fees, no UTXO -- kept minimal, mirrors
// node/antchain_node/transaction.py.
struct Transaction {
    std::string sender;         // address
    std::string sender_pubkey;  // hex
    std::string recipient;      // address
    double amount = 0.0;
    uint64_t nonce = 0;
    std::string signature;      // hex, empty until sign()

    Json signing_payload() const;
    std::string signing_bytes() const; // canonical JSON of signing_payload()
    std::string tx_hash() const;       // H(signing_payload() + signature)

    void sign(const Wallet& wallet);
    bool verify() const;

    Json to_json() const;
    static Transaction from_json(const Json& j);
};

} // namespace antchain_node
