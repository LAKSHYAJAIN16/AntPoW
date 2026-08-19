"""Deterministic per-block TSP instance generation, tour verification, and
ACO search -- the real (non-probabilistic) counterpart of sim/antchain_sim/tsp.py,
used by actual mining and block validation.
"""
from __future__ import annotations

import hashlib
from dataclasses import dataclass, field

import numpy as np


def seed_from_hash(prev_hash_hex: str) -> int:
    digest = hashlib.sha256(bytes.fromhex(prev_hash_hex)).digest()
    return int.from_bytes(digest[:8], "big") & 0x7FFFFFFFFFFFFFFF


@dataclass
class TSPInstance:
    n_cities: int
    dist: np.ndarray = field(repr=False)

    def tour_length(self, tour: list[int]) -> float:
        arr = np.asarray(tour, dtype=int)
        return float(self.dist[arr, np.roll(arr, -1)].sum())

    def is_valid_tour(self, tour: list[int]) -> bool:
        return (
            len(tour) == self.n_cities
            and sorted(tour) == list(range(self.n_cities))
        )


def generate_instance(prev_hash_hex: str, n_cities: int) -> TSPInstance:
    """Gen(H(prev_block)) -- every honest node derives the identical instance
    from the previous block's hash, so no miner has a precomputation edge."""
    seed = seed_from_hash(prev_hash_hex)
    rng = np.random.default_rng(seed)
    coords = rng.random((n_cities, 2))
    diff = coords[:, None, :] - coords[None, :, :]
    dist = np.sqrt((diff ** 2).sum(axis=-1))
    return TSPInstance(n_cities=n_cities, dist=dist)


def nearest_neighbor_tour(instance: TSPInstance, start: int = 0) -> list[int]:
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
    return tour


def reference_length(instance: TSPInstance) -> float:
    """Cheap deterministic reference every node can recompute during
    validation -- initializes f_old for the quality-weighted target."""
    return instance.tour_length(nearest_neighbor_tour(instance, start=0))


class AntColonyOptimizer:
    def __init__(self, instance: TSPInstance, n_ants: int = 16, alpha: float = 1.0,
                 beta: float = 3.0, rho: float = 0.1, q: float = 1.0):
        self.instance = instance
        self.n_ants = n_ants
        self.alpha = alpha
        self.beta = beta
        self.rho = rho
        self.q = q
        self.rng = np.random.default_rng()  # miner-local randomness -- NOT part of consensus
        n = instance.n_cities
        self.tau = np.full((n, n), 1.0)
        with np.errstate(divide="ignore"):
            self.eta = np.where(instance.dist > 0, 1.0 / instance.dist, 0.0)
        self.best_tour: list[int] | None = None
        self.best_length = float("inf")

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
            weights = (self.tau[current] ** self.alpha) * (self.eta[current] ** self.beta) * mask
            total = weights.sum()
            if total <= 0:
                candidates = np.flatnonzero(mask)
                nxt = int(candidates[int(self.rng.integers(len(candidates)))])
            else:
                r = self.rng.random() * total
                cum = np.cumsum(weights)
                nxt = int(np.searchsorted(cum, r, side="right"))
                if nxt >= n:
                    nxt = int(np.flatnonzero(mask)[-1])
            tour[step] = nxt
            visited[nxt] = True
            current = nxt
        return tour

    def run(self, n_iterations: int) -> tuple[list[int], float]:
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
                self.best_tour = tours[best_idx].tolist()
        return self.best_tour, self.best_length
