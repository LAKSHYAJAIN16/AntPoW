"""Consensus mechanics: quality-weighted hash target and win-probability model.

Targets are expressed directly as per-attempt win probabilities p in (0, 1]
rather than as 256-bit integers -- p = T / 2**256 is exactly the quantity
that matters, so working with p directly is equivalent and avoids big-integer
bookkeeping in the simulation.
"""
from __future__ import annotations

from dataclasses import dataclass


@dataclass
class ConsensusParams:
    mode: str              # "sha_pow" | "pure_poao" | "hybrid"
    p0: float = 2e-4       # base per-attempt win probability (T0), retargeted over time
    lam: float = 1.0       # lambda: max multiplier is (1 + lambda)
    t_opt_frac: float = 1.10  # quality gate: f(x) <= t_opt_frac * f_reference


def quality_target(p0: float, lam: float, f_old: float, f_x: float) -> float:
    """T(x) = T0 * (1 + lambda * (f_old - f(x)) / f_old), clipped to
    [p0, p0*(1+lambda)] so a below-reference solution never beats plain PoW
    (Property: bounded advantage) and quality can only help, never hurt.
    """
    if f_old <= 0:
        return p0
    improvement = (f_old - f_x) / f_old
    improvement = max(0.0, improvement)
    p = p0 * (1.0 + lam * improvement)
    return min(p, p0 * (1.0 + lam))


def win_probability_this_tick(p_per_attempt: float, n_attempts: float) -> float:
    """P(>=1 success in n_attempts iid Bernoulli(p) trials), n_attempts allowed
    to be fractional (treated as an expected-attempts rate for small dt)."""
    if n_attempts <= 0 or p_per_attempt <= 0:
        return 0.0
    # 1 - (1-p)^n, numerically stable for tiny p via expm1/log1p
    import math
    return -math.expm1(n_attempts * math.log1p(-min(p_per_attempt, 0.999999)))


class DifficultyAdjuster:
    """Bitcoin-style retargeting of p0 to hold expected block time constant,
    independent of the optimization component (Property: instance-independent
    difficulty is preserved because this never looks at f(x))."""

    def __init__(self, target_block_time: float, window: int = 10, p0_init: float = 2e-4):
        self.target_block_time = target_block_time
        self.window = window
        self.p0 = p0_init
        self._recent_times: list[float] = []

    def record_block_time(self, block_time: float) -> None:
        self._recent_times.append(block_time)
        if len(self._recent_times) >= self.window:
            avg = sum(self._recent_times) / len(self._recent_times)
            ratio = avg / self.target_block_time
            # blocks slower than target -> avg > target -> ratio > 1 -> ease (raise p0)
            # blocks faster than target -> ratio < 1 -> harden (lower p0)
            ratio = max(0.25, min(4.0, ratio))
            self.p0 = self.p0 * ratio
            self._recent_times = []
