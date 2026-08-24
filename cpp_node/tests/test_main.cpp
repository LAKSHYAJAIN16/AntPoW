// Dependency-free sanity tests (no gtest/catch2 -- assert-based, run as a
// plain executable), mirroring cpp/tests/test_main.cpp's style. Covers the
// invariants that matter most for trusting this as a real, runnable node:
// crypto roundtrips, exact 256-bit target math, deterministic hashing, and
// an in-process mine-validate-persist cycle exercising chain+mempool
// together (not just each module in isolation).
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "block.hpp"
#include "chain.hpp"
#include "consensus.hpp"
#include "crypto.hpp"
#include "json.hpp"
#include "mempool.hpp"
#include "transaction.hpp"
#include "tsp.hpp"
#include "u256.hpp"

using namespace antchain_node;

static int g_failures = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        ++g_failures; \
    } \
} while (0)

#define CHECK_NEAR(a, b, tol) CHECK(std::fabs((double)(a) - (double)(b)) < (tol))

// ------------------------------------------------------------------- u256

void test_u256_basics() {
    U256 zero = U256::zero();
    U256 max = U256::max();
    CHECK(zero.is_zero());
    CHECK(!max.is_zero());
    CHECK(zero < max);
    CHECK(!(max < zero));

    U256 one = U256::from_u64(1);
    U256 shifted = max.shr(255);
    CHECK(shifted == one); // top bit only

    std::string hex = max.to_hex();
    CHECK(hex.size() == 64);
    U256 roundtrip = U256::from_hex(hex);
    CHECK(roundtrip == max);

    // exact division: MAX_TARGET // 1 == MAX_TARGET; MAX // MAX == 1
    CHECK(U256::divide(max, one) == max);
    CHECK(U256::divide(max, max) == one);
    // MAX = 2^256-1 is odd, so floor(MAX/2) == MAX >> 1 exactly.
    U256 half = max.shr(1);
    U256 two = U256::from_u64(2);
    CHECK(U256::divide(max, two) == half);
}

void test_u256_double_roundtrip() {
    U256 t0 = U256::max().shr(20); // difficulty_bits=20 initial target
    double d = t0.to_double();
    CHECK(d > 0.0);
    U256 back = U256::from_double_clamped(d);
    // lossy (double only has 53 bits of mantissa) but should be within a
    // tiny relative error of the original 236-bit value.
    double back_d = back.to_double();
    CHECK_NEAR(back_d / d, 1.0, 1e-9);

    CHECK(U256::from_double_clamped(-5.0).is_zero());
    CHECK(U256::from_double_clamped(1e100) == U256::max());
}

// --------------------------------------------------------------- consensus

void test_quality_target_bounds() {
    U256 t0 = U256::max().shr(20);
    double lam = 3.0;
    // no improvement (f_x == f_old) -> target == t0 (within double precision)
    U256 same = quality_target(t0, lam, 10.0, 10.0);
    CHECK_NEAR(same.to_double() / t0.to_double(), 1.0, 1e-9);
    // perfect solution (f_x=0) -> capped at t0*(1+lam)
    U256 capped = quality_target(t0, lam, 10.0, 0.0);
    CHECK_NEAR(capped.to_double() / t0.to_double(), 1.0 + lam, 1e-9);
    // worse-than-reference solution -> clipped to t0, never penalized below it
    U256 worse = quality_target(t0, lam, 10.0, 20.0);
    CHECK_NEAR(worse.to_double() / t0.to_double(), 1.0, 1e-9);
}

void test_retarget_clamped() {
    U256 t0 = U256::max().shr(20);
    // blocks arriving 100x slower than expected -> clamp to 4x easier, not 100x
    U256 eased = retarget(t0, 800.0, 8.0);
    CHECK_NEAR(eased.to_double() / t0.to_double(), 4.0, 1e-6);
    // blocks arriving 100x faster -> clamp to 4x harder (0.25x), not 100x
    U256 hardened = retarget(t0, 0.08, 8.0);
    CHECK_NEAR(hardened.to_double() / t0.to_double(), 0.25, 1e-6);
}

