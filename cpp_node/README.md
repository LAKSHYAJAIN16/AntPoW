# AntChain Node (C++)

A second, standalone implementation of [`../node/`](../node/README.md)'s real,
runnable, multi-node toy cryptocurrency: real Ed25519 keypairs, real
proof-of-work hashing, a real TCP gossip network, a persisted chain, and
real signed transactions implementing the hybrid **Proof-of-Work +
Proof-of-Optimization** mechanism from [`paper/antchain.tex`](../paper/antchain.tex).

This is to `../node/` what [`../cpp/`](../cpp/README.md) is to `../sim/`: an
independent C++ implementation of the same mechanism, not a byte-for-byte
port. **The two node implementations are not wire-compatible** and do not
produce identical block hashes for the same inputs — see "Why this diverges
from `../node/`" below. Run a network of C++ nodes together, or a network of
Python nodes together; don't mix the two.

## ⚠️ Read this before you think about "real" money

Same caveats as `../node/README.md`: no security audit, no peer discovery,
no NAT traversal, no wire encryption, single canonical chain (no
side-branch tree), no fees/halving schedule. This is an educational/research
project, not production software. Use it to see the mechanism actually run,
not to hold value.

## Why this diverges from `../node/`

- **BLAKE2b-256, not SHA-256**, for all hashing (block hash, tx hash,
  address derivation). The vendored crypto library
  ([Monocypher](https://monocypher.org/)) ships BLAKE2b but not SHA-256;
  pulling in a second crypto library just for SHA-256 wasn't worth it.
- **Ed25519, not secp256k1 ECDSA**, for signatures — Monocypher gives real,
  standard Ed25519.
- **A small hand-rolled JSON module**, not a vendored library, used
  uniformly for the canonical byte-payloads that get hashed/signed, chain
  persistence, and the P2P wire protocol.
- **256-bit integer target math**, matching the *precision* of `../node/`'s
  Python implementation (which itself downcasts through a double during the
  `t0 * multiplier` step) rather than exceeding it.
- No `deploy_network.py` equivalent — spin up nodes by hand (below), same
  as `../cpp/` has no such helper either.

None of this changes the mechanism itself: TSP-instance generation, the
quality-weighted target formula, retargeting, and chain validation logic
are all structurally identical to `../node/antchain_node/`.

## Building

Requires CMake 3.15+ and a C++17 compiler. On Windows with Visual Studio
2022 (which bundles both), from `cpp_node/`:

```
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```

On Linux/macOS:

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

This builds `antchain_node` (the CLI/node binary) and `antchain_node_tests`
(unit tests — run it directly, or `ctest` from `build/`).

## Quickstart: one node, mining to itself

```
antchain_node wallet-new --out wallet1.json
antchain_node run --port 6001 --data chain1.json \
    --wallet wallet1.json --mine --difficulty-bits 16 --cities 8
```

Leave that running. In another terminal:

```
antchain_node status  --node 127.0.0.1:6001
antchain_node balance --node 127.0.0.1:6001 --address <address from step 1>
```

`--difficulty-bits 16 --cities 8` mines fast (seconds) on a laptop; raise
either to slow it down.

## Quickstart: two nodes, real network consensus

Terminal 1 (miner):
```
antchain_node wallet-new --out wallet1.json
antchain_node run --port 6001 --data chain1.json \
    --wallet wallet1.json --mine --difficulty-bits 16 --cities 8
```

Terminal 2 (peer, not mining):
```
antchain_node wallet-new --out wallet2.json
antchain_node run --port 6002 --peers 127.0.0.1:6001 \
    --data chain2.json --difficulty-bits 16 --cities 8
```

Terminal 3 (send a transaction, then verify both nodes agree):
```
antchain_node send --node 127.0.0.1:6001 --wallet wallet1.json \
    --to $(antchain_node wallet-address --wallet wallet2.json) --amount 25

antchain_node balance --node 127.0.0.1:6001 --address <wallet2 address>
antchain_node balance --node 127.0.0.1:6002 --address <wallet2 address>
# both should print the same number once it's mined into a block
```

To add a third node, point `--peers` at any node already in the network; it
pulls the current chain on connect and starts receiving new blocks/
transactions live.

## CLI reference

| Command | Purpose |
|---|---|
| `wallet-new --out FILE` | Generate an Ed25519 keypair + address, save to `FILE` |
| `wallet-address --wallet FILE` | Print a wallet's address |
| `run --port P [options]` | Run a node |
| `send --node H:P --wallet FILE --to ADDR --amount N [--nonce N]` | Sign and submit a transaction |
| `balance --node H:P --address ADDR` | Query an address's balance |
| `status --node H:P` | Query height, tip hash, mempool size, peer list |

`run` options: `--host` (default `127.0.0.1`), `--port` (required),
`--peers` (comma-separated `host:port`), `--data` (chain persistence path;
omit to run in-memory only), `--wallet` (required with `--mine`), `--mine`,
`--cities` (10), `--t-opt-frac` (1.15), `--lam` (3.0), `--difficulty-bits`
(20), `--retarget-interval` (10), `--target-block-time` (8.0),
`--block-reward` (50.0).

All nodes on the same network **must agree on every consensus parameter**
(`--cities`, `--t-opt-frac`, `--lam`, `--difficulty-bits`,
`--retarget-interval`, `--target-block-time`, `--block-reward`) for the same
reason as `../node/`: each node validates blocks by recomputing the TSP
instance and target with its own configured parameters.

## Project layout

```
third_party/monocypher/  Vendored crypto: BLAKE2b, SHA-512, Ed25519
include/ + src/
  rng.hpp                 SplitMix64 (miner-local ACO randomness only)
  hex.hpp                  hex encode/decode
  json.hpp / json.cpp       Minimal JSON value type + canonical serialization
  u256.hpp / u256.cpp        Fixed 256-bit unsigned integer (target math)
  crypto.hpp / crypto.cpp     BLAKE2b hashing, Ed25519 wallet, address derivation
  transaction.hpp / .cpp       Signed account-model transactions
  tsp.hpp / tsp.cpp             Deterministic instance generation + ACO
  consensus.hpp / .cpp           Quality-weighted target + retargeting
  block.hpp / block.cpp           Block structure, real hashing
  chain.hpp / chain.cpp            Validation, ledger state, persistence
  mempool.hpp / .cpp                Pending transactions
  miner.hpp / miner.cpp              Search phase (ACO) -> hash phase (nonce grinding)
  network.hpp / .cpp                  TCP gossip (Winsock2/POSIX)
  node.hpp / node.cpp                  Wires chain+mempool+miner+network together
  rpc.hpp / rpc.cpp                     One-shot CLI request helper
  main.cpp                               wallet-new / run / send / balance / status
tests/test_main.cpp        Dependency-free unit tests (crypto, u256, consensus,
                            tsp, json, chain+mempool mine/validate cycle)
```
