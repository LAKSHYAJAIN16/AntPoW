# AntChain: Useful Proof-of-Work via Distributed Ant Colony Optimization

I wanted to know if you could embed Ant Colony Optimization inside
Bitcoin-style Proof-of-Work without wrecking the security properties you
get from a plain memoryless hash lottery. This is the research project
that came out of chasing that question.

There are two halves to it, and I built each one twice — once in Python,
once in C++ for speed:

- **`sim/`** (Python) / **`cpp/`** (C++17, dependency-free) — a fast
  research simulator. Probabilistic mining, thousands of blocks in
  seconds, built to answer "does this mechanism actually have good
  security/fairness/efficiency properties across a lot of parameter
  settings?" Not runnable as a real currency — the C++ version just
  implements the same mechanism and metrics for raw throughput at large
  miner/block counts. See [`cpp/README.md`](cpp/README.md).
- **`node/`** (Python) / **`cpp_node/`** (C++17) — a real, runnable,
  multi-node toy cryptocurrency: actual proof-of-work mining, actual
  signed transactions, a real TCP gossip network, a persisted chain. This
  is me proving the mechanism works end to end on however many local/LAN
  nodes you spin up. Heads up: the two node implementations aren't
  wire-compatible with each other (different hash/signature primitives) —
  see [`node/README.md`](node/README.md) and
  [`cpp_node/README.md`](cpp_node/README.md) if you want to actually run
  one.
- **`web/`** — a local Next.js workbench (`antchain-workbench`) that
  builds and drives the C++ simulator from a browser instead of the
  command line: pick parameters, run the experiment, look at the results.
  See [`web/README.md`](web/README.md).

## Layout

```
paper/
  antchain.tex          IEEEtran (journal) version: mechanism design +
                         security analysis + experimental protocol.
  antchain_neurips.tex  Same paper, typeset against the official NeurIPS
                         2024 style file instead. Content is identical to
                         antchain.tex; only the document class/preamble
                         differ.
  neurips_2024.sty      Official NeurIPS style file (vendored, unmodified),
                         required to compile antchain_neurips.tex.
                       Compile either with pdflatex (requires a LaTeX
                       distribution such as MiKTeX/TeX Live). Both files
                       compile cleanly (two pdflatex passes, zero
                       errors/warnings) as of the last update.
sim/
  antchain_sim/        Simulation package
    tsp.py             Deterministic instance generation, tour eval, ACO solver
    miner.py            Heterogeneous miner agents (hashrate, skill, strategy)
    consensus.py         Quality-weighted hash target T(x), difficulty adjuster
    blockchain.py         Discrete-time mining-round + chain simulation
    experiment.py          3-way experiment orchestration + metrics
    metrics.py               Gini coefficient etc.
    plotting.py               Comparison plots
  run_experiment.py    CLI entry point
  requirements.txt
node/
  antchain_node/        Real multi-node implementation (see node/README.md)
    crypto.py, transaction.py, tsp.py, block.py, consensus.py,
    chain.py, mempool.py, miner.py, network.py, node.py, cli.py
  requirements.txt
cpp/                   C++17 twin of sim/ (see cpp/README.md)
  include/, src/, tests/, CMakeLists.txt
cpp_node/               C++17 twin of node/ (see cpp_node/README.md)
  include/, src/, tests/, third_party/ (vendored Monocypher), CMakeLists.txt
web/                   Next.js workbench that drives cpp/ from a browser UI
  app/, package.json (see web/README.md)
results/
  summary.csv          Per-condition metric summary (written by run_experiment.py)
  blocks.csv           Per-block raw data
  comparison_summary.png / block_times.png
```

## Running the simulation

```
cd sim
pip install -r requirements.txt
python run_experiment.py --quick          # fast smoke test
python run_experiment.py                  # full default run (~30 miners, 40 blocks,
                                           #   sha_pow + pure_poao + hybrid x4 lambdas)
python run_experiment.py --miners 50 --blocks 80 --lambdas 0.5 1 2 5 10
```

Output goes to `results/summary.csv`, `results/blocks.csv`, and two PNG
comparison plots.

## What the simulation implements

Three consensus mechanisms, on one shared code path so miner heterogeneity
and metrics are directly comparable (full experimental design's in
`paper/antchain.tex` Section 5):

- **sha_pow** — the baseline: memoryless hash lottery, no optimization
  component.
- **pure_poao** — a naive mechanism where the first miner to hit a quality
  threshold just wins outright, no hash lottery at all. I expected this to
  show high reward inequality and a strategic-miner advantage, and it
  does — that's the negative result in the paper's Section 2.3.
- **hybrid** — the actual PoW+PoAO mechanism from Section 3: a per-block
  TSP instance derived from the previous block hash gates and
  quality-weights a hash lottery via
  `T(x) = T0 * (1 + lambda * (f_old - f(x)) / f_old)`, swept over
  `lambda`.

Metrics I collect per condition: fork rate, block-time variance, Gini
coefficient of block rewards (against Gini of hashrate as the fairness
baseline), strategic-miner advantage (reward share over hashrate share for
pheromone-hoarding and withholding miners), total compute spent, and
useful optimization produced per unit of compute.

## A calibration gotcha

I tuned the defaults (`search_iters_per_tick=20`,
`hybrid_search_fraction=0.35`, `n_cities=16`) so ACO search has enough
budget per block to actually beat the nearest-neighbor reference tour some
of the time — otherwise `lambda` does nothing visible to block time,
because quality never beats the reference and the hash target never
actually gets easier. If you change `n_cities`, `max_ticks`, or population
size, double check `blocks.csv`'s `f_winner` is sometimes meaningfully
below `f_reference` for the hybrid condition. If it never is, bump up
`search_iters_per_tick` or `max_ticks`.