// -------------------------------------------------------------------- tsp

void test_tsp_determinism_and_validity() {
    TSPInstance inst = generate_instance(42, 12);
    CHECK(inst.n_cities == 12);
    auto nn = nearest_neighbor_tour(inst);
    CHECK(inst.is_valid_tour(nn));
    CHECK(reference_length(inst) == inst.tour_length(nn));

    TSPInstance inst2 = generate_instance(42, 12);
    for (int i = 0; i < 12; ++i) CHECK_NEAR(inst.xs[i], inst2.xs[i], 1e-15);

    std::vector<int> bad = nn;
    bad[0] = bad[1];
    CHECK(!inst.is_valid_tour(bad));

    // seed derivation from a hash must be deterministic given the same hex
    std::string h(64, 'a');
    CHECK(seed_from_hash_hex(h) == seed_from_hash_hex(h));
}

// ----------------------------------------------------------------- crypto

void test_wallet_sign_verify_roundtrip() {
    Wallet w = Wallet::generate();
    std::string addr = w.address();
    CHECK(addr.rfind("ant1", 0) == 0);

    std::string msg = "hello antchain";
    std::string sig = w.sign_hex(msg);
    CHECK(verify_signature(w.pubkey_hex(), msg, sig));
    CHECK(!verify_signature(w.pubkey_hex(), "tampered message", sig));

    // reconstructing from the persisted seed must reproduce the same identity
    Wallet w2 = Wallet::from_seed_hex(w.seed_hex());
    CHECK(w2.address() == addr);
    CHECK(w2.pubkey_hex() == w.pubkey_hex());
}

void test_blake2b_deterministic() {
    CHECK(blake2b256_hex("abc") == blake2b256_hex("abc"));
    CHECK(blake2b256_hex("abc") != blake2b256_hex("abd"));
    CHECK(blake2b256_hex("abc").size() == 64);
}

// -------------------------------------------------------------- json

void test_json_canonical_and_roundtrip() {
    Json obj = Json::object();
    obj.set("b", Json::integer(2));
    obj.set("a", Json::integer(1));
    CHECK(obj.dump_canonical() == "{\"a\":1,\"b\":2}"); // sorted keys

    Json parsed = Json::parse("{\"x\":1.5,\"y\":[1,2,3],\"z\":\"hi\"}");
    CHECK_NEAR(parsed.at("x").as_double(), 1.5, 1e-12);
    CHECK(parsed.at("y").as_array().size() == 3);
    CHECK(parsed.at("z").as_string() == "hi");
}

// ---------------------------------------------------------- transaction

void test_transaction_sign_verify() {
    Wallet sender = Wallet::generate();
    Wallet recipient = Wallet::generate();
    Transaction tx;
    tx.sender = sender.address();
    tx.sender_pubkey = sender.pubkey_hex();
    tx.recipient = recipient.address();
    tx.amount = 10.0;
    tx.nonce = 0;
    tx.sign(sender);
    CHECK(tx.verify());

    Transaction tampered = tx;
    tampered.amount = 1000.0;
    CHECK(!tampered.verify());

    // JSON roundtrip preserves everything verify() depends on
    Transaction back = Transaction::from_json(tx.to_json());
    CHECK(back.verify());
    CHECK(back.tx_hash() == tx.tx_hash());
}

// --------------------------------------------------------------- block

void test_genesis_block_deterministic() {
    Block g1 = genesis_block();
    Block g2 = genesis_block();
    CHECK(g1.hash == g2.hash);
    CHECK(g1.compute_hash() == g1.hash);
    CHECK(g1.prev_hash == std::string(64, '0'));
}

// --------------------------------------------------------- chain + mempool

