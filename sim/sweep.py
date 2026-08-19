#!/usr/bin/env python
"""Parameter sweep over t_opt_frac (quality gate looseness) and
hybrid_search_fraction (how much of a hybrid miner's compute goes to ACO vs
hashing), to find a calibration where hybrid actually shows the paper's
predicted behavior: fork rate / fairness close to SHA-PoW, while recovering
a meaningful fraction of pure PoAO's useful-work-per-joule.

sha_pow and pure_poao only depend on t_opt_frac (not on
hybrid_search_fraction), so they're computed once per t_opt_frac and reused
across the hybrid_search_fraction sweep.
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import pandas as pd

from antchain_sim.blockchain import run_chain
from antchain_sim.consensus import ConsensusParams
from antchain_sim.experiment import summarize_run
from antchain_sim.miner import make_population

T_OPT_FRACS = [1.02, 1.05, 1.10, 1.20]
SEARCH_FRACTIONS = [0.35, 0.6, 0.85]
LAM = 3.0
N_MINERS = 20
N_BLOCKS = 20
N_CITIES = 14
MAX_TICKS = 30
SEED = 7


def run_one(mode, lam, t_opt_frac, search_fraction, seed) -> dict:
    miners = make_population(N_MINERS, seed=seed)
    params = ConsensusParams(mode=mode, lam=lam, t_opt_frac=t_opt_frac)
    df = run_chain(
        miners=miners, params=params, n_blocks=N_BLOCKS, n_cities=N_CITIES,
        max_ticks=MAX_TICKS, hybrid_search_fraction=search_fraction, seed=seed + 1,
    )
    s = summarize_run(df, miners)
    s["t_opt_frac"] = t_opt_frac
    s["search_fraction"] = search_fraction
    return s


def main() -> None:
    rows = []
    for t_opt in T_OPT_FRACS:
        print(f"=== t_opt_frac={t_opt} ===")
        sha = run_one("sha_pow", 0.0, t_opt, 0.0, SEED)
        sha["condition"] = "sha_pow"
        rows.append(sha)
        print(f"  sha_pow: fork={sha['fork_rate']:.3f} gini={sha['gini_rewards']:.3f}")

        poao = run_one("pure_poao", 0.0, t_opt, 1.0, SEED)
        poao["condition"] = "pure_poao"
        rows.append(poao)
        print(f"  pure_poao: fork={poao['fork_rate']:.3f} gini={poao['gini_rewards']:.3f} "
              f"useful/joule={poao['useful_improvement_per_joule']:.5f}")

        for sf in SEARCH_FRACTIONS:
            hyb = run_one("hybrid", LAM, t_opt, sf, SEED)
            hyb["condition"] = f"hybrid(sf={sf})"
            rows.append(hyb)
            ratio = (hyb["useful_improvement_per_joule"] / poao["useful_improvement_per_joule"]
                     if poao["useful_improvement_per_joule"] > 0 else float("nan"))
            print(f"  hybrid(sf={sf}): fork={hyb['fork_rate']:.3f} gini={hyb['gini_rewards']:.3f} "
                  f"strat_adv={hyb['strategic_advantage']:.3f} "
                  f"useful/joule={hyb['useful_improvement_per_joule']:.5f} "
                  f"(={ratio:.1%} of pure_poao)")

    df = pd.DataFrame(rows)
    out = Path(__file__).resolve().parent.parent / "results" / "sweep.csv"
    df.to_csv(out, index=False)
    print(f"\nWrote {out}")


if __name__ == "__main__":
    main()
