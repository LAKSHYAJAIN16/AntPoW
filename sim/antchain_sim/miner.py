"""Miner agent model: heterogeneous hashrate, ACO skill, and strategy."""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Literal

import numpy as np

from .tsp import AntColonyOptimizer, TSPInstance

Strategy = Literal["honest", "pheromone_hoarder", "withholder"]


@dataclass
class Miner:
    miner_id: int
    hashrate_share: float          # relative hashrate, sums to 1 across population
    aco_skill: float                # multiplier on effective ACO iterations per tick
    strategy: Strategy = "honest"
    is_strategic: bool = False      # convenience flag: strategy != "honest"

    # per-block mutable state
    optimizer: AntColonyOptimizer | None = field(default=None, repr=False)
    best_length: float = np.inf
    best_tour: np.ndarray | None = field(default=None, repr=False)
    _search_budget_accum: float = 0.0  # carries fractional ACO-iteration budget across ticks

    def start_block(self, instance: TSPInstance, rng: np.random.Generator) -> None:
        """Called at the start of every block round."""
        self._search_budget_accum = 0.0
        if self.optimizer is None or self.strategy != "pheromone_hoarder":
            self.optimizer = AntColonyOptimizer(instance, rng=rng)
        else:
            # pheromone_hoarder: keep tau from the previous block instead of
            # resetting it, carrying a (protocol-illegitimate) memory advantage
            # forward onto the new instance. Distance/heuristic table (eta)
            # must still be refreshed for the new instance.
            self.optimizer.instance = instance
            with np.errstate(divide="ignore"):
                self.optimizer.eta = np.where(instance.dist > 0, 1.0 / instance.dist, 0.0)
            self.optimizer.best_tour = None
            self.optimizer.best_length = np.inf
        self.best_length = np.inf
        self.best_tour = None

    def search_step(self, search_iterations: float) -> float:
        """Run ACO for a (possibly fractional) number of iterations this tick,
        scaled by aco_skill. Fractional budget below one full iteration is
        carried forward (not discarded) so low-hashrate-share miners still
        make search progress over many ticks instead of being rounded to
        zero every tick."""
        self._search_budget_accum += search_iterations * self.aco_skill
        n_iter = int(self._search_budget_accum)
        self._search_budget_accum -= n_iter
        if n_iter > 0 and self.optimizer is not None:
            best_tour, best_length = self.optimizer.step(n_iter)
            if best_tour is not None:
                self.best_tour, self.best_length = best_tour, best_length
        return self.best_length


def make_population(
    n_miners: int,
    strategic_fraction: float = 0.15,
    strategic_strategies: tuple[Strategy, ...] = ("pheromone_hoarder", "withholder"),
    hashrate_alpha: float = 1.5,
    skill_low: float = 0.6,
    skill_high: float = 1.8,
    seed: int = 0,
) -> list[Miner]:
    """Build a heterogeneous miner population.

    Hashrate shares are drawn from a Pareto-like heavy tail (mimicking real
    mining-pool concentration) and normalized to sum to 1. A configurable
    fraction of miners are "strategic" (pheromone hoarders or withholders);
    the rest are honest.
    """
    rng = np.random.default_rng(seed)
    raw = rng.pareto(hashrate_alpha, size=n_miners) + 1.0
    shares = raw / raw.sum()
    skills = rng.uniform(skill_low, skill_high, size=n_miners)

    n_strategic = int(round(n_miners * strategic_fraction))
    strategic_ids = set(rng.choice(n_miners, size=n_strategic, replace=False).tolist())

    miners = []
    for i in range(n_miners):
        if i in strategic_ids:
            strat = strategic_strategies[i % len(strategic_strategies)]
        else:
            strat = "honest"
        miners.append(
            Miner(
                miner_id=i,
                hashrate_share=float(shares[i]),
                aco_skill=float(skills[i]),
                strategy=strat,
                is_strategic=strat != "honest",
            )
        )
    return miners
