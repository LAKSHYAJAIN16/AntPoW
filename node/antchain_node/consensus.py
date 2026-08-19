"""Real (integer, 256-bit) consensus math: quality-weighted difficulty target
and retargeting, mirroring paper/antchain.tex Section 3 exactly, but with
actual big integers instead of the simulator's probability shortcuts.
"""
from __future__ import annotations

from dataclasses import dataclass

MAX_TARGET = (1 << 256) - 1


@dataclass
class ConsensusParams:
    n_cities: int = 10
    t_opt_frac: float = 1.15       # quality gate: f(x) <= t_opt_frac * f_reference
    lam: float = 3.0               # lambda: max target multiplier is (1 + lambda)
    difficulty_bits: int = 20      # initial target = MAX_TARGET >> difficulty_bits
    retarget_interval: int = 10    # blocks between difficulty retargets
    target_block_time: float = 8.0  # seconds
    block_reward: float = 50.0
    max_txs_per_block: int = 50

    def initial_target(self) -> int:
        return MAX_TARGET >> self.difficulty_bits


def quality_target(t0: int, lam: float, f_old: float, f_x: float) -> int:
    """T(x) = T0 * (1 + lambda * (f_old - f(x)) / f_old), clipped to
    [T0, T0*(1+lambda)]. Bounded advantage: a perfect solution never buys
    more than a (1+lambda)x easier target than plain hashing would give."""
    if f_old <= 0:
        return t0
    improvement = max(0.0, (f_old - f_x) / f_old)
    multiplier = 1.0 + lam * improvement
    multiplier = min(multiplier, 1.0 + lam)
    target = int(t0 * multiplier)
    return min(target, MAX_TARGET)


def retarget(prev_target: int, actual_seconds: float, expected_seconds: float) -> int:
    """Bitcoin-style retarget: if blocks came slower than expected, ease the
    target (raise it); if faster, harden it. Clamped to a 4x move per period
    so a burst of variance can't swing difficulty absurdly in one step."""
    if actual_seconds <= 0:
        actual_seconds = 0.001
    ratio = actual_seconds / expected_seconds
    ratio = max(0.25, min(4.0, ratio))
    new_target = int(prev_target * ratio)
    return max(1, min(new_target, MAX_TARGET))
