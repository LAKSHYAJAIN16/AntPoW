#pragma once
#include <array>
#include <cstdint>
#include <string>

namespace antchain_node {

constexpr size_t kHashSize = 32;      // BLAKE2b-256 digest
constexpr size_t kSeedSize = 32;      // Ed25519 seed
constexpr size_t kSecretKeySize = 64; // Ed25519 expanded secret key (monocypher convention)
constexpr size_t kPublicKeySize = 32; // Ed25519 public key
constexpr size_t kSignatureSize = 64; // Ed25519 signature

using Hash32 = std::array<uint8_t, kHashSize>;

// H() used throughout this implementation wherever node/antchain_node/crypto.py
// uses hashlib.sha256 -- block hashing, tx hashing, address derivation, and
// the PoW target comparison. See cpp_node/README.md for why BLAKE2b instead
// of SHA-256 (monocypher doesn't ship SHA-256; not worth a second library).
Hash32 blake2b256(const uint8_t* data, size_t len);
Hash32 blake2b256(const std::string& data);
std::string blake2b256_hex(const std::string& data);

// sha256(pubkey)[:20] analog: "ant1" + hex(H(pubkey)[:20]).
std::string address_from_pubkey_hex(const std::string& pubkey_hex);

// Fills `len` bytes from the OS CSPRNG (BCrypt/rand_s on Windows,
// /dev/urandom on POSIX). Used only for wallet key generation -- never for
// consensus-critical values.
void secure_random_bytes(uint8_t* out, size_t len);

class Wallet {
public:
    static Wallet generate();
    static Wallet from_seed_hex(const std::string& seed_hex); // 64 hex chars

    std::string seed_hex() const;
    std::string pubkey_hex() const;
    std::string address() const;
    std::string sign_hex(const std::string& message) const;

private:
    std::array<uint8_t, kSeedSize> seed_{};
    std::array<uint8_t, kSecretKeySize> secret_key_{};
    std::array<uint8_t, kPublicKeySize> public_key_{};
};

bool verify_signature(const std::string& pubkey_hex, const std::string& message, const std::string& signature_hex);

} // namespace antchain_node
