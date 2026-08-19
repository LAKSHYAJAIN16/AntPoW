"""Discrete-time simulation of a single mining round and a full chain, under
three consensus mechanisms: sha_pow, pure_poao, hybrid.

Each simulated tick represents a small slice of wall-clock time. Per tick,
each miner (a) optionally advances its ACO search on the current block's
instance and (b) optionally attempts hashes, with the split between the two
controlled by a per-mode search_fraction:

    sha_pow    : search_fraction = 0.0  (pure hash lottery, no ACO at all)
    pure_poao  : search_fraction = 1.0  (no hashing; first to reach the
                                          quality gate wins -- the naive,
                                          insecure mechanism of Section 2.3
                                          of the paper)
    hybrid     : search_fraction = params.hybrid_search_fraction
                 (ACO gates + quality-weights a hash lottery, Section 3)

This file intentionally keeps all three mechanisms on one code path so that
miner heterogeneity (hashrate, skill, strategy) and metrics collection are
identical across conditions -- the only thing that changes is the consensus
rule itself.
"""
from __future__ import annotations

import hashlib
import struct
from dataclasses import dataclass, field

import numpy as np
import pandas as pd

from .consensus import ConsensusParams, DifficultyAdjuster, quality_target, win_probability_this_tick
from .miner import Miner
from .tsp import generate_instance, nearest_neighbor_tour, estimate_reference_optimum, TSPInstance

MODE_SEARCH_FRACTION = {
    "sha_pow": 0.0,
    "pure_poao": 1.0,
    # "hybrid": taken from params.hybrid_search_fraction
}


@dataclass
class BlockResult:
    block_index: int
    mode: str
    lam: float
    winner_id: int | None
    block_time_ticks: int
    fork: bool
    n_competing: int
    f_winner: float | None
    f_reference: float
    f_optimum_est: float
    total_hash_attempts: float
    total_aco_iterations: float
    p0: float


