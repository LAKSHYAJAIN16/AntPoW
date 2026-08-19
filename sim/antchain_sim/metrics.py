"""Metric helpers: Gini coefficient and simple summary stats."""
from __future__ import annotations

import numpy as np


def gini(values: np.ndarray) -> float:
    """Standard Gini coefficient for a non-negative array of values
    (e.g. blocks won per miner, or hashrate share per miner)."""
    x = np.asarray(values, dtype=float)
    if x.sum() <= 0:
        return 0.0
    x = np.sort(x)
    n = len(x)
    cum = np.cumsum(x)
    return float((n + 1 - 2 * (cum.sum() / cum[-1])) / n)


def coefficient_of_variation(values: np.ndarray) -> float:
    x = np.asarray(values, dtype=float)
    if x.mean() == 0:
        return 0.0
    return float(x.std() / x.mean())
