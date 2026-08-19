# AntChain C++ Simulator

A dependency-free C++17 reimplementation of `sim/antchain_sim/`'s 3-way
experiment (SHA-PoW vs pure PoAO vs Hybrid PoW+PoAO), built for throughput.
Same mechanism, same metrics, same calibrated energy weighting -- just fast
enough to run experiments at miner/block counts the Python version can't
finish in reasonable time, which matters for cutting down the sampling
noise visible in the Python results (see `../sim/README.md` and
`../paper/antchain.tex` Section 5).

This is **not** the real toy blockchain (`../node/`, which has actual
SHA-256/ECDSA/TCP networking). It's the fast research simulator's twin, for
large-scale parameter sweeps. Block "hashes" here are a non-cryptographic
64-bit mixer (`SplitMix64`/`mix64`, see `include/rng.hpp`) used purely to
chain instance seeds deterministically between simulated blocks -- exactly
the role `hashlib.sha256()` plays in the Python simulator for the same
purpose, not a stand-in for the real node's consensus hashing.

## Building

Requires a C++17 compiler. No external dependencies.

**With CMake:**
```bash
cd cpp
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

**Directly (no CMake needed):**
```bash
cd cpp
clang++ -std=c++17 -O2 -Iinclude src/*.cpp -o build/antchain_experiment
# (drop src/experiment.cpp and add tests/test_main.cpp to build the test binary instead)
```
(substitute `g++` or `cl` for `clang++` as available; MSVC needs `/std:c++17 /O2 /I include`)

## Running

```bash
./build/antchain_experiment --miners 200 --blocks 2000 --cities 16 \
    --max-ticks 40 --lambdas 0.5 1 3 10 --seed 7 --outdir ../results
```

| Flag | Default | Meaning |
|---|---|---|
| `--miners` | 30 | Miner population size |
| `--blocks` | 200 | Blocks simulated per condition |
| `--cities` | 16 | TSP instance size |
| `--max-ticks` | 40 | Max ticks per block before the "nobody found a valid solution" fallback |
| `--strategic-fraction` | 0.15 | Fraction of miners that are pheromone-hoarders/withholders |
| `--lambdas` | 0.5 1 3 10 | Hybrid λ values to sweep (space-separated) |
| `--seed` | 7 | RNG seed |
| `--outdir` | `../results` | Where to write `cpp_summary.csv` |

Six conditions (sha_pow, pure_poao, hybrid×4 λ) at 200 miners / 2000 blocks
run in about 20 seconds on a laptop -- the equivalent Python run would take
much longer at that scale. This is the throughput argument for having a
compiled version at all: it makes parameter sweeps and noise-reduction
(more blocks per data point) actually practical.

## Testing

```bash
# after building with CMake:
ctest --test-dir build

# or directly:
clang++ -std=c++17 -O2 -Iinclude src/tsp.cpp src/consensus.cpp src/miner.cpp \
    src/blockchain.cpp src/metrics.cpp tests/test_main.cpp -o build/antchain_tests
./build/antchain_tests
```

Assert-based (no gtest/catch2 dependency). Covers: TSP instance determinism
and tour validity, ACO actually improving on the nearest-neighbor
reference, the pheromone-hoarder strategy correctly preserving state across
`rebind_instance_keep_pheromones`, Gini coefficient correctness on known
distributions, the quality-weighted target's bounded-advantage property
(`quality_target` never exceeds `p0*(1+lambda)` or drops below `p0`), and a
regression guard that pure PoAO forks measurably more than hybrid under
identical miners.

## Project layout

```
include/    Public headers (rng, tsp, consensus, miner, blockchain, metrics)
src/        Implementations + experiment.cpp (the CLI entry point)
tests/      Assert-based sanity tests
CMakeLists.txt
```

## A finding this scale surfaced that the smaller Python runs didn't

At 200 miners (vs. Python's default 30), hybrid's `gini_excess` (reward
inequality beyond what hashrate share alone would predict) comes out
**about as bad as pure PoAO's**, not close to SHA-PoW's like the paper
hoped. The likely mechanism: a miner's ACO search budget is proportional to
its hashrate share, so under a heavy-tailed (Pareto) hashrate distribution
with many miners, the long tail of small miners never accumulates enough
search iterations to clear the quality gate (`t_opt_frac`) *at all* --
regardless of λ or the hash lottery. The unfairness comes from the
eligibility gate itself excluding low-hashrate miners from ever competing,
which λ-tuning can't fix. See `paper/antchain.tex` Section 6 for the full
writeup and what this implies for the mechanism design.