def _mine_one_block(
    block_index: int,
    prev_hash: bytes,
    miners: list[Miner],
    params: ConsensusParams,
    difficulty: DifficultyAdjuster,
    n_cities: int,
    max_ticks: int,
    hash_attempts_per_tick_per_share: float,
    search_iters_per_tick: float,
    hybrid_search_fraction: float,
    propagation_delay_ticks: int,
    withhold_frac: float,
    estimate_optimum: bool,
    rng: np.random.Generator,
) -> tuple[BlockResult, bytes]:
    instance: TSPInstance = generate_instance(prev_hash, n_cities=n_cities)
    f_ref = instance.tour_length(nearest_neighbor_tour(instance))
    f_opt_est = estimate_reference_optimum(instance) if estimate_optimum else f_ref
    t_opt = params.t_opt_frac * f_ref

    search_fraction = MODE_SEARCH_FRACTION.get(params.mode, hybrid_search_fraction)
    use_hash_lottery = params.mode != "pure_poao"

    for m in miners:
        m.start_block(instance, rng)

    win_events: list[tuple[int, int, float]] = []  # (tick, miner_id, f_x)
    total_hash_attempts = 0.0
    total_aco_iterations = 0.0
    stop_tick = None

    for tick in range(max_ticks):
        # randomize per-tick miner order so simultaneous eligibility isn't biased by id
        order = rng.permutation(len(miners))
        for idx in order:
            m = miners[idx]
            f_x = None
            eligible = True

            if search_fraction > 0.0:
                search_budget = m.hashrate_share * search_iters_per_tick * search_fraction
                if search_budget > 0:
                    f_x = m.search_step(search_budget)
                    total_aco_iterations += search_budget
                else:
                    f_x = m.best_length
                eligible = f_x <= t_opt

            if not use_hash_lottery:
                if eligible:
                    win_events.append((tick, m.miner_id, f_x))
                continue

            if not eligible:
                continue
            if m.strategy == "withholder" and tick < int(withhold_frac * max_ticks):
                continue  # deliberately not hashing yet

            if search_fraction > 0.0:
                p = quality_target(difficulty.p0, params.lam, f_old=f_ref, f_x=f_x)
            else:
                p = difficulty.p0

            n_attempts = m.hashrate_share * hash_attempts_per_tick_per_share * (1.0 - search_fraction if search_fraction > 0 else 1.0)
            total_hash_attempts += n_attempts
            if n_attempts > 0 and rng.random() < win_probability_this_tick(p, n_attempts):
                win_events.append((tick, m.miner_id, f_x if f_x is not None else f_ref))

        if win_events and stop_tick is None:
            stop_tick = win_events[0][0] + propagation_delay_ticks
        if stop_tick is not None and tick >= stop_tick:
            break

    if not win_events:
        # Fallback: force the miner with the best current quality (or highest
        # hashrate if no ACO ran) to win, so the chain always makes progress.
        if search_fraction > 0.0:
            best = min(miners, key=lambda m: m.best_length)
            f_win = best.best_length
        else:
            best = max(miners, key=lambda m: m.hashrate_share)
            f_win = f_ref
        win_events = [(max_ticks - 1, best.miner_id, f_win)]

    first_tick = win_events[0][0]
    window_events = [e for e in win_events if e[0] <= first_tick + propagation_delay_ticks]
    winner_tick, winner_id, f_winner = window_events[int(rng.integers(len(window_events)))] \
        if len(window_events) > 1 else window_events[0]
    competing_miners = {e[1] for e in window_events} - {winner_id}
    fork = len(competing_miners) > 0

    block_time = winner_tick + 1
    difficulty.record_block_time(block_time)

    header = prev_hash + struct.pack(">iqd", block_index, winner_id, float(f_winner))
    new_hash = hashlib.sha256(header).digest()

    result = BlockResult(
        block_index=block_index,
        mode=params.mode,
        lam=params.lam,
        winner_id=winner_id,
        block_time_ticks=block_time,
        fork=fork,
        n_competing=len(competing_miners) + 1,
        f_winner=float(f_winner),
        f_reference=f_ref,
        f_optimum_est=f_opt_est,
        total_hash_attempts=total_hash_attempts,
        total_aco_iterations=total_aco_iterations,
        p0=difficulty.p0,
    )
    return result, new_hash


def run_chain(
    miners: list[Miner],
    params: ConsensusParams,
    n_blocks: int,
    n_cities: int = 12,
    max_ticks: int = 40,
    target_block_time: float = 15.0,
    hash_attempts_per_tick_per_share: float = 400.0,
    search_iters_per_tick: float = 20.0,
    hybrid_search_fraction: float = 0.35,
    propagation_delay_ticks: int = 1,
    withhold_frac: float = 0.8,
    estimate_optimum: bool = False,
    seed: int = 0,
) -> pd.DataFrame:
    """Simulate n_blocks of a chain under the given consensus mode/params.
    Returns a per-block DataFrame of results."""
    rng = np.random.default_rng(seed)
    difficulty = DifficultyAdjuster(target_block_time=target_block_time, p0_init=2e-4)
    prev_hash = hashlib.sha256(f"genesis-{seed}".encode()).digest()

    rows = []
    for b in range(n_blocks):
        result, prev_hash = _mine_one_block(
            block_index=b,
            prev_hash=prev_hash,
            miners=miners,
            params=params,
            difficulty=difficulty,
            n_cities=n_cities,
            max_ticks=max_ticks,
            hash_attempts_per_tick_per_share=hash_attempts_per_tick_per_share,
            search_iters_per_tick=search_iters_per_tick,
            hybrid_search_fraction=hybrid_search_fraction,
            propagation_delay_ticks=propagation_delay_ticks,
            withhold_frac=withhold_frac,
            estimate_optimum=estimate_optimum,
            rng=rng,
        )
        rows.append(result.__dict__)
    return pd.DataFrame(rows)
