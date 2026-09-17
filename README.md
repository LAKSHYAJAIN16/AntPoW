# AntChain: Useful Proof-of-Work via Distributed Ant Colony Optimization

> Can you embed Ant Colony Optimization inside Bitcoin-style PoW without wrecking its security properties?

That's the question this research project chases. There are two halves, each built twice — once in Python, once in C++ for speed.

- **`sim/`** (Python) / **`cpp/`** (C++17) — fast research simulator, thousands of blocks in seconds, for testing security/fairness/efficiency across parameter sweeps. Not a real currency. See [`cpp/README.md`](cpp/README.md).
- **`node/`** (Python) / **`cpp_node/`** (C++17) — a real, runnable multi-node toy cryptocurrency: PoW mining, signed transactions, TCP gossip, persisted chain. The two aren't wire-compatible with each other. See [`node/README.md`](node/README.md) and [`cpp_node/README.md`](cpp_node/README.md).
- **`web/`** — Next.js workbench that drives the C++ simulator from a browser instead of the CLI. See [`web/README.md`](web/README.md).
- **`paper/`** — the writeup (`antchain.tex` IEEEtran, `antchain_neurips.tex` NeurIPS, same content). Compile with pdflatex.

## Layout

```
paper/       LaTeX writeup (two variants, same content)
sim/         Python simulator + run_experiment.py CLI
node/        Python multi-node toy cryptocurrency
cpp/         C++17 twin of sim/
cpp_node/    C++17 twin of node/
web/         Next.js workbench driving cpp/
results/     summary.csv, blocks.csv, comparison plots (generated)
```

## Running the simulation

```bash
cd sim
pip install -r requirements.txt
python run_experiment.py --quick          # fast smoke test
python run_experiment.py                  # full default run
python run_experiment.py --miners 50 --blocks 80 --lambdas 0.5 1 2 5 10
```

Output goes to `results/summary.csv`, `results/blocks.csv`, and two PNG comparison plots.

## Three consensus mechanisms

Compared on one shared code path so miner heterogeneity and metrics stay comparable (full design in `paper/antchain.tex` Section 5):

- **sha_pow** — baseline memoryless hash lottery, no optimization component
- **pure_poao** — first miner to hit a quality threshold wins outright; shows high reward inequality and strategic-miner advantage (negative result, Section 2.3)
- **hybrid** — the actual PoW+PoAO mechanism (Section 3): a per-block TSP instance derived from the previous block hash quality-weights a hash lottery via `T(x) = T0 * (1 + lambda * (f_old - f(x)) / f_old)`, swept over `lambda`

Metrics collected per condition: fork rate, block-time variance, Gini coefficient of rewards vs. Gini of hashrate, strategic-miner advantage, compute spent, and useful optimization per unit of compute.

## Calibration note

Defaults (`search_iters_per_tick=20`, `hybrid_search_fraction=0.35`, `n_cities=16`) are tuned so ACO search can beat the nearest-neighbor reference tour often enough for `lambda` to matter. If you change `n_cities`/`max_ticks`/population size, check `blocks.csv`'s `f_winner` is sometimes below `f_reference` for the hybrid condition — if not, bump `search_iters_per_tick` or `max_ticks`.
