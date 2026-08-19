"""TSP instance generation, tour evaluation, and an Ant Colony Optimization solver.

Instances are generated deterministically from a seed (in AntChain, the seed is
derived from the previous block hash) so that every miner sees the same
instance at the same time with no precomputation advantage.
"""
from __future__ import annotations

import hashlib
from dataclasses import dataclass, field

import numpy as np


def seed_from_bytes(data: bytes) -> int:
    """Derive a 63-bit integer seed from arbitrary bytes (e.g. a block hash)."""
    digest = hashlib.sha256(data).digest()
    return int.from_bytes(digest[:8], "big") & 0x7FFFFFFFFFFFFFFF


@dataclass
class TSPInstance:
    n_cities: int
    coords: np.ndarray  # (n_cities, 2)
    dist: np.ndarray = field(repr=False)  # (n_cities, n_cities)
    instance_id: str

    def tour_length(self, tour: np.ndarray) -> float:
        """O(n) cost of a candidate tour (array of city indices, a permutation)."""
        return float(self.dist[tour, np.roll(tour, -1)].sum())

    def is_valid_tour(self, tour: np.ndarray) -> bool:
        return (
            len(tour) == self.n_cities
            and len(set(tour.tolist())) == self.n_cities
            and tour.min() == 0
            and tour.max() == self.n_cities - 1
        )


def generate_instance(seed_bytes: bytes, n_cities: int = 30) -> TSPInstance:
    """Deterministic instance generator Gen(H(block)). Public and reproducible."""
    seed = seed_from_bytes(seed_bytes)
    rng = np.random.default_rng(seed)
    coords = rng.random((n_cities, 2))
    diff = coords[:, None, :] - coords[None, :, :]
    dist = np.sqrt((diff ** 2).sum(axis=-1))
    instance_id = hashlib.sha256(seed_bytes).hexdigest()[:16]
    return TSPInstance(n_cities=n_cities, coords=coords, dist=dist, instance_id=instance_id)


def nearest_neighbor_tour(instance: TSPInstance, start: int = 0) -> np.ndarray:
    """Cheap reference heuristic used to initialize f_old / T_opt."""
    n = instance.n_cities
    unvisited = set(range(n))
    tour = [start]
    unvisited.remove(start)
    current = start
    while unvisited:
        nxt = min(unvisited, key=lambda c: instance.dist[current, c])
        tour.append(nxt)
        unvisited.remove(nxt)
        current = nxt
    return np.array(tour)


def two_opt(instance: TSPInstance, tour: np.ndarray, max_passes: int = 5) -> np.ndarray:
    """Local-search refinement used only to estimate a strong reference optimum
    for the convergence-quality metric (not part of the mining protocol)."""
    tour = tour.copy()
    n = len(tour)
    dist = instance.dist
    for _ in range(max_passes):
        improved = False
        for i in range(n - 1):
            for j in range(i + 2, n):
                if i == 0 and j == n - 1:
                    continue
                a, b = tour[i], tour[i + 1]
                c, d = tour[j], tour[(j + 1) % n]
                delta = (dist[a, c] + dist[b, d]) - (dist[a, b] + dist[c, d])
                if delta < -1e-12:
                    tour[i + 1:j + 1] = tour[i + 1:j + 1][::-1]
                    improved = True
        if not improved:
            break
    return tour


def estimate_reference_optimum(instance: TSPInstance, n_starts: int = 6) -> float:
    """Best-of-several nearest-neighbor + 2-opt runs; used only for the
    convergence-quality metric, never by the protocol itself."""
    best = np.inf
    n = instance.n_cities
    starts = np.linspace(0, n - 1, num=min(n_starts, n), dtype=int)
    for s in starts:
        t = nearest_neighbor_tour(instance, start=int(s))
        t = two_opt(instance, t)
        length = instance.tour_length(t)
        if length < best:
            best = length
    return float(best)


class AntColonyOptimizer:
    """Incremental ACO solver: run() can be called repeatedly with a small
    iteration budget so a miner's search can be interleaved with hash attempts
    in the discrete-time simulation loop.

    Pheromone state (`tau`) is the mechanism's memory. Protocol-honest miners
    call reset() at the start of every block round; a "pheromone-hoarder"
    strategic miner (see miner.py) deliberately skips reset() to carry an
    advantage across blocks/instances.
    """

    def __init__(self, instance: TSPInstance, n_ants: int = 12, alpha: float = 1.0,
                 beta: float = 3.0, rho: float = 0.1, q: float = 1.0, rng: np.random.Generator | None = None):
        self.instance = instance
        self.n_ants = n_ants
        self.alpha = alpha
        self.beta = beta
        self.rho = rho
        self.q = q
        self.rng = rng if rng is not None else np.random.default_rng()
        n = instance.n_cities
        self.tau = np.full((n, n), 1.0)
        with np.errstate(divide="ignore"):
            self.eta = np.where(instance.dist > 0, 1.0 / instance.dist, 0.0)
        self.best_tour: np.ndarray | None = None
        self.best_length: float = np.inf

    def reset(self, instance: TSPInstance | None = None) -> None:
        if instance is not None:
            self.instance = instance
            with np.errstate(divide="ignore"):
                self.eta = np.where(instance.dist > 0, 1.0 / instance.dist, 0.0)
        n = self.instance.n_cities
        self.tau = np.full((n, n), 1.0)
        self.best_tour = None
        self.best_length = np.inf

    def _construct_tour(self) -> np.ndarray:
        n = self.instance.n_cities
        start = int(self.rng.integers(n))
        visited = np.zeros(n, dtype=bool)
        visited[start] = True
        tour = np.empty(n, dtype=int)
        tour[0] = start
        current = start
        for step in range(1, n):
            mask = ~visited
            weights = (self.tau[current] ** self.alpha) * (self.eta[current] ** self.beta)
            weights = weights * mask
            total = weights.sum()
            if total <= 0:
                candidates = np.flatnonzero(mask)
                nxt = int(candidates[int(self.rng.integers(len(candidates)))])
            else:
                # roulette-wheel selection via a single uniform draw + cumsum
                # search, avoiding np.random.Generator.choice's per-call
                # overhead (probability-array validation, etc.) which
                # dominates runtime at the iteration counts this simulation needs.
                r = self.rng.random() * total
                cum = np.cumsum(weights)
                nxt = int(np.searchsorted(cum, r, side="right"))
                if nxt >= n:
                    nxt = int(np.flatnonzero(mask)[-1])
            tour[step] = nxt
            visited[nxt] = True
            current = nxt
        return tour

    def step(self, n_iterations: int = 1) -> tuple[np.ndarray, float]:
        """Run `n_iterations` ACO iterations (construct + update pheromones).
        Returns the best (tour, length) found so far across all calls since
        the last reset()."""
        n = self.instance.n_cities
        for _ in range(n_iterations):
            tours = [self._construct_tour() for _ in range(self.n_ants)]
            lengths = [self.instance.tour_length(t) for t in tours]
            self.tau *= (1 - self.rho)
            for t, length in zip(tours, lengths):
                if length <= 0:
                    continue
                deposit = self.q / length
                self.tau[t, np.roll(t, -1)] += deposit
                self.tau[np.roll(t, -1), t] += deposit
            best_idx = int(np.argmin(lengths))
            if lengths[best_idx] < self.best_length:
                self.best_length = lengths[best_idx]
                self.best_tour = tours[best_idx]
        return self.best_tour, self.best_length
