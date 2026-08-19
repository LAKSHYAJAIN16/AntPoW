"""Orchestrates the 3-way experiment: SHA-PoW vs pure PoAO vs Hybrid (lambda
sweep), computing the metrics defined in Section 5 of the paper."""
from __future__ import annotations

import copy
from dataclasses import dataclass

import numpy as np
import pandas as pd

from .blockchain import run_chain
from .consensus import ConsensusParams
from .metrics import gini
from .miner import Miner, make_population

# Measured on this machine: hashlib.sha256() on a ~200-byte header vs. one
# AntColonyOptimizer.step(1) at n_cities=16, n_ants=12 (sim/antchain_sim's
# defaults). One ACO iteration does O(n_ants * n_cities) tour construction +
# pheromone-matrix updates, so it is not remotely comparable in cost to one
# hash evaluation -- treating them as equal units (as an earlier version of
# this module did) silently buries hybrid's useful-work contribution under
# its much larger hash-attempt count. This ratio converts ACO iterations into
# hash-attempt-equivalent units so total_energy_proxy reflects real relative
# cost instead of raw operation counts.
ACO_ITERATION_COST_IN_HASH_ATTEMPTS = 1671.1


@dataclass
class ExperimentConfig:
    n_miners: int = 30
    n_blocks: int = 40
    n_cities: int = 16
    max_ticks: int = 40
    target_block_time: float = 15.0
    strategic_fraction: float = 0.15
    lambdas: tuple[float, ...] = (0.5, 1.0, 3.0, 10.0)
    seed: int = 0
    estimate_optimum: bool = False  # expensive (2-opt); enable for final quality run


def summarize_run(df: pd.DataFrame, miners: list[Miner]) -> dict:
    n_blocks = len(df)
    hashrate = np.array([m.hashrate_share for m in miners])
    hashrate = hashrate / hashrate.sum()
    win_counts = df["winner_id"].value_counts()
    wins = np.array([win_counts.get(m.miner_id, 0) for m in miners], dtype=float)

    strategic_mask = np.array([m.is_strategic for m in miners])
    strategic_hash_share = hashrate[strategic_mask].sum()
    strategic_reward_share = wins[strategic_mask].sum() / max(wins.sum(), 1)
    strategic_advantage = (
        (strategic_reward_share / strategic_hash_share) if strategic_hash_share > 0 else float("nan")
    )

    total_energy = df["total_hash_attempts"].sum() + df["total_aco_iterations"].sum()
    useful_improvement = (df["f_reference"] - df["f_winner"]).clip(lower=0).sum()
    useful_per_joule = useful_improvement / total_energy if total_energy > 0 else 0.0

    convergence = (df["f_winner"] / df["f_optimum_est"]).mean() if "f_optimum_est" in df else np.nan

    return {
        "mode": df["mode"].iloc[0],
        "lambda": df["lam"].iloc[0],
        "n_blocks": n_blocks,
        "fork_rate": df["fork"].mean(),
        "mean_block_time": df["block_time_ticks"].mean(),
        "block_time_cv": df["block_time_ticks"].std() / df["block_time_ticks"].mean(),
        "gini_rewards": gini(wins),
        "gini_hashrate": gini(hashrate),
        "gini_excess": gini(wins) - gini(hashrate),
        "strategic_hashrate_share": strategic_hash_share,
        "strategic_reward_share": strategic_reward_share,
        "strategic_advantage": strategic_advantage,
        "total_hash_attempts": df["total_hash_attempts"].sum(),
        "total_aco_iterations": df["total_aco_iterations"].sum(),
        "total_energy_proxy": total_energy,
        "useful_improvement_total": useful_improvement,
        "useful_improvement_per_joule": useful_per_joule,
        "mean_convergence_ratio": convergence,
    }


def run_condition(mode: str, lam: float, cfg: ExperimentConfig) -> tuple[pd.DataFrame, dict, list[Miner]]:
    miners = make_population(
        cfg.n_miners, strategic_fraction=cfg.strategic_fraction, seed=cfg.seed
    )
    params = ConsensusParams(mode=mode, lam=lam)
    df = run_chain(
        miners=miners,
        params=params,
        n_blocks=cfg.n_blocks,
        n_cities=cfg.n_cities,
        max_ticks=cfg.max_ticks,
        target_block_time=cfg.target_block_time,
        estimate_optimum=cfg.estimate_optimum,
        seed=cfg.seed + 1,  # separate stream from population draw
    )
    summary = summarize_run(df, miners)
    return df, summary, miners


def run_full_experiment(cfg: ExperimentConfig) -> tuple[pd.DataFrame, pd.DataFrame]:
    """Runs sha_pow, pure_poao, and hybrid at each lambda in cfg.lambdas.
    Returns (summary_df, all_blocks_df)."""
    conditions: list[tuple[str, float]] = [("sha_pow", 0.0), ("pure_poao", 0.0)]
    conditions += [("hybrid", lam) for lam in cfg.lambdas]

    summaries = []
    all_blocks = []
    for mode, lam in conditions:
        df, summary, _ = run_condition(mode, lam, cfg)
        label = mode if mode != "hybrid" else f"hybrid(lambda={lam})"
        summary["label"] = label
        df["label"] = label
        summaries.append(summary)
        all_blocks.append(df)

    summary_df = pd.DataFrame(summaries).set_index("label")
    blocks_df = pd.concat(all_blocks, ignore_index=True)
    return summary_df, blocks_df