Block mine_block_for_test(const Blockchain& chain_ref, ConsensusParams params, const Block& prev,
                           const std::string& miner_address, const std::vector<Transaction>& txs, U256 target) {
    TSPInstance instance = generate_instance(seed_from_hash_hex(prev.hash), params.n_cities);
    double f_ref = reference_length(instance);
    std::vector<int> tour = nearest_neighbor_tour(instance); // cheap and deterministic, fine for a test
    double f_x = instance.tour_length(tour);
    U256 required_target = quality_target(target, params.lam, f_ref, f_x);
    (void)chain_ref;

    Block b;
    b.index = prev.index + 1;
    b.prev_hash = prev.hash;
    b.timestamp = prev.timestamp + 1.0;
    b.transactions = txs;
    b.tour = tour;
    b.f_x = f_x;
    b.f_ref = f_ref;
    b.miner_address = miner_address;
    b.reward = params.block_reward;
    b.nonce = 0;
    while (true) {
        b.finalize();
        if (U256::from_hex(b.hash) < required_target) return b;
        ++b.nonce;
    }
}

void test_chain_mine_validate_and_mempool() {
    ConsensusParams params;
    params.n_cities = 6;
    params.difficulty_bits = 1; // near-trivial target so the test mines instantly
    params.retarget_interval = 100; // avoid retargeting during this short test
    Blockchain chain(params, "");

    Wallet miner_wallet = Wallet::generate();
    Wallet alice = Wallet::generate();

    // Mine 3 blocks rewarding the miner, then a 4th carrying a signed tx.
    for (int i = 0; i < 3; ++i) {
        U256 target = chain.next_target();
        Block b = mine_block_for_test(chain, params, chain.tip(), miner_wallet.address(), {}, target);
        CHECK(chain.try_extend(b));
    }
    CHECK(chain.height() == 3);
    CHECK_NEAR(chain.balance_of(miner_wallet.address()), 3 * params.block_reward, 1e-9);

    Mempool mempool;
    Transaction tx;
    tx.sender = miner_wallet.address();
    tx.sender_pubkey = miner_wallet.pubkey_hex();
    tx.recipient = alice.address();
    tx.amount = 25.0;
    tx.nonce = chain.next_nonce_for(miner_wallet.address());
    tx.sign(miner_wallet);
    CHECK(mempool.add(tx, chain));
    CHECK(mempool.size() == 1);

    auto selected = mempool.select(chain, 50);
    CHECK(selected.size() == 1);

    U256 target = chain.next_target();
    Block b4 = mine_block_for_test(chain, params, chain.tip(), miner_wallet.address(), selected, target);
    CHECK(chain.try_extend(b4));
    mempool.remove_all(b4.transactions);
    CHECK(mempool.size() == 0);

    CHECK_NEAR(chain.balance_of(alice.address()), 25.0, 1e-9);
    CHECK_NEAR(chain.balance_of(miner_wallet.address()), 3 * params.block_reward - 25.0 + params.block_reward, 1e-9);

    // full-chain re-validation from scratch (as a peer receiving this chain would do)
    auto [bal, nonces, targets] = chain.validate_full_chain(chain.blocks);
    CHECK_NEAR(bal.at(alice.address()), 25.0, 1e-9);
    CHECK(targets.size() == chain.blocks.size() - 1);

    // a tampered block must be rejected
    Block bad = b4;
    bad.reward = 999.0;
    bool threw = false;
    try {
        std::map<std::string, double> b2 = chain.balances;
        std::map<std::string, uint64_t> n2 = chain.nonces;
        chain.validate_block(bad, chain.blocks[chain.blocks.size() - 2], target, b2, n2);
    } catch (const ChainError&) {
        threw = true;
    }
    CHECK(threw);
}

int main() {
    test_u256_basics();
    test_u256_double_roundtrip();
    test_quality_target_bounds();
    test_retarget_clamped();
    test_tsp_determinism_and_validity();
    test_wallet_sign_verify_roundtrip();
    test_blake2b_deterministic();
    test_json_canonical_and_roundtrip();
    test_transaction_sign_verify();
    test_genesis_block_deterministic();
    test_chain_mine_validate_and_mempool();

    if (g_failures == 0) {
        std::printf("All tests passed.\n");
        return 0;
    }
    std::printf("%d test(s) FAILED.\n", g_failures);
    return 1;
}
