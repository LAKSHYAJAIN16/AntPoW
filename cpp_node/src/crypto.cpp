// _CRT_RAND_S must be defined before the FIRST inclusion of <stdlib.h> in
// this translation unit (including transitive includes), so this has to be
// the very first thing in the file -- ahead of even crypto.hpp.
#ifdef _WIN32
#define _CRT_RAND_S
#include <stdlib.h>
#endif

#include "crypto.hpp"

#include <algorithm>
#include <stdexcept>

#include "hex.hpp"

extern "C" {
#include "monocypher.h"
#include "monocypher-ed25519.h"
}

#ifndef _WIN32
#include <cstdio>
#endif

namespace antchain_node {

Hash32 blake2b256(const uint8_t* data, size_t len) {
    Hash32 out{};
    crypto_blake2b(out.data(), kHashSize, data, len);
    return out;
}

Hash32 blake2b256(const std::string& data) {
    return blake2b256(reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

std::string blake2b256_hex(const std::string& data) {
    return to_hex(blake2b256(data));
}

std::string address_from_pubkey_hex(const std::string& pubkey_hex) {
    auto pub = from_hex(pubkey_hex);
    Hash32 digest = blake2b256(pub.data(), pub.size());
    return "ant1" + to_hex(digest.data(), 20);
}

void secure_random_bytes(uint8_t* out, size_t len) {
#ifdef _WIN32
    size_t i = 0;
    while (i < len) {
        unsigned int r;
        if (rand_s(&r) != 0) throw std::runtime_error("rand_s failed");
        size_t chunk = (len - i < 4) ? (len - i) : 4;
        for (size_t k = 0; k < chunk; ++k) {
            out[i + k] = static_cast<uint8_t>((r >> (8 * k)) & 0xFF);
        }
        i += chunk;
    }
#else
    std::FILE* f = std::fopen("/dev/urandom", "rb");
    if (!f) throw std::runtime_error("could not open /dev/urandom");
    size_t got = std::fread(out, 1, len, f);
    std::fclose(f);
    if (got != len) throw std::runtime_error("short read from /dev/urandom");
#endif
}

Wallet Wallet::generate() {
    Wallet w;
    secure_random_bytes(w.seed_.data(), w.seed_.size());
    // monocypher's key_pair wipes/consumes the seed buffer in place; keep our
    // own copy since Wallet persists the seed to reconstruct the wallet later.
    std::array<uint8_t, kSeedSize> seed_copy = w.seed_;
    crypto_ed25519_key_pair(w.secret_key_.data(), w.public_key_.data(), seed_copy.data());
    return w;
}

Wallet Wallet::from_seed_hex(const std::string& seed_hex) {
    auto bytes = from_hex(seed_hex);
    if (bytes.size() != kSeedSize) throw std::invalid_argument("wallet seed must be 32 bytes (64 hex chars)");
    Wallet w;
    std::copy(bytes.begin(), bytes.end(), w.seed_.begin());
    std::array<uint8_t, kSeedSize> seed_copy = w.seed_;
    crypto_ed25519_key_pair(w.secret_key_.data(), w.public_key_.data(), seed_copy.data());
    return w;
}

std::string Wallet::seed_hex() const { return to_hex(seed_); }
std::string Wallet::pubkey_hex() const { return to_hex(public_key_); }
std::string Wallet::address() const { return address_from_pubkey_hex(pubkey_hex()); }

std::string Wallet::sign_hex(const std::string& message) const {
    std::array<uint8_t, kSignatureSize> sig{};
    crypto_ed25519_sign(sig.data(), secret_key_.data(),
                         reinterpret_cast<const uint8_t*>(message.data()), message.size());
    return to_hex(sig);
}

bool verify_signature(const std::string& pubkey_hex, const std::string& message, const std::string& signature_hex) {
    try {
        auto pub = from_hex(pubkey_hex);
        auto sig = from_hex(signature_hex);
        if (pub.size() != kPublicKeySize || sig.size() != kSignatureSize) return false;
        return crypto_ed25519_check(sig.data(), pub.data(),
                                     reinterpret_cast<const uint8_t*>(message.data()), message.size()) == 0;
    } catch (const std::exception&) {
        return false;
    }
}

} // namespace antchain_node
