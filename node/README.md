# AntChain Node

A real, runnable, multi-node toy cryptocurrency implementing the hybrid
**Proof-of-Work + Proof-of-Optimization** consensus mechanism from
[`paper/antchain.tex`](../paper/antchain.tex): every block requires a miner
to find a good tour for a per-block Traveling Salesman Problem instance
(derived deterministically from the previous block's hash) *and* a real
SHA-256 hash below a target that gets easier the better that tour is.

This is **not** the research simulator in `../sim/` (which models mining
probabilistically to run large parameter sweeps fast). This is actual node
software: real ECDSA keypairs, real SHA-256 proof-of-work, a real gossip
network over TCP sockets, a persisted chain, and real signed transactions.

## ⚠️ Read this before you think about "real" money

This is an educational/research project, not production software:

- **No security audit.** The crypto primitives (`cryptography` library,
  SECP256k1 ECDSA) are real and solid; the protocol built on top of them is
  not audited and almost certainly has bugs or attack surface I haven't
  found.
- **No peer discovery, no NAT traversal, no encryption on the wire.** You
  give it a static peer list. Fine on localhost or a trusted LAN; not fine
  on the open internet.
- **Single canonical chain, no side-branch storage.** A competing chain is
  only adopted if a peer offers one with strictly more cumulative work; deep
  reorgs are handled by wholesale re-validation of what the peer sends, not
  a maintained tree of forks.
- **No transaction fees, no mempool prioritization, no halving schedule.**
  Deliberately minimal — the point of this project is the consensus
  mechanism, not payment-layer economics.
- Difficulty is tuned via `--cities`, `--difficulty-bits`, `--t-opt-frac`
  for a *toy* network of one or a few nodes on one machine. It has not been
  tuned, and would need to be re-tuned, for anything resembling a real
  distributed miner population.

Use it to see the AntChain mechanism actually run, not to hold value.

## Installation

Requires Python 3.10+.

```bash
cd node
pip install -r requirements.txt
```

That installs `numpy` (TSP/ACO) and `cryptography` (ECDSA signing). No other
dependencies, no external services.

Verify the install:

```bash
python -m antchain_node --help
```

## Quickstart: one node, mining to itself

```bash
# 1. Generate a wallet
python -m antchain_node wallet-new --out data/wallet1.json

# 2. Run a node that mines to that wallet
python -m antchain_node run --port 6001 --data data/chain1.json \
    --wallet data/wallet1.json --mine \
    --difficulty-bits 16 --cities 8
```

Leave that running. In another terminal:

```bash
python -m antchain_node status  --node 127.0.0.1:6001
python -m antchain_node balance --node 127.0.0.1:6001 --address <address from step 1>
```

You should see the height climbing and the balance increasing by the block
reward (default 50) each time a block is mined. `--difficulty-bits 16
--cities 8` mines fast (seconds) on a laptop, good for watching it work;
raise `--difficulty-bits` and/or `--cities` to slow it down.

## Quickstart: two nodes, real network consensus

Terminal 1 (miner):
```bash
python -m antchain_node wallet-new --out data/wallet1.json
python -m antchain_node run --port 6001 --data data/chain1.json \
    --wallet data/wallet1.json --mine --difficulty-bits 16 --cities 8
```

Terminal 2 (peer, not mining):
```bash
python -m antchain_node wallet-new --out data/wallet2.json
python -m antchain_node run --port 6002 --peers 127.0.0.1:6001 \
    --data data/chain2.json --difficulty-bits 16 --cities 8
```

Terminal 3 (send a transaction, then verify both nodes agree):
```bash
python -m antchain_node send --node 127.0.0.1:6001 --wallet data/wallet1.json \
    --to $(python -m antchain_node wallet-address --wallet data/wallet2.json) --amount 25

python -m antchain_node balance --node 127.0.0.1:6001 --address <wallet2 address>
python -m antchain_node balance --node 127.0.0.1:6002 --address <wallet2 address>
# both should print the same number once it's mined into a block
```

To add a third node, point `--peers` at any node already in the network
(e.g. `--peers 127.0.0.1:6001,127.0.0.1:6002`); it will pull the current
chain on connect and start receiving new blocks/transactions live. To run
across machines instead of localhost, use `--host 0.0.0.0` on the listener
and real IPs in `--peers` (make sure the port is reachable — no NAT
traversal is implemented).

## CLI reference

| Command | Purpose |
|---|---|
| `wallet-new --out FILE` | Generate an ECDSA keypair + address, save to `FILE` |
| `wallet-address --wallet FILE` | Print a wallet's address |
| `run --port P [options]` | Run a node (see below for options) |
| `send --node H:P --wallet FILE --to ADDR --amount N` | Sign and submit a transaction |
| `balance --node H:P --address ADDR` | Query an address's balance |
| `status --node H:P` | Query height, tip hash, mempool size, peer list |

`run` options:

| Flag | Default | Meaning |
|---|---|---|
| `--host` | `127.0.0.1` | Bind address |
| `--port` | required | Listen port |
| `--peers` | none | Comma-separated `host:port` list to connect to at startup |
| `--data` | none | Path to persist the chain as JSON (omit to run in-memory only) |
| `--wallet` | none | Wallet file to receive block rewards (required with `--mine`) |
| `--mine` | off | Enable mining |
| `--cities` | 10 | TSP instance size per block |
| `--t-opt-frac` | 1.15 | Quality gate: tour must be within this fraction of the cheap reference tour |
| `--lam` | 3.0 | λ: max target multiplier is `(1+λ)` for a perfect solution |
| `--difficulty-bits` | 20 | Initial target = `2^256 >> difficulty_bits`; lower = easier/faster |
| `--retarget-interval` | 10 | Blocks between difficulty retargets |
| `--target-block-time` | 8.0 | Seconds, used by the retarget algorithm |
| `--block-reward` | 50.0 | Coinbase reward per block |

All nodes on the same network **must agree on every consensus parameter**
(`--cities`, `--t-opt-frac`, `--lam`, `--difficulty-bits`,
`--retarget-interval`, `--target-block-time`, `--block-reward`) — a node
validates every block by recomputing the TSP instance and target with its
*own* configured parameters, so mismatched parameters between nodes will
just make every block look invalid to whichever node is misconfigured.

## How a block actually gets mined (what to watch for)

1. Miner takes the current tip's hash, deterministically regenerates the
   TSP instance from it (every node computes the same instance).
2. Runs Ant Colony Optimization for up to a fixed iteration budget, trying
   to beat a cheap nearest-neighbor reference tour.
3. Computes the quality-weighted target `T(x) = T0 * (1 + λ·Δf/f_ref)` from
   how good the tour is.
4. Grinds nonces, computing real `SHA-256(prev‖tx_root‖tour‖f(x)‖miner‖nonce)`,
   until the hash is below `T(x)`.
5. Broadcasts the block; every receiving node independently recomputes the
   TSP instance, re-derives the reference tour, recomputes the target, and
   verifies the hash — exactly the verification procedure in the paper
   (Section 3.4), and cheap (`O(n_cities)`) regardless of how long mining took.

If you want to see λ actually matter, watch `data/chain*.json` (or add
logging) for how often `f_x` beats `f_ref` — with the defaults above, a
better tour measurably speeds up mining. See `../sim/README.md`'s
"Calibration note" for why this can silently stop mattering if you change
`--cities`/`--lam`/iteration budgets without checking.

## Project layout

```
antchain_node/
  crypto.py        ECDSA keys, signing, address derivation (cryptography lib)
  transaction.py     Signed account-model transactions
  tsp.py               Deterministic instance generation + ACO (real, non-probabilistic)
  block.py               Block structure, real SHA-256 hashing
  consensus.py              Integer-target quality-weighted difficulty + retargeting
  chain.py                    Validation, ledger state, persistence
  mempool.py                    Pending transactions
  miner.py                       Search phase (ACO) -> hash phase (nonce grinding)
  network.py                      TCP gossip: peers, broadcast, message dispatch
  node.py                          Wires chain+mempool+miner+network together
  cli.py                            wallet-new / run / send / balance / status
```
