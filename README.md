# AntChain: Useful Proof-of-Work via Distributed Ant Colony Optimization

Research project exploring whether Ant Colony Optimization (ACO) can be
embedded inside Bitcoin-style Proof-of-Work without breaking the security
properties a memoryless hash lottery provides.

There are two implementations here, for two different purposes:

- **`sim/`** — a fast *research simulator*. Probabilistic mining, thousands
  of blocks in seconds, built to answer "does this mechanism have good
  security/fairness/efficiency properties across many parameter settings."
  Not runnable as an actual currency.
- **`node/`** — a real, runnable, multi-node *toy cryptocurrency*: real
  SHA-256 mining, real ECDSA-signed transactions, a real TCP gossip network,
  a persisted chain. Built to show the mechanism actually working end to
  end, on however many local/LAN nodes you start. See
  [`node/README.md`](node/README.md) for installation and usage — start
  there if you want to run it, not just read about it.

## Layout

```
paper/
  antchain.tex        LaTeX spec: mechanism design + security analysis
                       + experimental protocol. Compile with pdflatex
                       (requires a LaTeX distribution such as MiKTeX/TeX Live,
                       not installed in this environment).
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
and metrics are directly comparable (see `paper/antchain.tex` Section 5 for
the full experimental design):

- **sha_pow** — baseline memoryless hash lottery, no optimization component.
- **pure_poao** — naive mechanism where the first miner to reach a quality
  threshold wins outright (no hash lottery). Expected to show high reward
  inequality and strategic-miner advantage, per the paper's Section 2.3
  negative result.
- **hybrid** — the paper's PoW+PoAO mechanism (Section 3): a per-block TSP
  instance derived from the previous block hash gates and quality-weights a
  hash lottery via `T(x) = T0 * (1 + lambda * (f_old - f(x)) / f_old)`,
  swept over `lambda`.

Metrics collected per condition: fork rate, block-time variance, Gini
coefficient of block rewards (vs. Gini of hashrate as the fairness
baseline), strategic-miner advantage (reward share / hashrate share for
pheromone-hoarding and withholding miners), total compute expenditure, and
useful optimization produced per unit compute.

## Calibration note

Default parameters (`search_iters_per_tick=20`, `hybrid_search_fraction=0.35`,
`n_cities=16`) were tuned so that ACO search has enough budget per block to
meaningfully beat the nearest-neighbor reference tour at least some of the
time — otherwise `lambda` has no visible effect on block time (quality never
exceeds the reference, so the hash target never actually gets easier). If you
change `n_cities`, `max_ticks`, or population size, re-check that
`blocks.csv`'s `f_winner` is sometimes meaningfully below `f_reference` for
the hybrid condition; if it never is, increase `search_iters_per_tick` or
`max_ticks`.
